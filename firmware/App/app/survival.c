/* Survival Radio
 *
 * Channel groups are written to memory channels 0.. on first boot (settings
 * record in flash 0x00D000 / 0x012000). The group of a channel is found from its index, so scan and
 * up/down wrap inside the group (see SURVIVAL_FindNextChannel).
 */

#ifdef ENABLE_SURVIVAL

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "app/action.h"
#include "app/chFrScanner.h"
#include "app/generic.h"
#ifdef ENABLE_FMRADIO
    #include "app/fm.h"
#endif
#include "app/survival.h"
#include "driver/backlight.h"
#include "driver/eeprom.h"
    #include "app/survival_country.h"
    #include "ui/menu.h"
    #include "driver/py25q16.h"
#include "external/printf/printf.h"
#include "frequencies.h"
#include "functions.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "audio.h"
#include "driver/bk4819.h"
#include "driver/st7565.h"
#include "ui/helper.h"
#include "ui/inputbox.h"
#include "ui/ui.h"

#define SURV_RANGE_START   14400000   // 144.000 MHz
#define SURV_RANGE_END     14600000   // 146.000 MHz
// bump when the channel tables change: the groups are rewritten on the next boot,
// FAV, the skip list and all settings are kept
#define SURV_TABLE_VERSION 1
#define SURV_TX_DEFAULT    (1u << SURV_TX_PMR)   // FRS is not licence-free in Europe

// ---------------------------------------------------------------- settings record
// All survival settings and the skip list in one CRC checked record, written to two
// flash sectors in turn (outside the EEPROM map, unused by F4HWN): a power cut while
// one is erased or written leaves the other, older record intact.
#define SURV_REC_A   0x00D000
#define SURV_REC_B   0x012000

typedef struct {
    uint8_t  magic[4];      // 'S' 'U' 'R' 'V'
    uint16_t seq;           // the higher (wrapping) sequence number is the current record
    uint8_t  tableVer;
    uint8_t  txMask;
    uint8_t  fmScan;
    uint8_t  country;       // 0xFF: never chosen, asked after the logo
    uint8_t  flags;         // SURV_F_*
    uint8_t  listen;        // listen windows, see SURVIVAL_ListenWindow
    uint32_t customStart;   // 10 Hz units
    uint32_t customEnd;
    uint32_t skip[SURV_SKIP_N];
    uint8_t  defaultsRev;   // new defaults given to radios set up before them
    uint8_t  team;          // team code 1..50 (CTCSS tone on PMR, packet filter), 0/0xFF off
    uint8_t  name;          // team name index, 0xFF: from the radio's calibration
    uint8_t  teamFlags;     // bit 0 clear: no automatic answer to checks
    uint8_t  watch;         // watch interval index
    uint8_t  reserved[1];
    uint16_t crc;
} SurvRecord_t;

static const uint8_t SURV_MAGIC[4] = {'S', 'U', 'R', 'V'};
static SurvRecord_t  sRec;
static bool          sRecInB;   // the current record is in sector B

uint8_t  gSurvivalTxMask      = SURV_TX_DEFAULT;
uint8_t  gSurvivalFmScan      = SURV_FMSCAN_LAST;
uint8_t  gSurvivalFlags       = 0;

static bool sFmBgScan;       // BK4819 scans a group while the FM radio plays
static bool sStopOnRxEnd;    // a background hit ends the scan when that transmission ends
static bool sScanBeforeFm;   // a scan was running when FM was switched on
static uint8_t sChBeforeFm = 0xFF;   // channel the user was on before the FM background scan moved it
uint32_t gSurvivalCustomStart = SURV_RANGE_START;
uint32_t gSurvivalCustomEnd   = SURV_RANGE_END;
uint8_t  gSurvivalPrio        = 0xFF;

