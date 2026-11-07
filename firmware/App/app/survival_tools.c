/* Survival Radio: tools on the info screen slot
 *
 * F+1 SOS beacon   Morse SOS as a tone on the current channel, 10 s every 2 min,
 *                  flashlight SOS at the same time, off after 30 min or with EXIT
 * F+2 signal meter big RSSI, peak hold, a beep whose pitch follows the signal:
 *                  turn around slowly, the strongest direction is the source
 * F+3 status       battery, hours left, country, SAFE LIMITS, listen windows
 * Listen windows   the receiver sleeps outside short windows (Menu > Listen)
 */

#ifdef ENABLE_SURVIVAL

#include <string.h>

#include "app/action.h"
#include "app/app.h"
#include "app/chFrScanner.h"
#ifdef ENABLE_FMRADIO
    #include "app/fm.h"
#endif
#include "app/flashlight.h"
#include "app/survival.h"
#include "audio.h"
#include "driver/bk4819.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "functions.h"
#include "helper/battery.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/helper.h"
#include "ui/ui.h"

enum { TOOL_NONE, TOOL_BEACON, TOOL_METER, TOOL_STATUS, TOOL_TEAM };

static uint8_t sTool;

// ---------------------------------------------------------------- SOS beacon
#define BEACON_EVERY_S   120
#define BEACON_TOTAL_S   (30 * 60)
#define DIT              12      // 10 ms ticks, about 10 words per minute
#define BEACON_TONE_HZ   800

// "SOS SOS SOS": element lengths in dits, positive = tone, negative = silence
static const int8_t SOS[] = {
    1, -1, 1, -1, 1, -3,   3, -1, 3, -1, 3, -3,   1, -1, 1, -1, 1, -7,
};
#define SOS_REPEAT 3

static bool     sBeaconOn;
static bool     sBeaconTx;
static uint16_t sBeaconLeft_s;    // until the beacon stops
static uint16_t sBeaconNext_s;    // until the next transmission
static uint8_t  sBeaconSent;
static uint8_t  sElem, sRepeat;
static uint16_t sElemTicks;

static void BeaconTxStop(void)
{
    if (!sBeaconTx)
        return;
    sBeaconTx = false;
    AUDIO_AudioPathOff();
    gEnableSpeaker = false;
    RADIO_SendEndOfTransmission();
    FUNCTION_Select(FUNCTION_FOREGROUND);
    RADIO_SetupRegisters(true);
    gUpdateDisplay = true;
}

static void BeaconTxStart(void)
{
    RADIO_PrepareTX();   // the band permission and SAFE LIMITS apply as for PTT
    if (gCurrentFunction != FUNCTION_TRANSMIT) {
        sBeaconOn = false;
        gFlashLightState = FLASHLIGHT_OFF;
        return;
    }
    BK4819_TransmitTone(true, BEACON_TONE_HZ);   // heard in the speaker too
    BK4819_EnterTxMute();
    AUDIO_AudioPathOn();
    gEnableSpeaker = true;
    sBeaconTx  = true;
    sElem      = 0;
    sRepeat    = 0;
    sElemTicks = 0;
    sBeaconSent++;
}

void SURVIVAL_BeaconStop(void)
{
    BeaconTxStop();
    sBeaconOn        = false;
    gFlashLightState = FLASHLIGHT_OFF;
    GPIO_ResetOutputPin(GPIO_PIN_FLASHLIGHT);
}

static void BeaconStart(void)
{
    // the beacon sends on the channel on screen: no scan, no FM moving it
    if (gScanStateDir != SCAN_OFF)
        CHFRSCANNER_Stop();
#ifdef ENABLE_FMRADIO
    if (gFmRadioMode)
        ACTION_FM();   // FM off, back to the channel used before it
#endif
    sBeaconOn        = true;
    sBeaconLeft_s    = BEACON_TOTAL_S;
    sBeaconNext_s    = 0;
    sBeaconSent      = 0;
    gFlashLightState = FLASHLIGHT_SOS;
}

static void BeaconTick10ms(void)
{
    if (!sBeaconTx)
        return;
    if (gCurrentFunction != FUNCTION_TRANSMIT) {   // the TX time limit ended it
        sBeaconTx = false;
        return;
    }
    if (sElemTicks) {
        sElemTicks--;
        return;
    }
    if (sElem == ARRAY_SIZE(SOS)) {
        sElem = 0;
        if (++sRepeat == SOS_REPEAT) {
            BeaconTxStop();
            return;
        }
    }
    const int8_t e = SOS[sElem++];
    if (e > 0)
        BK4819_ExitTxMute();
    else
        BK4819_EnterTxMute();
    sElemTicks = (uint16_t)(e > 0 ? e : -e) * DIT - 1;
}

