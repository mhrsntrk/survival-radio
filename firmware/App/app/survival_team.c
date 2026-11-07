/* Survival Radio: team features over short data bursts
 *
 * Radios with the same team code (Menu > Team) hear only each other on PMR (the
 * code is a CTCSS tone there) and exchange 20-byte packets on the current channel:
 *   MSG   a preset message (Team screen, M)
 *   PING  "who is there?" (Team screen, *, and the Watch timer); team radios
 *         answer with PONG automatically: name, how strong the PING arrived,
 *         battery and clock
 *   ID    sent when PTT is released: the others see who talked
 * Packets: FFSK 1200/1800 baud on the BK4819 (the FSK set-up follows the
 * Messenger of joaquimorg/uv-k5-firmware-custom, Apache 2.0), with a short tone
 * first so receivers in battery save wake up.
 */

#ifdef ENABLE_SURVIVAL

#include <stddef.h>
#include <string.h>

#include "app/app.h"
#include "app/chFrScanner.h"
#include "app/survival.h"
#include "audio.h"
#include "driver/bk4819.h"
#include "driver/bk4819-regs.h"
#include "driver/backlight.h"
#include "driver/st7565.h"
#include "driver/system.h"
#include "external/printf/printf.h"
#include "frequencies.h"
#include "functions.h"
#include "helper/battery.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/helper.h"
#include "ui/ui.h"

// ---------------------------------------------------------------- names and messages
const char *const gSurvivalNames[SURV_NAME_COUNT] = {
    "ALPHA", "BRAVO", "CHARLIE", "DELTA", "ECHO", "FOXTROT", "GOLF", "HOTEL", "INDIA",
    "JULIETT", "KILO", "LIMA", "MIKE", "NOVEMBER", "OSCAR", "PAPA", "QUEBEC", "ROMEO",
    "SIERRA", "TANGO", "UNIFORM", "VICTOR", "WHISKEY", "XRAY", "YANKEE", "ZULU",
    "BASE", "CAR", "CAMP", "HOME",
};

static const char *const MESSAGES[] = {
    "OK, ALL GOOD", "COME TO ME", "NEED HELP", "WAIT FOR ME", "STAY WHERE YOU", "ON MY WAY",
    "AT MEETING PT", "GO BACK", "INJURED", "I AM LOST", "BATTERY LOW", "TALK ON PMR",
};
// rows of 18 characters: a second line for the long ones
static const char *const MESSAGES2[] = {
    "", "", "", "", "ARE", "", "", "", "", "", "", "",
};

// ---------------------------------------------------------------- packet
enum { PKT_MSG = 1, PKT_PING, PKT_PONG, PKT_ID };

typedef struct {
    uint8_t  magic[2];   // 'S' 'R'
    uint8_t  type;
    uint8_t  team;
    uint8_t  from;       // name index
    uint8_t  seq;
    uint8_t  arg;        // MSG: message, PONG: how strong the PING was (-dBm)
    uint8_t  battery;    // percent
    uint16_t clock;      // minutes since midnight, 0xFFFF: not set
    uint8_t  reserved[8];
    uint16_t crc;
} Packet_t;

_Static_assert(sizeof(Packet_t) == 20, "packet size");

static const uint16_t TONE2_FREQ = 0x3065;   // 1200 Hz FSK tone (REG_72)

static Packet_t sRx;
static uint8_t  sRxIndex;
static bool     sRxActive;
static bool     sRxOn;
static uint8_t  sSeq;

// ---------------------------------------------------------------- state
#define LOG_N      6
#define PARTNER_N  6

typedef struct {
    uint8_t  from;
    uint8_t  what;    // message index, 0xFE = answered a check, 0xFD = talked
    uint32_t at_s;    // uptime seconds
} LogEntry_t;

typedef struct {
    uint8_t  name;    // 0xFF = free
    int8_t   rssi;    // dBm / 2 (how strong our PING reached them)
    uint8_t  battery;
    uint8_t  missed;  // watch checks without an answer
    uint32_t at_s;
} Partner_t;