static uint16_t Crc16(const void *data, uint16_t size)
{
    const uint8_t *p   = data;
    uint16_t       crc = 0xFFFF;
    while (size--) {
        crc ^= (uint16_t)*p++ << 8;
        for (uint8_t i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
}

static bool ReadRecord(uint32_t addr, SurvRecord_t *rec)
{
    PY25Q16_ReadBuffer(addr, rec, sizeof(*rec));
    return memcmp(rec->magic, SURV_MAGIC, sizeof(SURV_MAGIC)) == 0 &&
           rec->crc == Crc16(rec, offsetof(SurvRecord_t, crc));
}

// the newer valid record of the two sectors into sRec, false when there is none
static bool LoadRecord(void)
{
    SurvRecord_t b;
    const bool okA = ReadRecord(SURV_REC_A, &sRec);
    const bool okB = ReadRecord(SURV_REC_B, &b);
    sRecInB = okB && (!okA || (int16_t)(b.seq - sRec.seq) > 0);
    if (sRecInB)
        sRec = b;
    return okA || okB;
}

static void SaveRecord(void)
{
    const uint32_t addr = sRecInB ? SURV_REC_A : SURV_REC_B;
    memcpy(sRec.magic, SURV_MAGIC, sizeof(SURV_MAGIC));
    sRec.seq++;
    sRec.txMask      = gSurvivalTxMask;
    sRec.fmScan      = gSurvivalFmScan;
    sRec.flags       = gSurvivalFlags;
    sRec.customStart = gSurvivalCustomStart;
    sRec.customEnd   = gSurvivalCustomEnd;
    sRec.crc         = Crc16(&sRec, offsetof(SurvRecord_t, crc));
    PY25Q16_SectorErase(addr);
    PY25Q16_WriteBuffer(addr, &sRec, sizeof(sRec), false);
    sRecInB = !sRecInB;
}

// ---------------------------------------------------------------- skip list
// Frequencies the scans pass over (noise, carriers), kept at the start of the list,
// stored in the settings record
#define sSkip sRec.skip   // 10 Hz units
static uint8_t sSkipCount;

static void LoadSkip(void)
{
    sSkipCount = 0;
    while (sSkipCount < SURV_SKIP_N && sSkip[sSkipCount] != 0 && sSkip[sSkipCount] != 0xFFFFFFFF)
        sSkipCount++;
    memset(&sSkip[sSkipCount], 0, (SURV_SKIP_N - sSkipCount) * sizeof(sSkip[0]));
}

#define SaveSkip SaveRecord

bool SURVIVAL_IsSkipped(uint32_t frequency)
{
    for (uint8_t i = 0; i < sSkipCount; i++)
        if (sSkip[i] == frequency)
            return true;
    return false;
}

static void RemoveSkip(uint8_t index)
{
    memmove(&sSkip[index], &sSkip[index + 1], (SURV_SKIP_N - 1 - index) * sizeof(sSkip[0]));
    sSkip[SURV_SKIP_N - 1] = 0;
    sSkipCount--;
    SaveSkip();
}

const char gSubMenu_SRV_MODE[SURV_MODE_COUNT][9] = {
    "PMR446", "LOCAL", "MARINE", "AIR", "HAM", "CB", "FM RADIO", "CUSTOM", "FAV",
};

#define SURV_AM     (1u << 0)
#define SURV_NARROW (1u << 1)
#define SURV_HIGH   (1u << 2)   // full power, otherwise LOW4 (~0.5 W)

typedef struct {
    uint32_t lower;
    uint32_t upper;
    uint8_t  bit;
} TxBand_t;

static const TxBand_t TX_BANDS[] = {
    {44600625, 44619375, SURV_TX_PMR},
    {46255000, 46272500, SURV_TX_FRS},
    {46755000, 46772500, SURV_TX_FRS},
    {15602500, 15742500, SURV_TX_MARINE},   // ship side only, coast frequencies are RX
    {14400000, 14600000, SURV_TX_HAM},  // 2 m, ITU region 1 (Spain)
    {43000000, 44000000, SURV_TX_HAM},  // 70 cm, ITU region 1 (Spain)
    { 2656500,  2799125, SURV_TX_CB},   // 40 ch CEPT/FCC plus national CB plans
};

// never transmit here, not even with TxAll
static const TxBand_t TX_BLOCKED[] = {
    { 8800000, 13700000, 0},   // FM broadcast + civil aviation (VOR/ILS, 121.5 guard)
    {22500000, 40000000, 0},   // military aviation (243.0 guard)
    {40600000, 40610000, 0},   // COSPAS-SARSAT distress beacons
    {15651250, 15653750, 0},   // marine channel 70: DSC data only, no voice
    {15676250, 15683750, 0},   // marine channels 75/76: guard band around channel 16
    {16196250, 16203750, 0},   // AIS 1 and 2 (161.975 / 162.025 MHz)
    {121500000, 130000000, 0}, // GNSS L2 / E6 (GPS, Galileo)
};

#define PMR_LOWER 44600000     // PMR446 band edges: power cap and narrow FM under SAFE LIMITS
#define PMR_UPPER 44620000
#define CB_LOWER   2656500     // 27 MHz: harmonics land in the air and marine bands
#define CB_UPPER   2800000

typedef struct {
    const char     *prefix;  // name = prefix + channel number
    const uint32_t *freqs;   // 10 Hz units, NULL: base + i * step
    uint32_t        base;
    uint16_t        step;
    uint8_t         count;
    uint8_t         flags;
    const uint8_t  *nums;    // channel numbers, NULL: 1..count, 200+n prints "Mn"
    const char     *names;   // NUL separated names, overrides prefix/nums
} SurvivalGroup_t;

static const uint32_t FRS_FREQS[] = {
    46256250, 46258750, 46261250, 46263750, 46266250, 46268750, 46271250,
    46756250, 46758750, 46761250, 46763750, 46766250, 46768750, 46771250,
    46255000, 46257500, 46260000, 46262500, 46265000, 46267500, 46270000, 46272500,
};

// distress/calling first, then the common simplex ship channels, then the
// duplex channels on the coast station frequency (coastguard MSI, ports, VTS);
// duplex channels transmit 4.6 MHz lower on the ship frequency.
// NOAA weather WX1..WX7 removed: US/Canada only, silent in Spain.
static const uint8_t MARINE_NUMS[] = {
    16, 6, 67, 8, 9, 10, 11, 12, 13, 14, 15, 17, 68, 69, 71, 72, 73, 74, 77,
     1,  2,  3,  4,  5,  7, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28,
    60, 61, 62, 63, 64, 65, 66, 78, 79, 80, 81, 82, 83, 84, 85, 86,
};
static const uint32_t MARINE_FREQS[] = {
    15680000, 15630000, 15637500, 15640000, 15645000, 15650000, 15655000,
    15660000, 15665000, 15670000, 15675000, 15685000, 15642500, 15647500,
    15657500, 15662500, 15667500, 15672500, 15687500,
    16065000, 16070000, 16075000, 16080000, 16085000, 16095000, 16150000,
    16155000, 16160000, 16165000, 16170000, 16175000, 16180000, 16185000,
    16190000, 16195000, 16200000, 16062500, 16067500, 16072500, 16077500,
    16082500, 16087500, 16092500, 16152500, 16157500, 16162500, 16167500,
    16172500, 16177500, 16182500, 16187500, 16192500,
};
#define MARINE_DUPLEX_OFFSET 460000   // 4.6 MHz

// AIR is not a channel group but the whole air band on its own VFO slot
// (108-137 MHz band memory): AM, 25 kHz steps, scanned 118.000-136.975 MHz
#define AIR_VFO_CHANNEL (FREQ_CHANNEL_FIRST + BAND2_108MHz)
#define AIR_SCAN_START  11800000
#define AIR_SCAN_END    13697500
#define AIR_START_FREQ  12150000   // 121.500 guard

// the air band slot is AIR, unless CUSTOM was put there (its range is in 108-137 MHz)
static bool IsAirSlot(uint8_t channel)
{
    return channel == AIR_VFO_CHANNEL && !(gSurvivalFlags & SURV_F_CUSTOM);
}

static void SetCustomFlag(bool on)
{
    if (!(gSurvivalFlags & SURV_F_CUSTOM) == !on)
        return;
    gSurvivalFlags ^= SURV_F_CUSTOM;
    SURVIVAL_SaveSettings();
}

// FM calling channels for ITU region 1 (Europe/Africa/Middle East) and 2 (Americas)
static const uint32_t HAM_FREQS[] = {
    14550000, 14652000, 43350000, 44600000, 14580000,
};
static const char HAM_NAMES[] = "2M CALL EU\0" "2M CALL US\0" "70 CALL EU\0" "70 CALL US\0ISS";

// CEPT 40 channel plan, channel 9 = emergency
static const uint32_t CB_FREQS[] = {
    2696500, 2697500, 2698500, 2700500, 2701500, 2702500, 2703500, 2705500,
    2706500, 2707500, 2708500, 2710500, 2711500, 2712500, 2713500, 2715500,
    2716500, 2717500, 2718500, 2720500, 2721500, 2722500, 2725500, 2723500,
    2724500, 2726500, 2727500, 2728500, 2729500, 2730500, 2731500, 2732500,
    2733500, 2734500, 2735500, 2736500, 2737500, 2738500, 2739500, 2740500,
};

static const SurvivalGroup_t GROUPS[] = {
    {"PMR ", NULL,         44600625, 1250, 16,                      SURV_NARROW,           NULL,        NULL},
    {"FRS ", FRS_FREQS,    0,        0,    ARRAY_SIZE(FRS_FREQS),    0,                     NULL,        NULL},
    {"MAR ", MARINE_FREQS, 0,        0,    ARRAY_SIZE(MARINE_FREQS), SURV_HIGH,             MARINE_NUMS, NULL},
    {"",     NULL,         0,        0,    0,                        SURV_AM,               NULL,        NULL},   // AIR: band mode, no channels
    {"",     HAM_FREQS,    0,        0,    ARRAY_SIZE(HAM_FREQS),    SURV_HIGH,             NULL,        HAM_NAMES},
    {"CB ",  CB_FREQS,     0,        0,    ARRAY_SIZE(CB_FREQS),     SURV_NARROW|SURV_HIGH, NULL,        NULL},
};

static_assert(ARRAY_SIZE(MARINE_NUMS) == ARRAY_SIZE(MARINE_FREQS), "");
static_assert(ARRAY_SIZE(GROUPS) == SURV_MODE_FM, "");

#define GROUP_NONE 0xFF

// ---------------------------------------------------------------- country presets
// The country sets the LOCAL group (in place of FRS) and the CB plan. FAV starts at a
// fixed channel, so a country change rewrites the groups below it and keeps FAV.
#define SURV_FAV_FIRST   170
#define SURV_FAV_STAGE   0x013000   // own sector (unused by F4HWN V3): FAV parked during a country change

uint8_t gSurvivalCountry = SURV_COUNTRY_GENERIC;
static bool sAskCountry;     // first boot: the country menu opens after the logo

static const SurvivalCountry_t *Country(void)
{
    return &gSurvivalCountries[gSurvivalCountry];
}

static uint8_t GroupCount(uint8_t g)
{
    if (g == SURV_MODE_FRS)
        return Country()->localCount;
    if (g == SURV_MODE_CB)
        return (Country()->cb == SURV_CB_DE80 || Country()->cb == SURV_CB_UK) ? 80 : 40;
    return GROUPS[g].count;
}

static void LoadCountry(void)
{
    const uint8_t c  = sRec.country;
    sAskCountry      = c >= gSurvivalCountryCount;   // never chosen: ask after the logo
    gSurvivalCountry = sAskCountry ? SURV_COUNTRY_GENERIC : c;
}

const char *SURVIVAL_CountryName(uint8_t c)
{
    return (c < gSurvivalCountryCount) ? gSurvivalCountries[c].name : "";
}

uint8_t SURVIVAL_CountryCount(void)
{
    return gSurvivalCountryCount;
}

// emergency numbers and local channels of the selected country, manual style rows
const char *SURVIVAL_CountryRows(void)
{
    return Country()->numbers;
}

static void ShowMessage(const char *line1, const char *line2);   // feedback screen, see below

static uint8_t GroupStart(uint8_t g)
{
    uint8_t start = 0;
    for (uint8_t i = 0; i < g; i++)
        start += GroupCount(i);
    return start;
}

static uint8_t FindGroup(uint8_t channel)
{
    uint8_t start = 0;
    for (uint8_t g = 0; g < ARRAY_SIZE(GROUPS); g++) {
        start += GroupCount(g);
        if (channel < start)
            return g;
    }
    return GROUP_NONE;
}

// own channels (FAV) follow the fixed groups
static uint8_t FavFirst(void)
{
    return SURV_FAV_FIRST;
}

static uint8_t FirstFav(void)
{
    for (uint8_t c = FavFirst(); IS_MR_CHANNEL(c); c++)
        if (RADIO_CheckValidChannel(c, false, 0))
            return c;
    return 0xFF;
}

bool SURVIVAL_IsFavChannel(uint8_t channel)
{
    return IS_MR_CHANNEL(channel) && channel >= FavFirst();
}

bool SURVIVAL_GetGroupRange(uint8_t channel, uint8_t *first, uint8_t *last)
{
    const uint8_t g = FindGroup(channel);
    if (g == GROUP_NONE) {
        if (!IS_MR_CHANNEL(channel))
            return false;
        *first = FavFirst();   // FAV
        *last  = MR_CHANNEL_LAST;
        return true;
    }
    *first = GroupStart(g);
    *last  = *first + GroupCount(g) - 1;
    return true;
}

uint8_t SURVIVAL_FindNextChannel(uint8_t Channel, int8_t Direction, bool bCheckScanList, uint8_t VFO)
{
    // stay inside the group of the channel we come from
    uint8_t first, last;
    if (!SURVIVAL_GetGroupRange(Channel - Direction, &first, &last))
        return RADIO_FindNextChannel(Channel, Direction, bCheckScanList, VFO);

    for (unsigned int i = 0; i <= (unsigned int)(last - first); i++, Channel += Direction) {
        if (Channel < first || Channel > last)
            Channel = (Direction > 0) ? first : last;

        if (bCheckScanList && gMR_ChannelExclude[Channel])
            continue;

        if (RADIO_CheckValidChannel(Channel, false, VFO))
            return Channel;
    }
    return 0xFF;
}

static void ChannelName(const SurvivalGroup_t *grp, uint8_t i, char *name)
{
    if (grp->names) {
        const char *s = grp->names;
        while (i--)
            s += strlen(s) + 1;
        strcpy(name, s);
        return;
    }

    const uint8_t num = grp->nums ? grp->nums[i] : i + 1;
    if (num > 200)
        sprintf(name, "%sWX%u", grp->prefix, num - 200);
    else
        sprintf(name, "%s%u", grp->prefix, num);
}

// copy the FAV channels and their names (flash 0x000000 + 16 * ch, 0x00e000 + 16 * ch)
// to the staging area and back, 16 bytes at a time
static void MoveFav(bool park)
{
    uint8_t buf[16];
    for (uint8_t c = SURV_FAV_FIRST; IS_MR_CHANNEL(c); c++) {
        for (uint8_t k = 0; k < 2; k++) {
            const uint32_t live  = (k ? 0x00e000 : 0x000000) + c * 16u;
            const uint32_t stage = SURV_FAV_STAGE + ((c - SURV_FAV_FIRST) * 2u + k) * 16u;
            PY25Q16_ReadBuffer(park ? live : stage, buf, sizeof(buf));
            PY25Q16_WriteBuffer(park ? stage : live, buf, sizeof(buf), false);
        }
    }
}

// keepFav: rewrite the groups only. FAV is parked in the stage sector and the
// record marks the rewrite before the channel sectors are erased: after a power cut
// the next start finishes it from the parked copy (staged = FAV already parked).
static void WriteChannels(bool keepFav, bool staged)
{
    VFO_Info_t *pVfo = &gEeprom.VfoInfo[0];   // reloaded by RADIO_ConfigureChannel afterwards
    uint8_t     ch   = 0;
    char        name[16];

    // Every channel and name is rewritten below. Blank the two flash sectors that
    // hold only them (EEPROM 0x0000-0x0C7F and 0x0F50-0x1BCF), so each write
    // programs erased flash instead of erasing a 4 KB sector per channel.
    // A country change parks FAV first and puts it back afterwards.
    if (keepFav && !staged) {
        PY25Q16_SectorErase(SURV_FAV_STAGE);
        MoveFav(true);
        gSurvivalFlags |= SURV_F_REWRITE;
        SaveRecord();
    }
    PY25Q16_SectorErase(0x000000);
    PY25Q16_SectorErase(0x00e000);
    gSettingsBatchAttr = true;   // the attribute table is written once at the end

    for (uint8_t g = 0; g < ARRAY_SIZE(GROUPS); g++) {
        const SurvivalGroup_t *grp = &GROUPS[g];

        for (uint8_t i = 0; i < GroupCount(g) && ch < FavFirst(); i++, ch++) {   // never into FAV
            if (g == SURV_MODE_FRS) {   // LOCAL: the country's channels
                const SurvivalLocal_t *loc = &Country()->local[i];
                RADIO_InitInfo(pVfo, ch, loc->freq);
                pVfo->Modulation        = (loc->flags & SURV_LOCAL_AM)   ? MODULATION_AM  : MODULATION_FM;
                pVfo->CHANNEL_BANDWIDTH = (loc->flags & SURV_LOCAL_WIDE) ? BANDWIDTH_WIDE : BANDWIDTH_NARROW;
                pVfo->OUTPUT_POWER      = OUTPUT_POWER_LOW4;
                if (loc->tone) {   // sent so the listeners' tone squelch opens; RX stays open
                    pVfo->freq_config_TX.CodeType = CODE_TYPE_CONTINUOUS_TONE;
                    pVfo->freq_config_TX.Code     = loc->tone - 1;
                }
                SETTINGS_SaveChannel(ch, 0, pVfo, 2);
                strcpy(name, loc->name);
                SETTINGS_SaveChannelName(ch, name);
                continue;
            }
            uint32_t freq = grp->freqs ? grp->freqs[i] : grp->base + (uint32_t)i * grp->step;
            uint8_t  flags = grp->flags;
            if (g == SURV_MODE_CB) {
                const uint8_t plan = Country()->cb;
                if (i >= 40)   // Germany 41-80 below CEPT, UK 27/81 above
                    freq = (plan == SURV_CB_DE80 ? 2656500 : 2760125) + (uint32_t)(i - 40) * 1000;
                else if (plan == SURV_CB_PL) {
                    freq -= 500;   // Polish channels sit 5 kHz lower, mostly AM
                    flags |= SURV_AM;
                }
            }

            RADIO_InitInfo(pVfo, ch, freq);
            pVfo->Modulation        = (flags & SURV_AM)     ? MODULATION_AM     : MODULATION_FM;
            pVfo->CHANNEL_BANDWIDTH = (flags & SURV_NARROW) ? BANDWIDTH_NARROW  : BANDWIDTH_WIDE;
            pVfo->OUTPUT_POWER      = (flags & SURV_HIGH)   ? OUTPUT_POWER_HIGH : OUTPUT_POWER_LOW4;
            if (grp->freqs == MARINE_FREQS && grp->nums[i] < 200 && freq >= 16000000) {
                pVfo->TX_OFFSET_FREQUENCY           = MARINE_DUPLEX_OFFSET;
                pVfo->TX_OFFSET_FREQUENCY_DIRECTION = TX_OFFSET_FREQUENCY_DIRECTION_SUB;
            }
            SETTINGS_SaveChannel(ch, 0, pVfo, 2);

            if (g == SURV_MODE_CB && i >= 40)
                sprintf(name, Country()->cb == SURV_CB_UK ? "UK %u" : "CB %u", Country()->cb == SURV_CB_UK ? i - 39 : i + 1);
            else
            ChannelName(grp, i, name);
            SETTINGS_SaveChannelName(ch, name);
        }
    }

    // FAV starts empty: clear whatever the stock firmware left there
    // (on V3 also the unused channels between the groups and FAV)
    for (; IS_MR_CHANNEL(ch) && !(keepFav && ch >= FavFirst()); ch++)
        SETTINGS_UpdateChannel(ch, pVfo, false, false, true);

    {   // attribute table (flash 0x002000, one byte per channel) in one write
        uint8_t buf[224];
        PY25Q16_ReadBuffer(0x002000, buf, sizeof(buf));
        for (uint8_t c = 0; IS_MR_CHANNEL(c); c++)
            buf[c] = gMR_ChannelAttributes[c].__val;
        PY25Q16_WriteBuffer(0x002000, buf, sizeof(buf), true);
        gSettingsBatchAttr = false;
    }

    if (keepFav) {
        MoveFav(false);
        gSurvivalFlags &= ~SURV_F_REWRITE;
        SaveRecord();
    }
}

// FM broadcast 87.5-108 MHz (an erased EEPROM reads as band 3 = 64-76 MHz)
static void FmDefaults(void)
{
#ifdef ENABLE_FMRADIO
    gEeprom.FM_Band              = 0;
    gEeprom.FM_SelectedFrequency = 875;
    gEeprom.FM_FrequencyPlaying  = 875;
    gEeprom.FM_IsMrMode          = false;
    SETTINGS_SaveFM();
#endif
}

static void WriteDefaults(void)
{
    gEeprom.KEY_1_SHORT_PRESS_ACTION = ACTION_OPT_FLASHLIGHT;   // light, blink, SOS, off
    gEeprom.KEY_1_LONG_PRESS_ACTION  = ACTION_OPT_MONITOR;      // hold: monitor on / off
    gEeprom.KEY_2_SHORT_PRESS_ACTION = ACTION_OPT_FM;      // FM radio on/off, M switches modes
    gEeprom.KEY_2_LONG_PRESS_ACTION  = ACTION_OPT_SCAN;
    gEeprom.SCAN_RESUME_MODE         = 0;   // STOP: stay on the channel with activity
    gEeprom.SCAN_LIST_DEFAULT        = 5;   // all channels, the group limits the scan
    gEeprom.SQUELCH_LEVEL            = 1;   // most sensitive; raise it if noise stops the scan
    gEeprom.POWER_ON_DISPLAY_MODE    = POWER_ON_DISPLAY_MODE_ALL;
    gEeprom.CHANNEL_DISPLAY_MODE     = MDF_NAME_FREQ;
    gEeprom.DUAL_WATCH               = DUAL_WATCH_OFF;
    gEeprom.CROSS_BAND_RX_TX         = CROSS_BAND_OFF;
    gSetting_set_inv                 = false;   // normal (not negative) display
    gSetting_set_ctr                 = 10;      // F4HWN default contrast, blank EEPROM reads as 15
    gSetting_set_lck                 = true;    // key lock locks PTT too: no pocket calls
    gEeprom.ROGER                    = ROGER_MODE_ROGER;   // a beep tells the other side it is their turn

    gEeprom.TX_VFO = 0;
    for (uint8_t vfo = 0; vfo < 2; vfo++) {
        gEeprom.MrChannel[vfo]     = 0;
        gEeprom.ScreenChannel[vfo] = 0;

        VFO_Info_t *pVfo = &gEeprom.VfoInfo[vfo];
        RADIO_InitInfo(pVfo, AIR_VFO_CHANNEL, AIR_START_FREQ);   // AM for this band
        pVfo->STEP_SETTING  = STEP_25kHz;
        pVfo->StepFrequency = gStepFrequencyTable[STEP_25kHz];
        SETTINGS_SaveChannel(AIR_VFO_CHANNEL, vfo, pVfo, 2);
    }

    SETTINGS_SaveSettings();
    SETTINGS_SaveVfoIndices();
    FmDefaults();
}

void SURVIVAL_SaveSettings(void)
{
    SaveRecord();
}

// CUSTOM range: inside the receiver bands, start below end, at least 100 kHz wide
void SURVIVAL_SaveCustomRange(void)
{
    uint32_t lo = gSurvivalCustomStart, hi = gSurvivalCustomEnd;
    if (lo > hi) {
        const uint32_t tmp = lo;
        lo = hi;
        hi = tmp;
    }
    if (lo < BX4819_band1.lower)
        lo = BX4819_band1.lower;
    if (hi > BX4819_band2.upper)
        hi = BX4819_band2.upper;
    if (lo > BX4819_band1.upper && lo < BX4819_band2.lower)   // the chip's gap
        lo = BX4819_band2.lower;
    if (hi > BX4819_band1.upper && hi < BX4819_band2.lower)
        hi = BX4819_band1.upper;
    if (hi < lo + 10000) {
        lo = SURV_RANGE_START;
        hi = SURV_RANGE_END;
    }
    gSurvivalCustomStart = lo;
    gSurvivalCustomEnd   = hi;
    SaveRecord();
}

// first start: a copy of the factory calibration (512 bytes at 0x010000, unique to the
// radio) in a spare sector, 0x014000, kept as long as it is there
#define SURV_CAL_COPY 0x014000

static void BackupCalibration(void)
{
    uint8_t buf[64];
    PY25Q16_ReadBuffer(SURV_CAL_COPY, buf, 4);
    if (buf[0] != 0xFF || buf[1] != 0xFF || buf[2] != 0xFF || buf[3] != 0xFF)
        return;   // already copied
    PY25Q16_SectorErase(SURV_CAL_COPY);
    for (uint16_t ofs = 0; ofs < 512; ofs += sizeof(buf)) {
        PY25Q16_ReadBuffer(0x010000 + ofs, buf, sizeof(buf));
        PY25Q16_WriteBuffer(SURV_CAL_COPY + ofs, buf, sizeof(buf), false);
    }
}

void SURVIVAL_Init(void)
{
    const bool known = LoadRecord();

    BackupCalibration();
    SURVIVAL_TeamInit();

    // the priority channel is marked with the (otherwise unused) scan list 1 bit
    for (uint8_t c = 0; IS_MR_CHANNEL(c); c++) {
        if (gMR_ChannelAttributes[c].scanlist1 && RADIO_CheckValidChannel(c, false, 0)) {
            gSurvivalPrio = c;
            break;
        }
    }

    if (known) {
        LoadCountry();   // before anything looks at the group layout
        LoadSkip();
        gSurvivalTxMask = sRec.txMask;
        gSurvivalFlags  = sRec.flags;
        if (sRec.fmScan <= SURV_FMSCAN_LAST)
            gSurvivalFmScan = sRec.fmScan;
        gSurvivalCustomStart = sRec.customStart;
        gSurvivalCustomEnd   = sRec.customEnd;
        if (sRec.defaultsRev == 0xFF) {   // rev 1: roger beep on
            gEeprom.ROGER    = ROGER_MODE_ROGER;
            SETTINGS_SaveSettings();
            sRec.defaultsRev = 1;
            SaveRecord();
        }
        if (gSurvivalFlags & SURV_F_REWRITE) {   // a power cut stopped a rewrite: finish it
            WriteChannels(true, true);
            if (gSurvivalPrio < FavFirst())
                gSurvivalPrio = 0xFF;
        }
        if (sRec.tableVer != SURV_TABLE_VERSION) {   // new channel tables: groups only
            sRec.tableVer = SURV_TABLE_VERSION;      // saved with the rewrite mark
            WriteChannels(true, false);
            if (gSurvivalPrio < FavFirst())
                gSurvivalPrio = 0xFF;
        }
        return;
    }

    // first start (or after a reset): everything from scratch
    memset(&sRec, 0xFF, sizeof(sRec));
    sRec.seq      = 0;
    sRec.tableVer = SURV_TABLE_VERSION;
    sRec.listen   = 0;
    LoadCountry();
    WriteChannels(false, false);
    WriteDefaults();
    sSkipCount = 0;
    memset(sSkip, 0, sizeof(sSkip));
    gSurvivalPrio        = 0xFF;   // the channels were rewritten without a priority mark
    gSurvivalTxMask      = SURV_TX_DEFAULT;
    gSurvivalFlags       = 0;
    gSurvivalCustomStart = SURV_RANGE_START;
    gSurvivalCustomEnd   = SURV_RANGE_END;
    memset(sRec.reserved, 0xFF, sizeof(sRec.reserved));
    sRec.defaultsRev = 1;
    SaveRecord();
}

void SURVIVAL_ApplyScanRange(void)
{
    sStopOnRxEnd = false;   // every scan start clears a pending background stop

    // VFO modes scan a range with the VFO step: AIR the air band, CUSTOM the user range
    const uint8_t channel = gRxVfo->CHANNEL_SAVE;
    if (IsAirSlot(channel)) {
        gScanRangeStart = AIR_SCAN_START;
        gScanRangeStop  = AIR_SCAN_END;
    } else if (!IS_MR_CHANNEL(channel)) {
        gScanRangeStart = gSurvivalCustomStart;
        gScanRangeStop  = gSurvivalCustomEnd;
    }
}

bool SURVIVAL_TxAllowed(uint32_t frequency)
{
    // the blocked ranges apply in every case, before any permission
    for (uint8_t i = 0; i < ARRAY_SIZE(TX_BLOCKED); i++) {
        if (frequency >= TX_BLOCKED[i].lower && frequency < TX_BLOCKED[i].upper)
            return false;
    }
    // SAFE LIMITS: CB is listen only (the VHF filter does not stop its harmonics)
    if (!(gSurvivalFlags & SURV_F_NO_LIMITS) && frequency >= CB_LOWER && frequency < CB_UPPER)
        return false;
    if ((gSurvivalTxMask >> SURV_TX_ALL) & 1u)
        return true;

    // bands may overlap (70 cm contains PMR446), any permitted band is enough
    for (uint8_t i = 0; i < ARRAY_SIZE(TX_BANDS); i++) {
        if (frequency >= TX_BANDS[i].lower && frequency <= TX_BANDS[i].upper &&
            ((gSurvivalTxMask >> TX_BANDS[i].bit) & 1u))
            return true;
    }
    return false;
}

// SAFE LIMITS: PMR446 is sent at LOW3 (about 0.25 W) and narrow, whatever the channel says
bool SURVIVAL_TxCapped(uint32_t frequency)
{
    return !(gSurvivalFlags & SURV_F_NO_LIMITS) && frequency >= PMR_LOWER && frequency < PMR_UPPER;
}

// ---------------------------------------------------------------- team settings
uint8_t SURVIVAL_Team(void)
{
    return (sRec.team >= 1 && sRec.team <= SURV_TEAM_MAX) ? sRec.team : 0;
}

void SURVIVAL_SetTeam(uint8_t t)
{
    sRec.team = (t <= SURV_TEAM_MAX) ? t : 0;
    SaveRecord();
    gFlagReconfigureVfos = true;   // the PMR channels take the tone, the FSK receiver goes on
}

uint8_t SURVIVAL_TeamName(void)
{
    if (sRec.name < SURV_NAME_COUNT)
        return sRec.name;
    // not chosen yet: a letter from the radio's own calibration, so two radios differ
    uint8_t  buf[32];
    uint16_t h = 0;
    for (uint16_t ofs = 0; ofs < 512; ofs += sizeof(buf)) {
        PY25Q16_ReadBuffer(0x010000 + ofs, buf, sizeof(buf));
        for (uint8_t i = 0; i < sizeof(buf); i++)
            h = (uint16_t)(h * 31u + buf[i]);
    }
    sRec.name = h % 26;   // ALPHA..ZULU
    SaveRecord();
    return sRec.name;
}

void SURVIVAL_SetTeamName(uint8_t n)
{
    sRec.name = (n < SURV_NAME_COUNT) ? n : 0;
    SaveRecord();
}

bool SURVIVAL_TeamAutoReply(void)
{
    return sRec.teamFlags & 1u;   // 0xFF (never set): on
}

void SURVIVAL_SetTeamAutoReply(bool on)
{
    sRec.teamFlags = on ? (sRec.teamFlags | 1u) : (sRec.teamFlags & ~1u);
    SaveRecord();
}

uint8_t SURVIVAL_TeamWatch(void)
{
    return (sRec.watch < SURV_WATCH_COUNT) ? sRec.watch : 0;
}

void SURVIVAL_SetTeamWatch(uint8_t w)
{
    sRec.watch = (w < SURV_WATCH_COUNT) ? w : 0;
    SaveRecord();
}

// PMR channels carry the team tone (CTCSS) on receive and send: other users of the
// channel stay silent, team radios hear each other
void SURVIVAL_TeamApply(VFO_Info_t *pVfo)
{
    const uint8_t t  = SURVIVAL_Team();
    const uint8_t ch = pVfo->CHANNEL_SAVE;
    if (!t || !IS_MR_CHANNEL(ch) || FindGroup(ch) != SURV_MODE_PMR)
        return;
    pVfo->freq_config_RX.CodeType = CODE_TYPE_CONTINUOUS_TONE;
    pVfo->freq_config_RX.Code     = t - 1;
    pVfo->freq_config_TX.CodeType = CODE_TYPE_CONTINUOUS_TONE;
    pVfo->freq_config_TX.Code     = t - 1;
}

uint8_t SURVIVAL_ListenWindow(void)
{
    return (sRec.listen < SURV_LISTEN_COUNT) ? sRec.listen : 0;
}

void SURVIVAL_SetListen(uint8_t l)
{
    sRec.listen = (l < SURV_LISTEN_COUNT) ? l : 0;
    SURVIVAL_ListenRestart();   // the first window starts now
    SaveRecord();
}

// simple mode: M steps PMR, LOCAL, FM and FAV only, key lock also locks PTT
void SURVIVAL_SetSimple(bool on)
{
    gSurvivalFlags = on ? (gSurvivalFlags | SURV_F_SIMPLE) : (gSurvivalFlags & ~SURV_F_SIMPLE);
    if (on && !gSetting_set_lck) {
        gSetting_set_lck = true;
        SETTINGS_SaveSettings();
    }
    SaveRecord();
}

// factory reset: both records go, the next start sets everything up again
void SURVIVAL_EraseMarker(void)
{
    PY25Q16_SectorErase(SURV_REC_A);
    PY25Q16_SectorErase(SURV_REC_B);
}

uint8_t SURVIVAL_GetMode(void)
{
#ifdef ENABLE_FMRADIO
    if (gFmRadioMode)
        return SURV_MODE_FM;
#endif
    const uint8_t channel = gEeprom.ScreenChannel[gEeprom.TX_VFO];
    if (IsAirSlot(channel))
        return SURV_MODE_AIR;
    if (!IS_MR_CHANNEL(channel))
        return SURV_MODE_CUSTOM;

    const uint8_t g = FindGroup(channel);
    return (g == GROUP_NONE) ? SURV_MODE_FAV : g;   // own saved channels
}

// point the VFO at the first channel of a group/FAV, or at the air band VFO;
// false when FAV is empty
static bool SelectModeChannel(uint8_t mode, uint8_t vfo)
{
    if (mode == SURV_MODE_AIR) {
        SetCustomFlag(false);
        gEeprom.ScreenChannel[vfo] = AIR_VFO_CHANNEL;
        return true;
    }
    if (mode < ARRAY_SIZE(GROUPS) && GroupCount(mode) == 0)
        return false;   // e.g. no LOCAL channels in this country
    const uint8_t ch = (mode == SURV_MODE_FAV) ? FirstFav() : GroupStart(mode);
    if (ch == 0xFF)
        return false;
    gEeprom.MrChannel[vfo]     = ch;
    gEeprom.ScreenChannel[vfo] = ch;
    return true;
}

// while a VFO range is scanned the screen is redrawn every few steps only, so
// the 10 kHz and smaller digits would lag: show "123.4----" (the 100 kHz digit
// still moves) until the scan stops on a signal
void SURVIVAL_MaskScanDigits(char *freq)
{
    if (gScanStateDir == SCAN_OFF || FUNCTION_IsRx())
        return;
    char *p = strchr(freq, '.');
    if (p && p[1])
        for (p += 2; *p; p++)
            *p = '-';
}

const char *SURVIVAL_VfoLabel(uint8_t channel)
{
    return IsAirSlot(channel) ? "AIR BAND" : "CUSTOM";
}

void SURVIVAL_VfoRange(uint8_t channel, uint32_t *lower, uint32_t *upper)
{
    const bool air = IsAirSlot(channel);
    *lower = air ? AIR_SCAN_START : gSurvivalCustomStart;
    *upper = air ? AIR_SCAN_END   : gSurvivalCustomEnd;
}

void SURVIVAL_SetMode(uint8_t mode)
{
    const uint8_t vfo = gEeprom.TX_VFO;

    // the FM background scan only continues a scan that was already running
    sScanBeforeFm = (mode == SURV_MODE_FM) && gScanStateDir != SCAN_OFF;

    if (gScanStateDir != SCAN_OFF)
        CHFRSCANNER_Stop();

#ifdef ENABLE_FMRADIO
    if (gFmRadioMode != (mode == SURV_MODE_FM))
        ACTION_FM();   // toggles FM radio on/off
    if (mode == SURV_MODE_FM)
        return;
#else
    if (mode == SURV_MODE_FM)
        mode = SURV_MODE_CUSTOM;
#endif

    if (mode == SURV_MODE_CUSTOM) {
        // free frequency on the band VFO of the range start; a frequency outside the
        // range (an unused band slot can hold anything, e.g. 1300 MHz) starts at its beginning
        const uint8_t slot = FREQ_CHANNEL_FIRST + FREQUENCY_GetBand(gSurvivalCustomStart);
        SetCustomFlag(slot == AIR_VFO_CHANNEL);
        gEeprom.FreqChannel[vfo]   = slot;
        gEeprom.ScreenChannel[vfo] = slot;
        RADIO_ConfigureChannel(vfo, VFO_CONFIGURE_RELOAD);
        VFO_Info_t *v = &gEeprom.VfoInfo[vfo];
        if (v->freq_config_RX.Frequency < gSurvivalCustomStart || v->freq_config_RX.Frequency > gSurvivalCustomEnd) {
            v->freq_config_RX.Frequency = gSurvivalCustomStart;
            v->freq_config_TX.Frequency = gSurvivalCustomStart;
            SETTINGS_SaveChannel(slot, vfo, v, 2);   // saved now: the reload below reads it back
        }
    } else if (!SelectModeChannel(mode, vfo)) {
        gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;   // FAV empty: on to the first group
        SelectModeChannel(SURV_MODE_PMR, vfo);
        if (mode != SURV_MODE_FAV)
            ShowMessage("NO CHANNELS", "IN THIS COUNTRY");
        else
        ShowMessage("FAV EMPTY", "F+7 ADDS");
        gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
        gRequestSaveVFO   = true;
        return;
    }

    gRequestSaveVFO       = true;
    gVfoConfigureMode     = VFO_CONFIGURE_RELOAD;
    gRequestDisplayScreen = DISPLAY_MAIN;
    gUpdateStatus         = true;
}

void SURVIVAL_ToggleFm(void)
{
#ifdef ENABLE_FMRADIO
    if (gFmRadioMode)
        ACTION_FM();   // back to the previous mode
    else
        SURVIVAL_SetMode(SURV_MODE_FM);
#endif
}

void ACTION_SurvivalGroup(void)
{
    uint8_t mode = SURVIVAL_GetMode();
    for (uint8_t n = 0; n < SURV_MODE_COUNT; n++) {
        mode = (mode + 1) % SURV_MODE_COUNT;
        if (mode < ARRAY_SIZE(GROUPS) && mode != SURV_MODE_AIR && GroupCount(mode) == 0)
            continue;   // skip empty groups (AIR is a band, it has no channels)
        if ((gSurvivalFlags & SURV_F_SIMPLE) && mode != SURV_MODE_PMR && mode != SURV_MODE_FRS &&
            mode != SURV_MODE_FM && mode != SURV_MODE_FAV)
            continue;   // simple mode: the few groups that matter
        break;
    }
    SURVIVAL_SetMode(mode);
}

void SURVIVAL_FmStarted(void)
{
    // scan on the BK4819 while the BK1080 plays FM
    const uint8_t vfo = gEeprom.TX_VFO;

    const bool scanning = sScanBeforeFm;
    sScanBeforeFm = false;

    // no scan before FM: only watch the current channel
    if (!scanning || gSurvivalFmScan == 0 || gSurvivalFmScan > SURV_FMSCAN_LAST)
        return;

    if (gSurvivalFmScan == SURV_FMSCAN_LAST) {
        // the mode used before FM: its group or FAV from the current channel on,
        // or the band/CStart..CEnd range for AIR/CUSTOM
    } else {
        sChBeforeFm = gEeprom.ScreenChannel[vfo];
        SelectModeChannel(gSurvivalFmScan - 1, vfo);
        RADIO_ConfigureChannel(vfo, VFO_CONFIGURE_RELOAD);
    }
    RADIO_SelectVfos();

    sFmBgScan = true;
    CHFRSCANNER_Start(true, SCAN_FWD);
}

void SURVIVAL_FmStopped(void)
{
    if (!sFmBgScan)
        return;

    sFmBgScan = false;
    if (gScanStateDir != SCAN_OFF)
        CHFRSCANNER_Stop();

    // FM switched off by hand: back to the channel used before the background scan
    const uint8_t vfo = gEeprom.TX_VFO;
    if (sChBeforeFm != 0xFF) {
        gEeprom.ScreenChannel[vfo] = sChBeforeFm;
        if (IS_MR_CHANNEL(sChBeforeFm))
            gEeprom.MrChannel[vfo] = sChBeforeFm;
        sChBeforeFm = 0xFF;
        RADIO_ConfigureChannel(vfo, VFO_CONFIGURE_RELOAD);
        RADIO_SelectVfos();
        RADIO_SetupRegisters(true);
        gRequestSaveVFO = true;
    }
}

bool SURVIVAL_FmBgScanning(void)
{
#ifdef ENABLE_FMRADIO
    return sFmBgScan && gFmRadioMode && gScanStateDir != SCAN_OFF;
#else
    return false;
#endif
}

void SURVIVAL_FmBgFound(void)
{
    // activity on a scanned channel: leave FM for good and stay on that channel,
    // the user goes back to FM by hand (F+0 or M)
    sFmBgScan    = false;
    sChBeforeFm  = 0xFF;   // the user stays on the channel that was heard
    sStopOnRxEnd = true;
#ifdef ENABLE_FMRADIO
    FM_TurnOff();
#endif
    // switch to the main screen right away (gRequestDisplayScreen is only
    // handled after a key press); the switch also ends the scan, keep the channel
    gEeprom.MrChannel[gEeprom.RX_VFO] = gRxVfo->CHANNEL_SAVE;
    gRequestSaveVFO = true;
    GUI_SelectNextDisplay(DISPLAY_MAIN);
}

uint8_t SURVIVAL_ScanResumeMode(void)
{
    return sStopOnRxEnd ? 0 : gEeprom.SCAN_RESUME_MODE;   // 0 = STOP
}

// ---------------------------------------------------------------- FAV / PRIO

// F+7 on a channel/frequency that is already in FAV takes it out again
static void RemoveFav(uint8_t c)
{
    const uint8_t vfo = gEeprom.TX_VFO;
    char          name[16] = {0};

    SETTINGS_FetchChannelName(name, c);
    SETTINGS_UpdateChannel(c, gTxVfo, false, false, true);   // invalid attributes, name cleared
    if (gSurvivalPrio == c)
        gSurvivalPrio = 0xFF;

    if (gEeprom.ScreenChannel[vfo] == c) {   // standing on it: next FAV channel, or PMR when empty
        uint8_t next = FirstFav();
        if (next == 0xFF)
            next = GroupStart(SURV_MODE_PMR);
        gEeprom.MrChannel[vfo]     = next;
        gEeprom.ScreenChannel[vfo] = next;
        gVfoConfigureMode          = VFO_CONFIGURE_RELOAD;
        gRequestSaveVFO            = true;
    }
    ShowMessage("FAV REMOVED", name);
}

void SURVIVAL_AddFav(void)
{
    VFO_Info_t     vfo   = *gTxVfo;
    const uint32_t f     = vfo.freq_config_RX.Frequency;
    uint8_t        slot  = 0xFF;
    uint8_t        match = 0xFF;

    for (uint8_t c = FavFirst(); IS_MR_CHANNEL(c); c++) {
        if (!RADIO_CheckValidChannel(c, false, 0)) {
            if (slot == 0xFF)
                slot = c;
            continue;
        }
        uint32_t stored;
        EEPROM_ReadBuffer(c * 16, &stored, 4);
        if (stored == f && (match == 0xFF || c == vfo.CHANNEL_SAVE))
            match = c;   // prefer the FAV channel we are standing on
    }
    if (match != 0xFF) {
        RemoveFav(match);
        return;
    }
    if (slot == 0xFF) {
        gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
        ShowMessage("FAV FULL", NULL);
        return;
    }

    char name[16] = {0};
    if (IS_MR_CHANNEL(vfo.CHANNEL_SAVE))
        SETTINGS_FetchChannelName(name, vfo.CHANNEL_SAVE);
    if (name[0] == 0)
        sprintf(name, "%u.%04u", f / 100000, (f / 10) % 10000);

    vfo.SCANLIST1_PARTICIPATION = 0;   // not a second priority channel
    SETTINGS_SaveChannel(slot, gEeprom.TX_VFO, &vfo, 2);
    SETTINGS_SaveChannelName(slot, name);
    gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
    ShowMessage("ADDED TO FAV", name);
}

static void SetPrioFlag(uint8_t ch, bool on)
{
    uint8_t        state[8];
    const uint16_t offset = 0x0D60 + (ch & ~7u);

    gMR_ChannelAttributes[ch].scanlist1 = on;
    EEPROM_ReadBuffer(offset, state, sizeof(state));
    state[ch & 7u] = gMR_ChannelAttributes[ch].__val;
    EEPROM_WriteBuffer(offset, state);
}

void SURVIVAL_TogglePrio(void)
{
    const uint8_t ch = gEeprom.ScreenChannel[gEeprom.TX_VFO];
    if (!IS_MR_CHANNEL(ch)) {   // AIR/CUSTOM: put the frequency into FAV first (F+7)
        gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
        ShowMessage("CHANNELS ONLY", "F+7: FAV FIRST");
        return;
    }
    if (IS_MR_CHANNEL(gSurvivalPrio))
        SetPrioFlag(gSurvivalPrio, false);
    gSurvivalPrio = (gSurvivalPrio == ch) ? 0xFF : ch;
    if (IS_MR_CHANNEL(gSurvivalPrio)) {
        char name[16] = {0};
        SetPrioFlag(gSurvivalPrio, true);
        SETTINGS_FetchChannelName(name, gSurvivalPrio);
        ShowMessage("PRIORITY", name);
    } else {
        ShowMessage("PRIORITY OFF", NULL);
    }
}

// ---------------------------------------------------------------- recently heard

typedef struct {
    uint32_t freq;
    uint32_t time;   // seconds since power on
    uint8_t  channel;
    uint8_t  rssi;   // dBm + 160
} Heard_t;

#define HEARD_N 8

static const char *sMsg1;
static char        sMsg2[16];
static uint8_t     sMsgTimer;   // half seconds left on the message screen

// manual (F+6), shown in the heard list slot
enum { MAN_OFF, MAN_LANDING, MAN_INDEX, MAN_TOPIC, MAN_PROMPT };

static uint8_t sManual;        // MAN_*, shown in the heard list slot
static uint8_t sManTopic;      // index cursor / open topic
static uint8_t sManScroll;     // first row on screen
static uint8_t sManTimer;      // half seconds left on the boot prompt


// short feedback for keys that otherwise only beep, back to main after 1.5 s
void SURVIVAL_Message(const char *line1, const char *line2)
{
    ShowMessage(line1, line2);
}

static void ShowMessage(const char *line1, const char *line2)
{
    if (gScanStateDir != SCAN_OFF && !SURVIVAL_FmBgScanning())
        CHFRSCANNER_Stop();   // the screen switch would end the scan without its cleanup
    sMsg1 = line1;
    strncpy(sMsg2, line2 ? line2 : "", sizeof(sMsg2) - 1);
    sMsgTimer             = 3;
    gRequestDisplayScreen = DISPLAY_SCANNER;
}

static Heard_t  sHeard[HEARD_N];
static uint8_t  sHeardCount;
static uint8_t  sLogCursor;
static uint32_t sHalfSeconds;

void SURVIVAL_TimeSlice500ms(void)
{
    sHalfSeconds++;

    if (sMsgTimer && --sMsgTimer == 0 && gScreenToDisplay == DISPLAY_SCANNER)
        GUI_SelectNextDisplay(gFmRadioMode ? DISPLAY_FM : DISPLAY_MAIN);

#ifdef ENABLE_FMRADIO
    // Eco: the background scan under FM runs 3 s, then the BK4819 receiver is off for
    // 6 s (the AF DAC stays on for the FM audio); a station it hears stops the cycle
    static uint8_t sBgCycle;
    if (SURVIVAL_FmBgScanning() && SURVIVAL_EcoOn() && !FUNCTION_IsRx() && !gScanPauseMode &&
        gCurrentFunction != FUNCTION_TRANSMIT && !SURVIVAL_BeaconActive()) {
        if (++sBgCycle == 6) {
            gScheduleScanListen    = false;
            gScanPauseDelayIn_10ms = 700;
            BK4819_WriteRegister(BK4819_REG_30, BK4819_REG_30_ENABLE_AF_DAC);
        } else if (sBgCycle >= 18) {
            sBgCycle = 0;
            RADIO_SetupRegisters(true);
            gScanPauseDelayIn_10ms = 1;
        }
    } else if (sBgCycle) {
        if (sBgCycle >= 6 && SURVIVAL_FmBgScanning())
            RADIO_SetupRegisters(true);   // Eco switched off while asleep
        sBgCycle = 0;
    }
#endif

    if (sManual == MAN_PROMPT && gScreenToDisplay == DISPLAY_SCANNER) {   // boot prompt countdown
        if (--sManTimer == 0) {
            sManual = MAN_OFF;
            GUI_SelectNextDisplay(DISPLAY_MAIN);
        } else
            gUpdateDisplay = true;
    }

}

void SURVIVAL_LogHeard(void)
{
    const uint32_t f = gRxVfo->freq_config_RX.Frequency;
    uint8_t        d = (sHeardCount < HEARD_N) ? sHeardCount : HEARD_N - 1;

    for (uint8_t i = 0; i < sHeardCount; i++) {
        if (sHeard[i].freq == f) {   // heard again: move it to the top
            d = i;
            break;
        }
    }
    if (d == sHeardCount)
        sHeardCount++;

    memmove(&sHeard[1], &sHeard[0], d * sizeof(Heard_t));
    sHeard[0].freq    = f;
    sHeard[0].time    = sHalfSeconds / 2;
    sHeard[0].channel = gRxVfo->CHANNEL_SAVE;
    sHeard[0].rssi    = BK4819_GetRSSI() / 2;
}

// ------------------------------------------------------------------ key help

// The help pages are the F prefix made visible: while they show, F + 0/4/5/7/* work as before.
static uint8_t sHelpPage;

uint8_t SURVIVAL_HelpPage(void)
{
    if (!gWasFKeyPressed)
        sHelpPage = 0;
    return sHelpPage;
}

bool SURVIVAL_HelpVisible(void)
{
    return gWasFKeyPressed && gInputBoxIndex == 0 &&
           (gScreenToDisplay == DISPLAY_MAIN || gScreenToDisplay == DISPLAY_FM);
}

static void HelpTurn(int8_t direction)
{
    sHelpPage          = (sHelpPage + 3 + direction) % 3;
    gKeyInputCountdown = SURV_HELP_TIMEOUT_500MS;   // keep the page up a little longer
    gUpdateDisplay     = true;
    gUpdateStatus      = true;   // the page title is in the status line
}

bool SURVIVAL_HelpKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    switch (Key) {
        case KEY_MENU:
            if (bKeyHeld) {
                if (bKeyPressed) {   // hold M: leave the help, M does what it does on the screen below
                    gWasFKeyPressed = false;
                    gUpdateStatus   = true;
                }
                return false;
            }
            if (bKeyPressed)
                gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
            else
                HelpTurn(1);     // tap M: next page, on release
            return true;

        case KEY_EXIT:
            if (bKeyPressed && !bKeyHeld) {
                gWasFKeyPressed = false;
                gUpdateStatus   = true;
                gUpdateDisplay  = true;
                gBeepToPlay     = BEEP_1KHZ_60MS_OPTIONAL;
            }
            return true;

        case KEY_UP:
        case KEY_DOWN:
            if (sHelpPage == 0)
                return false;    // page 1: F + UP/DOWN sets the squelch
            if (bKeyPressed)
                HelpTurn(Key == KEY_UP ? -1 : 1);
            return true;

        default:
            return false;
    }
}

// ------------------------------------------------------------------ manual (F+6)

extern const char gSurvivalManual[];   // survival_manual.c, generated from design/manual.txt

#define MAN_ROWS 7             // the title is in the status line

static const char *NextString(const char *p)
{
    return p + strlen(p) + 1;
}

// "@COUNTRY" in the manual is a placeholder: its title is the country name and its
// rows come from the country table (survival_country.c)
static bool IsCountryTopic(const char *title)
{
    return title[0] == '@';
}

// title of topic n as stored (the placeholder is still "@COUNTRY"), NULL past the last one
static const char *ManualTopicRaw(uint8_t n)
{
    const char *p = gSurvivalManual;
    for (; *p; n--) {
        if (n == 0)
            return p;
        do
            p = NextString(p);
        while (*p);
        p++;   // the empty string after the rows
    }
    return NULL;
}

static const char *ManualTopic(uint8_t n)
{
    const char *t = ManualTopicRaw(n);
    return (t && IsCountryTopic(t)) ? Country()->name : t;
}

static uint8_t ManualCount(void)
{
    uint8_t n = 0;
    while (ManualTopic(n))
        n++;
    return n;
}

static uint8_t RowCount(const char *row)
{
    uint8_t n = 0;
    for (; *row; row = NextString(row))
        n++;
    return n;
}

// rows of topic n (after its title); the help pages show some topics too
const char *SURVIVAL_ManualRows(uint8_t n)
{
    const char *title = ManualTopicRaw(n);
    if (IsCountryTopic(title))
        return Country()->numbers;
    return NextString(title);
}

// Menu > Country: rewrite the groups for the new country, FAV stays
void SURVIVAL_SetCountry(uint8_t c)
{
    if (c >= gSurvivalCountryCount)
        return;
    sAskCountry = false;

    if (gScanStateDir != SCAN_OFF)
        CHFRSCANNER_Stop();

    gSurvivalCountry = c;
    sRec.country     = c;   // saved with the rewrite mark
    WriteChannels(true, false);
    if (gSurvivalPrio < FavFirst())
        gSurvivalPrio = 0xFF;   // the group channels were rewritten without the mark

    // both VFOs back on a valid channel: the group they were on may have moved
    for (uint8_t vfo = 0; vfo < 2; vfo++) {
        if (IS_MR_CHANNEL(gEeprom.ScreenChannel[vfo]) && gEeprom.ScreenChannel[vfo] < FavFirst()) {
            gEeprom.MrChannel[vfo]     = 0;
            gEeprom.ScreenChannel[vfo] = 0;
        }
        RADIO_ConfigureChannel(vfo, VFO_CONFIGURE_RELOAD);
    }
    SETTINGS_SaveVfoIndices();
    RADIO_SelectVfos();
    RADIO_SetupRegisters(true);
    ShowMessage(Country()->name, "PRESETS LOADED");
}

// first boot: the country menu, in edit mode (M confirms, EXIT keeps EUROPE)
static bool AskCountry(void)
{
    if (!sAskCountry)
        return false;
    sAskCountry         = false;
    gMenuLevel          = 0;
    gMenuCursor         = UI_MENU_GetMenuIdx(MENU_SRV_COUNTRY);
    gSubMenuSelection   = gSurvivalCountry;
    gIsInSubMenu        = true;
    gAskForConfirmation = 0;
    GUI_SelectNextDisplay(DISPLAY_MENU);
    gIsInSubMenu          = true;   // the display switch leaves the sub menu
    gRequestDisplayScreen = DISPLAY_INVALID;
    sRec.country          = gSurvivalCountry;   // asked once: EXIT keeps EUROPE
    SaveRecord();
    return true;
}

// after the boot logo: 5 s "PRESS M FOR MANUAL", M opens it, any other key skips
void SURVIVAL_ShowBootPrompt(void)
{
    if (AskCountry())
        return;
    SURVIVAL_ShowLog();
    sManual   = MAN_PROMPT;
    sManTimer = 10;
    // no key event yet: a display request would only be applied after the first key
    GUI_SelectNextDisplay(DISPLAY_SCANNER);
    gRequestDisplayScreen = DISPLAY_INVALID;
}

// F+9: straight to the country page (numbers, local channels, local notes)
void SURVIVAL_ShowCountryPage(void)
{
    SURVIVAL_ShowManual();
    for (uint8_t n = 0; ManualTopicRaw(n); n++) {
        if (IsCountryTopic(ManualTopicRaw(n))) {
            sManTopic  = n;
            sManScroll = 0;
            sManual    = MAN_TOPIC;
            break;
        }
    }
}

void SURVIVAL_ShowManual(void)
{
    SURVIVAL_ShowLog();
    sManual       = MAN_LANDING;   // stays until a key is pressed
    gUpdateStatus = true;
}

static void ManualClose(void)
{
    sManual               = MAN_OFF;
    gUpdateStatus         = true;
    gRequestDisplayScreen = gFmRadioMode ? DISPLAY_FM : DISPLAY_MAIN;
}

static void ManualKeys(KEY_Code_t Key, bool bKeyPressed)
{
    const uint8_t count = ManualCount();

    if (sManual == MAN_PROMPT) {
        if (!bKeyPressed) {
            if (Key == KEY_MENU)
                SURVIVAL_ShowManual();
            else
                ManualClose();
        }
        return;
    }
    if (Key == KEY_F || (Key == KEY_EXIT && sManual != MAN_TOPIC)) {
        if (!bKeyPressed)
            ManualClose();
        return;
    }
    if (sManual == MAN_LANDING) {
        if (!bKeyPressed)
            sManual = MAN_INDEX;
    } else if (sManual == MAN_INDEX) {
        if (Key == KEY_UP && bKeyPressed && sManTopic > 0)
            sManTopic--;
        else if (Key == KEY_DOWN && bKeyPressed && sManTopic + 1 < count)
            sManTopic++;
        else if (Key == KEY_MENU && !bKeyPressed) {
            sManual    = MAN_TOPIC;
            sManScroll = 0;
        } else
            return;
    } else {
        const uint8_t rows = RowCount(SURVIVAL_ManualRows(sManTopic));
        if (Key == KEY_EXIT) {
            if (!bKeyPressed)
                sManual = MAN_INDEX;
        } else if (Key == KEY_UP && bKeyPressed) {   // a page at a time: the blank end shows a topic ended
            if (sManScroll >= MAN_ROWS)
                sManScroll -= MAN_ROWS;
            else if (sManTopic > 0) {   // last page of the previous topic
                sManTopic--;
                sManScroll = (RowCount(SURVIVAL_ManualRows(sManTopic)) - 1) / MAN_ROWS * MAN_ROWS;
            } else
                return;
        } else if ((Key == KEY_DOWN && bKeyPressed) || (Key == KEY_MENU && !bKeyPressed)) {
            if (Key == KEY_DOWN && sManScroll + MAN_ROWS < rows)
                sManScroll += MAN_ROWS;
            else if (sManTopic + 1 < count) {
                sManTopic++;
                sManScroll = 0;
            } else
                return;
        } else
            return;
    }
    gBeepToPlay           = BEEP_1KHZ_60MS_OPTIONAL;
    gUpdateStatus         = true;
    gRequestDisplayScreen = DISPLAY_SCANNER;
}

static void DisplayManual(void)
{
    char String[24];

    if (sManual == MAN_PROMPT) {
        sprintf(String, "%u", (sManTimer + 1) / 2);
        UI_PrintString("PRESS M", 0, 127, 0, 8);
        UI_PrintString("FOR MANUAL", 0, 127, 2, 8);
        UI_PrintString(String, 0, 127, 4, 8);
        UI_PrintStringSmallNormal("other key: skip", 0, 127, 6);
    } else if (sManual == MAN_LANDING) {
        static const char LANDING[] = "UP/DN>Choose topic\0" "M>Open topic\0" "UP/DN>Turn page\0"
                                      "M>Next topic\0" "EXIT>Back to list\0" "F>Close manual\0"
                                      "Any key>Topic list\0";
        const char *p = LANDING;
        for (uint8_t row = 0; *p; row++, p = NextString(p))
            UI_PrintStringSmallNormal(p, 0, 0, row);
    } else if (sManual == MAN_INDEX) {
        const uint8_t top = (sManTopic >= MAN_ROWS) ? sManTopic - MAN_ROWS + 1 : 0;
        for (uint8_t row = 0; row < MAN_ROWS; row++) {
            const char *title = ManualTopic(top + row);
            if (!title)
                break;
            sprintf(String, "%2u %s", top + row + 1, title);
            UI_PrintStringSmallNormal(String, 0, 0, row);
            if (top + row == sManTopic)
                for (uint8_t i = 0; i < 128; i++)
                    gFrameBuffer[row][i] ^= 0xFF;
        }
    } else {
        const char *p = SURVIVAL_ManualRows(sManTopic);
        for (uint8_t i = 0; i < sManScroll && *p; i++)
            p = NextString(p);
        for (uint8_t row = 0; row < MAN_ROWS && *p; row++, p = NextString(p))
            UI_PrintStringSmallNormal(p, 0, 0, row);
    }
    ST7565_BlitFullScreen();
}

// status line title bar: the F help pages and the manual use it instead of the radio status
static void Header(const char *title, uint8_t n, uint8_t of)
{
    char Number[8] = {0};

    memset(gStatusLine, 0, sizeof(gStatusLine));
    UI_PrintStringSmallBufferBold(title, gStatusLine);
    if (of) {
        sprintf(Number, "%u/%u", n, of);
        UI_PrintStringSmallBufferBold(Number, gStatusLine + 127 - 7 * strlen(Number));
    }
    for (uint8_t i = 0; i < sizeof(gStatusLine); i++)
        gStatusLine[i] ^= 0xFF;
    ST7565_BlitStatusLine();
}

bool SURVIVAL_DrawHeader(void)
{
    static const char *const HELP_TITLE[] = {"F+KEY", "KEYS", "SIDE KEYS"};

    if (SURVIVAL_HelpVisible()) {
        Header(HELP_TITLE[SURVIVAL_HelpPage()], SURVIVAL_HelpPage() + 1, 3);
        return true;
    }
    if (SURVIVAL_ToolActive() && gScreenToDisplay == DISPLAY_SCANNER && !sMsgTimer) {
        Header(SURVIVAL_ToolTitle(), 0, 0);
        return true;
    }
    if (sManual && gScreenToDisplay == DISPLAY_SCANNER && !sMsgTimer) {
        if (sManual == MAN_TOPIC)
            Header(ManualTopic(sManTopic), sManTopic + 1, ManualCount());
        else
            Header("MANUAL", (sManual == MAN_INDEX) ? sManTopic + 1 : 0, (sManual == MAN_INDEX) ? ManualCount() : 0);
        return true;
    }
    return false;
}

static bool sSkipView;   // the heard list slot shows the skip list

// F+8: the current frequency in / out of the skip list. While scanning: skip it and scan on.
void SURVIVAL_ToggleSkip(void)
{
    const bool     scanning = gScanStateDir != SCAN_OFF;
    const uint32_t f        = scanning ? gRxVfo->freq_config_RX.Frequency : gTxVfo->pRX->Frequency;

    for (uint8_t i = 0; i < sSkipCount; i++) {
        if (sSkip[i] == f) {
            RemoveSkip(i);
            if (!scanning)
                ShowMessage("NOT SKIPPED", "SCAN STOPS HERE");
            return;
        }
    }

    if (sSkipCount == SURV_SKIP_N) {
        gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
        if (!scanning)
            ShowMessage("SKIP LIST FULL", "MENU > SKIP");
        return;
    }
    sSkip[sSkipCount++] = f;
    SaveSkip();

    if (scanning) {   // as F4HWN's scan exclusion: back to the last real find, on to the next step
        gVfoConfigureMode  = VFO_CONFIGURE;
        gFlagResetVfos     = true;
        lastFoundFrqOrChan = lastFoundFrqOrChanOld;
        CHFRSCANNER_ContinueScanning();
        return;
    }
    ShowMessage("SKIPPED", "SCAN PASSES BY");
}

void SURVIVAL_ShowSkipList(void)
{
    SURVIVAL_ShowLog();
    sSkipView = true;
}

void SURVIVAL_ShowLog(void)
{
    if (gScanStateDir != SCAN_OFF)
        CHFRSCANNER_StopHere();
    if (SURVIVAL_ToolActive())
        SURVIVAL_ToolClose();
    sSkipView             = false;
    sManual               = MAN_OFF;
    sLogCursor            = 0;
    sMsgTimer             = 0;
    gRequestDisplayScreen = DISPLAY_SCANNER;   // the code scanner screen is free in survival
}

static void TuneTo(const Heard_t *h)
{
    const uint8_t vfo = gEeprom.TX_VFO;

    gEeprom.ScreenChannel[vfo] = h->channel;
    if (IS_MR_CHANNEL(h->channel)) {
        gEeprom.MrChannel[vfo] = h->channel;
        gVfoConfigureMode      = VFO_CONFIGURE_RELOAD;
    } else {
        // AIR/CUSTOM: the band VFO, then the heard frequency
        RADIO_ConfigureChannel(vfo, VFO_CONFIGURE_RELOAD);
        gEeprom.VfoInfo[vfo].freq_config_RX.Frequency = h->freq;
        gEeprom.VfoInfo[vfo].freq_config_TX.Frequency = h->freq;
        gRequestSaveChannel = 1;
        gVfoConfigureMode   = VFO_CONFIGURE;
    }
    gRequestSaveVFO = true;
}

void SURVIVAL_LogKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    if (Key == KEY_PTT) {   // talk from every screen; the radio screen comes back
        sManual   = MAN_OFF;
        sMsgTimer = 0;
        GENERIC_Key_PTT(bKeyPressed);
        return;
    }
    if (bKeyHeld && Key != KEY_UP && Key != KEY_DOWN)   // held UP/DOWN repeat through the lists
        return;

    // keys that leave this screen act on release: otherwise the release would
    // reach the main screen (M released there = next mode)
    if (sMsgTimer) {   // any key closes a message
        if (!bKeyPressed) {
            sMsgTimer             = 0;
            gRequestDisplayScreen = DISPLAY_MAIN;
        }
        return;
    }

    if (SURVIVAL_ToolActive()) {
        SURVIVAL_ToolKeys(Key, bKeyPressed, bKeyHeld);
        return;
    }

    if (sManual) {
        ManualKeys(Key, bKeyPressed);
        return;
    }

    const uint8_t count = sSkipView ? sSkipCount : sHeardCount;

    switch (Key) {
        case KEY_UP:
            if (!bKeyPressed)
                return;
            if (sLogCursor > 0)
                sLogCursor--;
            break;
        case KEY_DOWN:
            if (!bKeyPressed)
                return;
            if (sLogCursor + 1 < count)
                sLogCursor++;
            break;
        case KEY_MENU:
            if (bKeyPressed)
                return;
            if (sSkipView) {   // delete the selected entry, stay on the list
                if (count) {
                    RemoveSkip(sLogCursor);
                    if (sLogCursor > 0 && sLogCursor >= sSkipCount)
                        sLogCursor--;
                }
                break;
            }
            if (sHeardCount)   // go to the selected entry
                TuneTo(&sHeard[sLogCursor]);
            gRequestDisplayScreen = DISPLAY_MAIN;
            return;
        case KEY_EXIT:
            if (!bKeyPressed)
                gRequestDisplayScreen = DISPLAY_MAIN;
            return;
        default:
            return;
    }
    gBeepToPlay           = BEEP_1KHZ_60MS_OPTIONAL;
    gRequestDisplayScreen = DISPLAY_SCANNER;
}