static void BeaconTick1s(void)
{
    if (!sBeaconOn)
        return;
    if (sBeaconLeft_s == 0 || --sBeaconLeft_s == 0) {
        SURVIVAL_BeaconStop();
        return;
    }
    if (!sBeaconTx && (sBeaconNext_s == 0 || --sBeaconNext_s == 0)) {
        sBeaconNext_s = BEACON_EVERY_S;
        BeaconTxStart();
    }
}

bool SURVIVAL_BeaconActive(void)
{
    return sBeaconOn;
}

// ---------------------------------------------------------------- signal meter
static int16_t sPeak = -160;
static bool    sMeterTone = true;
static uint8_t sMeterTicks;

static void MeterTick10ms(void)
{
    if (++sMeterTicks < 25)
        return;
    sMeterTicks = 0;
    const int16_t dbm = BK4819_GetRSSI_dBm();
    if (dbm > sPeak)
        sPeak = dbm;
    gUpdateDisplay = true;

    // pitch: weak 400, 500, 600 Hz, strong 1 kHz (not while the signal itself is heard)
    if (sMeterTone && gCurrentFunction != FUNCTION_RECEIVE && dbm > -125) {
        static uint8_t every;
        if (++every >= 2) {
            every = 0;
            AUDIO_PlayBeep(dbm > -70 ? BEEP_1KHZ_60MS_OPTIONAL : dbm > -90 ? BEEP_600HZ_30MS :
                           dbm > -110 ? BEEP_500HZ_30MS : BEEP_400HZ_30MS);
        }
    }
}

// ---------------------------------------------------------------- listen windows
// Menu > Listen: OFF, 5 of every 30 min, 5 of 60, 10 of 60. Outside the window the
// receiver sleeps (battery save without wake-ups) while the radio is idle on the main
// screen; a key wakes it, it sleeps again after the usual battery save delay.
static const uint8_t LISTEN_ON[]    = {0, 5, 5, 10};
static const uint8_t LISTEN_EVERY[] = {0, 30, 60, 60};
static uint32_t      sListen_s;       // seconds since the schedule started

const char *const gSurvivalListenNames[SURV_LISTEN_COUNT] = {"OFF", "5/30 MIN", "5/60 MIN", "10/60 MIN"};

void SURVIVAL_ListenRestart(void)
{
    sListen_s = 0;
}

// minutes until the next window, 0 while a window is open (or the schedule is off)
uint8_t SURVIVAL_ListenWaitMin(void)
{
    const uint8_t l = SURVIVAL_ListenWindow();
    if (l == 0 || l >= SURV_LISTEN_COUNT)
        return 0;
    const uint32_t every = LISTEN_EVERY[l] * 60u;
    // with the clock set the windows open on the hour (and half hour), the same on
    // every radio; without it they count from when the schedule was chosen
    const uint16_t clock = SURVIVAL_ClockMin();
    const uint32_t pos   = (clock < 1440) ? (clock * 60u) % every : sListen_s % every;
    if (pos < LISTEN_ON[l] * 60u)
        return 0;
    return (every - pos + 59) / 60;
}

static void ListenTick1s(void)
{
    const uint8_t l = SURVIVAL_ListenWindow();
    if (l == 0 || l >= SURV_LISTEN_COUNT)
        return;
    sListen_s++;
    static uint8_t wasWaiting;
    const uint8_t waiting = SURVIVAL_ListenWaitMin() != 0;
    if (wasWaiting && !waiting)   // a window opens: wake and tell
        gBeepToPlay = BEEP_880HZ_60MS_DOUBLE_BEEP;
    wasWaiting = waiting;

    if (SURVIVAL_ListenWaitMin() == 0 || sBeaconOn)
        return;
    const bool idle = gScreenToDisplay == DISPLAY_MAIN && gScanStateDir == SCAN_OFF &&
#ifdef ENABLE_FMRADIO
                      !gFmRadioMode &&
#endif
                      gBatterySaveCountdown_10ms == 0;
    if (!idle)
        return;
    if (gCurrentFunction == FUNCTION_FOREGROUND)
        FUNCTION_Select(FUNCTION_POWER_SAVE);
    if (gCurrentFunction == FUNCTION_POWER_SAVE && !gRxIdleMode)
        FUNCTION_Select(FUNCTION_POWER_SAVE);   // caught in a wake-up slot: back to sleep
    if (gCurrentFunction == FUNCTION_POWER_SAVE) {
        gPowerSave_10ms            = 200;   // keep the receiver asleep
        gPowerSaveCountdownExpired = false;
    }
}

// ---------------------------------------------------------------- battery reserve
// below 20 %: Eco on and, if none is set, listen windows 5 of every 60 minutes
static bool sReserve;

bool SURVIVAL_Reserve(void)
{
    return sReserve;
}