static LogEntry_t sLog[LOG_N];
static uint8_t    sLogCount;
static Partner_t  sPartner[PARTNER_N];
static uint32_t   sUptime_s;
static uint16_t   sClockMin = 0xFFFF;   // 0..1439 or unset
static uint8_t    sClockSec;
static uint16_t   sPongDelay_10ms;      // > 0: a PONG is due
static uint16_t   sWatch_s;
static uint8_t    sLastTalker = 0xFF;
static uint8_t    sPingDbm;             // how strong the last PING arrived (-dBm)
static uint8_t    sPongTries;
static uint32_t   sCheckAt;             // uptime of our last check, 0: none open
static uint8_t    sLastTalker_s;

static uint16_t Crc(const Packet_t *p)
{
    const uint8_t *b   = (const uint8_t *)p;
    uint16_t       crc = 0xFFFF;
    for (uint8_t n = 0; n < offsetof(Packet_t, crc); n++) {
        crc ^= (uint16_t)b[n] << 8;
        for (uint8_t i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
}

uint16_t SURVIVAL_ClockMin(void)
{
    return sClockMin;
}

void SURVIVAL_SetClockMin(uint16_t min)
{
    sClockMin = (min < 1440) ? min : 0xFFFF;
    sClockSec = 0;
}

// ---------------------------------------------------------------- FSK modem
static void FskConfig(void)
{
    BK4819_WriteRegister(BK4819_REG_70, (1u << 7) | (96u << 0));   // tone 2 (FSK) on, gain 96
    BK4819_WriteRegister(BK4819_REG_72, TONE2_FREQ);
    BK4819_WriteRegister(BK4819_REG_58,
        (1u << 13) |   // TX: FFSK 1200/1800
        (7u << 10) |   // RX: FFSK 1200/1800
        (3u << 8)  |   // RX gain
        (1u << 1)  |   // RX bandwidth FFSK 1200/1800
        (1u << 0));    // FSK on
    BK4819_WriteRegister(BK4819_REG_5A, 0x5555);   // sync bytes
    BK4819_WriteRegister(BK4819_REG_5B, 0x55AA);
    BK4819_WriteRegister(BK4819_REG_5C, 0x5625);
}

#define REG59_BASE ((15u << 4) | (1u << 3))   // preamble length 15, sync length 4 bytes

// receiver on while a team code is set (called at the end of RADIO_SetupRegisters)
void SURVIVAL_TeamRxOn(void)
{
    sRxOn = SURVIVAL_Team() != 0;
    if (!sRxOn)
        return;
    FskConfig();
    BK4819_WriteRegister(BK4819_REG_5E, (64u << 3) | (1u << 0));   // almost full: 1 word
    BK4819_WriteRegister(BK4819_REG_5D, (uint16_t)(sizeof(Packet_t) + 2) << 8);
    if (sRxActive)   // a packet is coming in (the sender's CTCSS stops under data): keep it
        return;
    BK4819_WriteRegister(BK4819_REG_59, (1u << 15) | (1u << 14) | REG59_BASE);   // clear FIFOs
    BK4819_WriteRegister(BK4819_REG_59, (1u << 12) | REG59_BASE);               // RX on
    BK4819_WriteRegister(BK4819_REG_02, 0);
}

uint16_t SURVIVAL_TeamIrqMask(void)
{
    return sRxOn ? (BK4819_REG_3F_FSK_RX_SYNC | BK4819_REG_3F_FSK_RX_FINISHED | BK4819_REG_3F_FSK_FIFO_ALMOST_FULL |
                    BK4819_REG_3F_FSK_TX_FINISHED) : 0;   // TX finished: FskSend sees the end at once
}

void SURVIVAL_TeamRxOff(void)
{
    if (!sRxOn)
        return;
    BK4819_WriteRegister(BK4819_REG_59, REG59_BASE);
    BK4819_WriteRegister(BK4819_REG_58, 0);
    BK4819_WriteRegister(BK4819_REG_70, 0);
    sRxOn = false;
}

// sends one packet; the radio must be transmitting already
static void FskSend(const Packet_t *p)
{
    const uint16_t css  = BK4819_ReadRegister(BK4819_REG_51);
    const uint16_t dev  = BK4819_ReadRegister(BK4819_REG_40);
    const uint16_t filt = BK4819_ReadRegister(BK4819_REG_2B);

    BK4819_WriteRegister(BK4819_REG_51, 0);   // no CTCSS under the data
    const bool wide = gCurrentVfo->CHANNEL_BANDWIDTH == BK4819_FILTER_BW_WIDE && !SURVIVAL_TxCapped(gCurrentVfo->pTX->Frequency);
    BK4819_WriteRegister(BK4819_REG_40, (dev & 0xF000) | (wide ? 1050 : 850));
    BK4819_WriteRegister(BK4819_REG_2B, (1u << 2) | (1u << 0));   // no TX HPF / pre-emphasis

    FskConfig();
    BK4819_WriteRegister(BK4819_REG_5D, (uint16_t)sizeof(Packet_t) << 8);
    BK4819_WriteRegister(BK4819_REG_59, (1u << 15) | (1u << 14) | REG59_BASE);
    BK4819_WriteRegister(BK4819_REG_59, REG59_BASE);
    SYSTEM_DelayMs(20);

    const uint8_t *b = (const uint8_t *)p;
    for (uint8_t i = 0; i < sizeof(Packet_t); i += 2)
        BK4819_WriteRegister(BK4819_REG_5F, (uint16_t)(b[i + 1] << 8) | b[i]);

    BK4819_WriteRegister(BK4819_REG_59, (1u << 11) | REG59_BASE);   // TX on
    for (uint16_t t = 0; t < 1000 / 5; t++) {
        SYSTEM_DelayMs(5);
        if (BK4819_ReadRegister(BK4819_REG_0C) & 1u) {
            BK4819_WriteRegister(BK4819_REG_02, 0);
            if (BK4819_ReadRegister(BK4819_REG_02) & BK4819_REG_02_FSK_TX_FINISHED)
                break;
        }
    }
    SYSTEM_DelayMs(50);

    BK4819_WriteRegister(BK4819_REG_59, REG59_BASE);
    BK4819_WriteRegister(BK4819_REG_58, 0);
    BK4819_WriteRegister(BK4819_REG_70, 0);
    BK4819_WriteRegister(BK4819_REG_40, dev);
    BK4819_WriteRegister(BK4819_REG_2B, filt);
    BK4819_WriteRegister(BK4819_REG_51, css);
}

static void Fill(Packet_t *p, uint8_t type, uint8_t arg)
{
    memset(p, 0, sizeof(*p));
    p->magic[0] = 'S';
    p->magic[1] = 'R';
    p->type     = type;
    p->team     = SURVIVAL_Team();
    p->from     = SURVIVAL_TeamName();
    p->seq      = ++sSeq;
    p->arg      = arg;
    p->battery  = BATTERY_VoltsToPercent(gBatteryVoltageAverage);
    p->clock    = sClockMin;
    p->crc      = Crc(p);
}

// a whole transmission: carrier with a wake-up tone, the packet, end; false when
// sending is not allowed here (band, SAFE LIMITS, battery)
static bool SendAlone(uint8_t type, uint8_t arg)
{
    Packet_t p;

    if (gCurrentFunction == FUNCTION_TRANSMIT || FUNCTION_IsRx())
        return false;
    if (!SURVIVAL_IsPmrChannel(gEeprom.ScreenChannel[gEeprom.TX_VFO]))
        return false;   // team packets only on the PMR channels
    if (gScanStateDir != SCAN_OFF)
        CHFRSCANNER_Stop();

    RADIO_PrepareTX();
    if (gCurrentFunction != FUNCTION_TRANSMIT)
        return false;

    BK4819_PlaySingleTone(1000, 700, 28, false);   // longer than a battery save sleep: wakes the others
    Fill(&p, type, arg);
    FskSend(&p);
    RADIO_SendEndOfTransmission();   // not APP_EndTransmission: no ID packet after this one
    FUNCTION_Select(FUNCTION_FOREGROUND);
    gFlagEndTransmission = false;
    RADIO_SetupRegisters(true);
    return true;
}

// PTT released with a team code: the others see who talked (still transmitting here)
void SURVIVAL_TeamSendId(void)
{
    if (!SURVIVAL_Team() || gCurrentFunction != FUNCTION_TRANSMIT || SURVIVAL_BeaconActive() ||
        !SURVIVAL_IsPmrChannel(gEeprom.ScreenChannel[gEeprom.TX_VFO]))
        return;
    Packet_t p;
    Fill(&p, PKT_ID, 0);
    FskSend(&p);
}

// ---------------------------------------------------------------- receive
static Partner_t *PartnerFor(uint8_t name)
{
    Partner_t *free = NULL;
    for (uint8_t i = 0; i < PARTNER_N; i++) {
        if (sPartner[i].name == name)
            return &sPartner[i];
        if (sPartner[i].name == 0xFF && !free)
            free = &sPartner[i];
    }
    if (!free) {   // full: the one heard longest ago goes
        free = &sPartner[0];
        for (uint8_t i = 1; i < PARTNER_N; i++)
            if (sPartner[i].at_s < free->at_s)
                free = &sPartner[i];
    }
    free->name = name;
    return free;
}

static void AddLog(uint8_t from, uint8_t what)
{
    if (sLogCount < LOG_N)
        sLogCount++;
    memmove(&sLog[1], &sLog[0], (LOG_N - 1) * sizeof(sLog[0]));
    sLog[0].from = from;
    sLog[0].what = what;
    sLog[0].at_s = sUptime_s;
}

static void Handle(const Packet_t *p, int16_t dbm)
{
    if (p->magic[0] != 'S' || p->magic[1] != 'R' || p->crc != Crc(p))
        return;
    if (p->team != SURVIVAL_Team() || p->from >= SURV_NAME_COUNT)
        return;
    if (p->from == SURVIVAL_TeamName()) {   // another radio of the team uses our name
        static bool told;
        if (!told) {
            told = true;
            SURVIVAL_Message("SAME NAME!", "MENU>NAME");
        }
        return;
    }

    static uint8_t lastFrom = 0xFF, lastSeq;
    if (p->from == lastFrom && p->seq == lastSeq)
        return;   // the same packet again
    lastFrom = p->from;
    lastSeq  = p->seq;

    if (sClockMin == 0xFFFF && p->clock < 1440)
        SURVIVAL_SetClockMin(p->clock);   // a partner knows the time

    Partner_t *pt = PartnerFor(p->from);
    pt->battery   = p->battery;
    pt->at_s      = sUptime_s;
    pt->missed    = 0;

    switch (p->type) {
        case PKT_MSG:
            if (p->arg < ARRAY_SIZE(MESSAGES)) {
                AddLog(p->from, p->arg);
                gBeepToPlay = BEEP_880HZ_60MS_DOUBLE_BEEP;
                BACKLIGHT_TurnOn();
                SURVIVAL_Message(gSurvivalNames[p->from], MESSAGES[p->arg]);
            }
            break;
        case PKT_PING:
            pt->rssi = dbm / 2;
            sPingDbm = (uint8_t)(dbm < 0 ? -dbm : 0);
            if (SURVIVAL_TeamAutoReply()) {   // an answer slot per name (1.2 s apart): answers do not collide
                sPongDelay_10ms = 30 + (SURVIVAL_TeamName() % 12) * 120;
                sPongTries      = 10;
            }
            break;
        case PKT_PONG:
            pt->rssi = -(int8_t)(p->arg / 2);
            AddLog(p->from, 0xFE);
            gUpdateDisplay = true;
            break;
        case PKT_ID:
            sLastTalker   = p->from;
            sLastTalker_s = 15;
            gUpdateDisplay = true;
            break;
    }
}

// FSK interrupts (called from the BK4819 interrupt loop)
void SURVIVAL_TeamIrq(uint16_t bits)
{
    if (!sRxOn)
        return;
    if (bits & BK4819_REG_02_FSK_RX_SYNC) {
        sRxIndex  = 0;
        sRxActive = true;
    }
    if ((bits & BK4819_REG_02_FSK_FIFO_ALMOST_FULL) && sRxActive) {
        const uint16_t count = BK4819_ReadRegister(BK4819_REG_5E) & 7u;
        uint8_t *b = (uint8_t *)&sRx;
        for (uint16_t i = 0; i < count; i++) {
            const uint16_t w = BK4819_ReadRegister(BK4819_REG_5F);
            if (sRxIndex < sizeof(sRx))
                b[sRxIndex++] = w & 0xFF;
            if (sRxIndex < sizeof(sRx))
                b[sRxIndex++] = w >> 8;
        }
    }
    if (bits & BK4819_REG_02_FSK_RX_FINISHED) {
        // words the almost-full interrupts did not fetch (a beep may have held the loop)
        uint8_t *b = (uint8_t *)&sRx;
        while (sRxActive && sRxIndex < sizeof(sRx)) {
            const uint16_t w = BK4819_ReadRegister(BK4819_REG_5F);
            b[sRxIndex++] = w & 0xFF;
            if (sRxIndex < sizeof(sRx))
                b[sRxIndex++] = w >> 8;
        }
        BK4819_WriteRegister(BK4819_REG_59, (1u << 15) | (1u << 14) | REG59_BASE);
        BK4819_WriteRegister(BK4819_REG_59, (1u << 12) | REG59_BASE);
        if (sRxActive && sRxIndex >= sizeof(sRx))
            Handle(&sRx, BK4819_GetRSSI_dBm());
        sRxActive = false;
        sRxIndex  = 0;
    }
}

// ---------------------------------------------------------------- timers
static const uint8_t WATCH_MIN[SURV_WATCH_COUNT] = {0, 10, 30, 60};
const char *const gSurvivalWatchNames[SURV_WATCH_COUNT] = {"OFF", "10 MIN", "30 MIN", "60 MIN"};

static bool Check(void)
{
    if (!SendAlone(PKT_PING, 0))
        return false;
    sCheckAt = sUptime_s;
    return true;
}

// 15 s after a check (all answer slots are over): who did not answer missed one;
// two misses in a row raise the alarm once (Watch only)
static void CountMisses(void)
{
    for (uint8_t i = 0; i < PARTNER_N; i++) {
        Partner_t *p = &sPartner[i];
        if (p->name == 0xFF || p->at_s >= sCheckAt)
            continue;
        if (p->missed < 255)
            p->missed++;
        if (p->missed == 2 && SURVIVAL_TeamWatch()) {
            gBeepToPlay = BEEP_440HZ_500MS;
            BACKLIGHT_TurnOn();
            SURVIVAL_Message("LOST CONTACT", gSurvivalNames[p->name]);
        }
    }
}

void SURVIVAL_TeamTick10ms(void)
{
    if (sPongDelay_10ms && --sPongDelay_10ms == 0 && SURVIVAL_Team()) {
        // busy (receiving, sending, scanning): try again a little later
        if (gScanStateDir != SCAN_OFF || !SendAlone(PKT_PONG, sPingDbm))
            if (sPongTries && --sPongTries)
                sPongDelay_10ms = 60;
    }
}

void SURVIVAL_TeamTick1s(void)
{
    sUptime_s++;
    if (sClockMin != 0xFFFF && ++sClockSec >= 60) {
        sClockSec = 0;
        if (++sClockMin >= 1440)
            sClockMin = 0;
    }
    if (sLastTalker_s && --sLastTalker_s == 0) {
        sLastTalker    = 0xFF;
        gUpdateDisplay = true;
    }

    // key beeps switch the FSK tone off (REG_70): back on while listening
    if (sRxOn && gCurrentFunction != FUNCTION_TRANSMIT)
        BK4819_WriteRegister(BK4819_REG_70, (1u << 7) | (96u << 0));

    if (sCheckAt && sUptime_s >= sCheckAt + 15) {
        CountMisses();
        sCheckAt = 0;
    }

    // Watch: a check every few minutes (not while the listen windows keep the team
    // asleep, not during a scan: then a little later)
    const uint8_t w = SURVIVAL_TeamWatch();
    if (!SURVIVAL_Team() || w == 0 || w >= SURV_WATCH_COUNT) {
        sWatch_s = 0;
        return;
    }
    if (sWatch_s < WATCH_MIN[w] * 60u) {
        sWatch_s++;
        return;
    }
    if (SURVIVAL_ListenWaitMin() || gScanStateDir != SCAN_OFF || !Check())
        return;
    sWatch_s = 0;
}

// name of the radio that talked last (ID packet), NULL after 15 s
const char *SURVIVAL_TeamTalker(void)
{
    return (sLastTalker < SURV_NAME_COUNT) ? gSurvivalNames[sLastTalker] : NULL;
}

// ---------------------------------------------------------------- team screen (F+3)
enum { VIEW_LOG, VIEW_PARTNERS, VIEW_PICK };
static uint8_t sView;
static uint8_t sPick;

void SURVIVAL_TeamOpen(void)
{
    sView = VIEW_LOG;
}

static void Age(char *s, uint32_t at_s)
{
    const uint32_t a = sUptime_s - at_s;
    if (a < 60)
        sprintf(s, "%us", (unsigned)a);
    else if (a < 3600)
        sprintf(s, "%um", (unsigned)(a / 60));
    else
        sprintf(s, "%uh", (unsigned)(a < 99u * 3600 ? a / 3600 : 99));
}

void SURVIVAL_TeamKeys(KEY_Code_t Key)
{
    if (!SURVIVAL_Team()) {
        if (Key == KEY_EXIT)
            SURVIVAL_ToolClose();
        return;
    }
    switch (sView) {
        case VIEW_PICK:
            if (Key == KEY_UP)
                sPick = (sPick + ARRAY_SIZE(MESSAGES) - 1) % ARRAY_SIZE(MESSAGES);
            else if (Key == KEY_DOWN)
                sPick = (sPick + 1) % ARRAY_SIZE(MESSAGES);
            else if (Key == KEY_MENU) {
                if (SendAlone(PKT_MSG, sPick)) {
                    AddLog(SURVIVAL_TeamName(), sPick);
                    gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
                } else {
                    gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
                    SURVIVAL_Message("TEAM: USE A", "PMR CHANNEL");
                }
                sView = VIEW_LOG;
            } else if (Key == KEY_EXIT)
                sView = VIEW_LOG;
            break;
        default:
            if (Key == KEY_MENU)
                sView = VIEW_PICK;
            else if (Key == KEY_STAR) {
                if (!Check()) {
                    gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
                    SURVIVAL_Message("TEAM: USE A", "PMR CHANNEL");
                }
                sView = VIEW_PARTNERS;
            } else if (Key == KEY_UP || Key == KEY_DOWN)
                sView = (sView == VIEW_LOG) ? VIEW_PARTNERS : VIEW_LOG;
            else if (Key == KEY_EXIT)
                SURVIVAL_ToolClose();
            break;
    }
    gUpdateDisplay = true;
}

static void Row(const char *s, uint8_t line, bool bold)
{
    if (bold)
        UI_PrintStringSmallBold(s, 0, 0, line);
    else
        UI_PrintStringSmallNormal(s, 0, 0, line);
}

void SURVIVAL_TeamDisplay(void)
{
    char s[24], a[8];

    if (!SURVIVAL_Team()) {
        Row("NO TEAM CODE", 1, true);
        Row("MENU>TEAM: SAME", 3, false);
        Row("CODE ON ALL", 4, false);
        Row("YOUR RADIOS", 5, false);
        Row("EXIT:BACK", 6, true);
        return;
    }

    if (sClockMin < 1440)
        sprintf(s, "%s %02u:%02u", gSurvivalNames[SURVIVAL_TeamName()], sClockMin / 60, sClockMin % 60);
    else
        sprintf(s, "%s --:--", gSurvivalNames[SURVIVAL_TeamName()]);
    Row(s, 0, true);

    switch (sView) {
        case VIEW_PICK:
            Row("SEND MESSAGE:", 1, false);
            Row(MESSAGES[sPick], 3, true);
            Row(MESSAGES2[sPick], 4, true);
            Row("UP/DN  M:SEND", 6, true);
            return;

        case VIEW_PARTNERS: {
            uint8_t line = 1;
            for (uint8_t i = 0; i < PARTNER_N && line < 6; i++) {
                const Partner_t *p = &sPartner[i];
                if (p->name == 0xFF)
                    continue;
                Age(a, p->at_s);
                sprintf(s, "%-7.7s %4d %3u%%", gSurvivalNames[p->name], p->rssi * 2, p->battery);
                Row(s, line++, p->missed >= 2);
            }
            if (line == 1)
                Row("*: CHECK WHO HEARS", 2, false);
            Row("*:CHECK UP:MSGS", 6, true);
            return;
        }

        default: {
            uint8_t line = 1;
            for (uint8_t i = 0; i < sLogCount && line < 6; i++, line++) {
                const LogEntry_t *e = &sLog[i];
                Age(a, e->at_s);
                const char *what = (e->what == 0xFE) ? "HEARS YOU" : (e->what < ARRAY_SIZE(MESSAGES)) ? MESSAGES[e->what] : "";
                sprintf(s, "%.3s %-11.11s%3s", gSurvivalNames[e->from], what, a);
                Row(s, line, false);
            }
            if (sLogCount == 0)
                Row("NO MESSAGES YET", 2, false);
            Row("M:SEND *:CHECK", 6, true);
            return;
        }
    }
}

void SURVIVAL_TeamInit(void)
{
    for (uint8_t i = 0; i < PARTNER_N; i++)
        sPartner[i].name = 0xFF;
}

#endif