void SURVIVAL_DisplayLog(void)
{
    char String[24];
    char Name[16];

    UI_DisplayClear();

    if (!sMsgTimer && SURVIVAL_ToolActive()) {
        SURVIVAL_ToolDisplay();
        return;
    }

    if (!sMsgTimer && sManual) {
        DisplayManual();
        return;
    }

    if (sMsgTimer) {
        UI_PrintString(sMsg1, 0, 127, 1, 8);
        UI_PrintString(sMsg2, 0, 127, 4, 8);
        ST7565_BlitFullScreen();
        return;
    }

    if (sSkipView) {
        sprintf(String, "SKIP %u/%u  M:DEL", sSkipCount, SURV_SKIP_N);
        UI_PrintStringSmallBold(String, 0, 127, 0);
        if (sSkipCount == 0)
            UI_PrintStringSmallNormal("empty, F+8 adds", 0, 127, 3);
        const uint8_t top = (sLogCursor > 5) ? sLogCursor - 5 : 0;
        for (uint8_t row = 0; row < 6 && top + row < sSkipCount; row++) {
            const uint32_t f = sSkip[top + row];
            sprintf(String, "%c%3lu.%05lu MHz", (top + row == sLogCursor) ? '>' : ' ', f / 100000, f % 100000);
            UI_PrintStringSmallNormal(String, 1, 0, row + 1);
        }
        ST7565_BlitFullScreen();
        return;
    }

    UI_PrintStringSmallBold("HEARD   M:GO", 0, 127, 0);

    if (sHeardCount == 0)
        UI_PrintStringSmallNormal("nothing yet", 0, 127, 3);

    const uint8_t first = (sLogCursor > 5) ? sLogCursor - 5 : 0;
    for (uint8_t row = 0; row < 6 && first + row < sHeardCount; row++) {
        const Heard_t *h   = &sHeard[first + row];
        const uint32_t min = (sHalfSeconds / 2 - h->time) / 60;

        memset(Name, 0, sizeof(Name));
        if (IS_MR_CHANNEL(h->channel))
            SETTINGS_FetchChannelName(Name, h->channel);
        if (Name[0] == 0)
            sprintf(Name, "%lu.%04lu", h->freq / 100000, (h->freq / 10) % 10000);

        if (min < 60)
            sprintf(String, "%c%-10s %2lum", (first + row == sLogCursor) ? '>' : ' ', Name, min);
        else
            sprintf(String, "%c%-10s %2luh", (first + row == sLogCursor) ? '>' : ' ', Name, min / 60);
        UI_PrintStringSmallNormal(String, 1, 0, row + 1);
    }

    ST7565_BlitFullScreen();
}

// ---------------------------------------------------------------- eco

bool SURVIVAL_EcoOn(void)
{
    return gEeprom.BATTERY_SAVE == 5 && gEeprom.BACKLIGHT_TIME == 2 && gEeprom.BACKLIGHT_MAX == BACKLIGHT_ECO_LEVEL;
}

// eco: deepest receive duty cycle, 10 s backlight at half brightness
void SURVIVAL_SetEco(bool on)
{
    gEeprom.BATTERY_SAVE   = on ? 5 : 4;
    gEeprom.BACKLIGHT_TIME = on ? 2 : 12;   // x 5 s
    gEeprom.BACKLIGHT_MAX  = on ? BACKLIGHT_ECO_LEVEL : 10;
    if (gEeprom.BACKLIGHT_MIN >= gEeprom.BACKLIGHT_MAX)
        gEeprom.BACKLIGHT_MIN = 0;
    gRequestSaveSettings = true;
}

#endif