static void ReserveTick1s(void)
{
    const uint8_t pct = BATTERY_VoltsToPercent(gBatteryVoltageAverage);
    if (sReserve) {
        if (pct > 40)   // charged
            sReserve = false;
        return;
    }
    if (pct == 0 || pct >= 20 || gChargingWithTypeC)
        return;
    sReserve = true;
    if (!SURVIVAL_EcoOn())
        SURVIVAL_SetEco(true);
    if (SURVIVAL_ListenWindow() == 0)
        SURVIVAL_SetListen(2);
    gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP;
    if (gScreenToDisplay == DISPLAY_MAIN)
        SURVIVAL_Message("BATTERY RESERVE", "ECO, LISTEN 5/60");
}

// ---------------------------------------------------------------- battery estimate
static const uint16_t BATTERY_MAH[] = {1600, 2200, 3500, 1500, 2500};

// hours left at a mean current in mA, from the charge in percent
static uint16_t HoursLeft(uint8_t percent, uint16_t mA)
{
    const uint16_t cap = (gEeprom.BATTERY_TYPE < ARRAY_SIZE(BATTERY_MAH)) ? BATTERY_MAH[gEeprom.BATTERY_TYPE] : 1600;
    return (uint32_t)cap * percent / 100u / mA;
}

// ---------------------------------------------------------------- screens
void SURVIVAL_ToolTick10ms(void)
{
    static uint8_t ticks;

    // the user talks: the beacon stops (a locked PTT in a pocket does not count);
    // a press during the beacon's own TX was taken as "already sending": start the user's
    if (sBeaconOn && gPttIsPressed && !(gEeprom.KEY_LOCK && gSetting_set_lck)) {
        const bool wasTx = sBeaconTx;
        SURVIVAL_BeaconStop();
        if (wasTx)
            gFlagPrepareTX = true;
    }
    BeaconTick10ms();
    SURVIVAL_TeamTick10ms();
    if (sTool == TOOL_METER && gScreenToDisplay == DISPLAY_SCANNER)
        MeterTick10ms();

    if (++ticks >= 100) {
        ticks = 0;
        BeaconTick1s();
        SURVIVAL_TeamTick1s();
        ListenTick1s();
        ReserveTick1s();
        if (sTool && gScreenToDisplay == DISPLAY_SCANNER)
            gUpdateDisplay = true;
    }
}

static void Open(uint8_t tool)
{
    if (gScanStateDir != SCAN_OFF && !SURVIVAL_FmBgScanning())
        CHFRSCANNER_Stop();   // the screen switch would end it without its cleanup
    sTool = tool;
    sPeak = -160;
    GUI_SelectNextDisplay(DISPLAY_SCANNER);
}

void SURVIVAL_ShowBeacon(void) { Open(TOOL_BEACON); }
void SURVIVAL_ShowMeter(void)  { Open(TOOL_METER); }
void SURVIVAL_ShowStatus(void) { Open(TOOL_STATUS); }
void SURVIVAL_ShowTeam(void)   { SURVIVAL_TeamOpen(); Open(TOOL_TEAM); }

bool SURVIVAL_ToolActive(void)
{
    return sTool != TOOL_NONE;
}

const char *SURVIVAL_ToolTitle(void)
{
    static const char *const TITLE[] = {"", "SOS BEACON", "SIGNAL METER", "STATUS", "TEAM"};
    return TITLE[sTool];
}

void SURVIVAL_ToolClose(void)
{
    sTool                 = TOOL_NONE;
    gUpdateStatus         = true;
#ifdef ENABLE_FMRADIO
    gRequestDisplayScreen = gFmRadioMode ? DISPLAY_FM : DISPLAY_MAIN;
#else
    gRequestDisplayScreen = DISPLAY_MAIN;
#endif
}

void SURVIVAL_ToolKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    if (bKeyHeld || bKeyPressed)   // act on release
        return;

    if (Key == KEY_EXIT && sTool != TOOL_TEAM) {
        if (sTool == TOOL_BEACON && sBeaconOn) {
            SURVIVAL_BeaconStop();
            gUpdateDisplay = true;
            return;   // a second EXIT leaves
        }
        SURVIVAL_ToolClose();
        return;
    }

    if (sTool == TOOL_TEAM) {   // EXIT is the team screen's own (it leaves sub views first)
        SURVIVAL_TeamKeys(Key);
        return;
    }

    switch (sTool) {
        case TOOL_BEACON:
            if (Key == KEY_MENU && !sBeaconOn)
                BeaconStart();
            break;
        case TOOL_METER:
            if (Key == KEY_MENU)
                sPeak = -160;   // M: reset the peak
            else if (Key == KEY_UP || Key == KEY_DOWN)
                sMeterTone = !sMeterTone;
            break;
        default:
            break;
    }
    gUpdateDisplay = true;
}

static void Row(const char *s, uint8_t line, bool bold)
{
    if (bold)
        UI_PrintStringSmallBold(s, 0, 127, line);
    else
        UI_PrintStringSmallNormal(s, 0, 127, line);
}

static void ChannelLine(char *s)
{
    const uint8_t ch = gEeprom.ScreenChannel[gEeprom.TX_VFO];
    const uint32_t f = gEeprom.VfoInfo[gEeprom.TX_VFO].pTX->Frequency;
    char name[11]    = {0};
    if (IS_MR_CHANNEL(ch))
        SETTINGS_FetchChannelName(name, ch);
    if (name[0])
        sprintf(s, "%s", name);
    else
        sprintf(s, "%u.%05u MHZ", f / 100000, f % 100000);
}

void SURVIVAL_ToolDisplay(void)
{
    char s[24];
    const uint8_t pct = BATTERY_VoltsToPercent(gBatteryVoltageAverage);

    UI_DisplayClear();
    switch (sTool) {
        case TOOL_TEAM:
            SURVIVAL_TeamDisplay();
            break;

        case TOOL_BEACON:
            ChannelLine(s);
            Row(s, 0, true);
            if (!sBeaconOn) {
                Row("MORSE SOS TONE", 1, false);
                Row("10 S EVERY 2 MIN", 2, false);
                Row("+ LIGHT, 30 MIN", 3, false);
                Row("LIFE AT RISK ONLY", 4, false);
                Row("M:START EXIT:BACK", 6, true);
            } else {
                Row(sBeaconTx ? "SENDING SOS" : "WAITING", 2, true);
                sprintf(s, "NEXT %u:%02u  SENT %u", sBeaconNext_s / 60, sBeaconNext_s % 60, sBeaconSent);
                Row(s, 3, false);
                sprintf(s, "OFF IN %u MIN", (sBeaconLeft_s + 59) / 60);
                Row(s, 4, false);
                Row("EXIT:STOP", 6, true);
            }
            break;

        case TOOL_METER: {
            const int16_t dbm = BK4819_GetRSSI_dBm();
            ChannelLine(s);
            Row(s, 0, false);
            sprintf(s, "%d DBM", dbm);
            UI_PrintString(s, 0, 127, 1, 8);
            // bar -130..-40 dBm across the screen, peak as a tick
            const int16_t lo = -130, hi = -40;
            const uint8_t w  = (dbm <= lo) ? 0 : (dbm >= hi) ? 127 : (uint8_t)((dbm - lo) * 127 / (hi - lo));
            const uint8_t pk = (sPeak <= lo) ? 0 : (sPeak >= hi) ? 127 : (uint8_t)((sPeak - lo) * 127 / (hi - lo));
            for (uint8_t x = 0; x < 128; x++)
                gFrameBuffer[3][x] = (x <= w) ? 0x7E : (x == pk || x == pk + 1) ? 0xFF : 0x42;
            sprintf(s, "PEAK %d DBM", sPeak);
            Row(s, 4, false);
            Row(sMeterTone ? "TONE ON  (UP/DN)" : "TONE OFF (UP/DN)", 5, false);
            Row("M:PEAK EXIT:BACK", 6, true);
            break;
        }

        case TOOL_STATUS:
            sprintf(s, "BATTERY %u%% %u.%02uV", pct, gBatteryVoltageAverage / 100, gBatteryVoltageAverage % 100);
            Row(s, 0, true);
            // about 60 mA listening, 25 mA with Eco battery save (1:4), 70 mA with FM
            sprintf(s, "RX ~%uH ECO ~%uH", HoursLeft(pct, 60), HoursLeft(pct, 25));
            Row(s, 1, false);
            sprintf(s, "FM RADIO ~%uH", HoursLeft(pct, 70));
            Row(s, 2, false);
            sprintf(s, "%s", SURVIVAL_CountryName(gSurvivalCountry));
            Row(s, 3, false);
            Row((gSurvivalFlags & SURV_F_NO_LIMITS) ? "LIMITS OFF (SOS)" : sReserve ? "BATTERY RESERVE" : "SAFE LIMITS ON", 4, false);
            if (SURVIVAL_ListenWindow()) {
                const uint8_t w = SURVIVAL_ListenWaitMin();
                if (w)
                    sprintf(s, "LISTEN IN %u MIN", w);
                else
                    sprintf(s, "WINDOW %s", gSurvivalListenNames[SURVIVAL_ListenWindow()]);
            } else
                sprintf(s, "LISTEN: ALWAYS");
            Row(s, 5, false);
            Row("EXIT:BACK", 6, true);
            break;
    }
    ST7565_BlitFullScreen();
}

#endif
