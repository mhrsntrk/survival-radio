/* Survival Radio
 *
 * Modes: fixed channel groups (PMR, FRS, marine, air, ham, CB) stored as
 * memory channels, FM broadcast, and CUSTOM (free frequency / VFO).
 * Side key 2 or the "Group" menu selects the mode; scan and up/down stay
 * inside the current group. TX is allowed per band (menu TxPMR..TxCB).
 */

#ifndef APP_SURVIVAL_H
#define APP_SURVIVAL_H

#ifdef ENABLE_SURVIVAL

#include <stdbool.h>
#include <stdint.h>

#include "driver/keyboard.h"

enum {
    SURV_MODE_PMR = 0,
    SURV_MODE_FRS,
    SURV_MODE_MARINE,
    SURV_MODE_AIR,
    SURV_MODE_HAM,
    SURV_MODE_CB,
    SURV_MODE_FM,
    SURV_MODE_CUSTOM,
    SURV_MODE_FAV,      // own channels after the groups (ChSave / F+7)
    SURV_MODE_COUNT
};

// TX permission bits, one per transmit capable band
enum {
    SURV_TX_PMR = 0,
    SURV_TX_FRS,
    SURV_TX_MARINE,
    SURV_TX_HAM,
    SURV_TX_CB,
    SURV_TX_ALL,      // everything except the blocked ranges, overrides the bits above
    SURV_TX_COUNT
};

extern const char gSubMenu_SRV_MODE[SURV_MODE_COUNT][9];
extern uint8_t    gSurvivalTxMask;
extern uint8_t    gSurvivalFlags;
#define SURV_F_NO_LIMITS (1u << 0)   // emergency override: no power cap, CB may send
#define SURV_F_SIMPLE    (1u << 1)   // simple mode: few groups, PTT behind key lock
#define SURV_F_CUSTOM    (1u << 2)   // CUSTOM shares the air band VFO slot (range in 108-137 MHz)
#define SURV_F_PTT_FREE  (1u << 3)   // key lock leaves PTT usable
#define SURV_F_REWRITE   (1u << 4)   // group rewrite under way, FAV parked in the stage sector
extern uint8_t    gSurvivalFmScan;        // FM background scan: 0 off, 1..6 = group (SURV_MODE_* + 1), 7 = last mode
#define SURV_FMSCAN_LAST (SURV_MODE_FM + 1)
extern uint32_t   gSurvivalCustomStart;   // CUSTOM mode scan range, 10 Hz units
extern uint32_t   gSurvivalCustomEnd;
extern uint8_t    gSurvivalPrio;          // priority channel, 0xFF = none

void    SURVIVAL_Init(void);
void    SURVIVAL_EraseMarker(void);
void    SURVIVAL_SaveSettings(void);
void    SURVIVAL_SaveCustomRange(void);
void    SURVIVAL_ApplyScanRange(void);
bool    SURVIVAL_GetGroupRange(uint8_t channel, uint8_t *first, uint8_t *last);
bool    SURVIVAL_IsFavChannel(uint8_t channel);
uint8_t SURVIVAL_FindNextChannel(uint8_t Channel, int8_t Direction, bool bCheckScanList, uint8_t VFO);
uint8_t SURVIVAL_GetMode(void);
void    SURVIVAL_SetMode(uint8_t mode);
bool    SURVIVAL_TxAllowed(uint32_t frequency);
bool    SURVIVAL_TxCapped(uint32_t frequency);   // SAFE LIMITS: low power, narrow
void    ACTION_SurvivalGroup(void);
void    SURVIVAL_ToggleFm(void);

void    SURVIVAL_AddFav(void);            // F+7: current channel/frequency into / out of FAV
void    SURVIVAL_TogglePrio(void);        // F+5: current channel as priority channel
void    SURVIVAL_LogHeard(void);          // a transmission was received
void    SURVIVAL_TimeSlice500ms(void);
void    SURVIVAL_ShowLog(void);           // F+4: recently heard list
#define SURV_SKIP_N 16
bool    SURVIVAL_IsSkipped(uint32_t frequency);   // scans pass over these frequencies
void    SURVIVAL_ToggleSkip(void);        // F+8: current frequency in / out of the skip list
void    SURVIVAL_ShowSkipList(void);      // Menu > Skip: see and delete entries
#define SURV_HELP_TIMEOUT_500MS (4 * 8000 / 500)   // 32 s: time to read a help page
uint8_t SURVIVAL_HelpPage(void);          // F tap: key help, page 0..2 while the F prefix is active
bool    SURVIVAL_HelpVisible(void);
void    SURVIVAL_ShowManual(void);
void    SURVIVAL_ShowBootPrompt(void);    // after the logo: PRESS M FOR MANUAL, 5 s        // F+6, Menu > Help: index of the built-in manual
const char *SURVIVAL_ManualRows(uint8_t topic);   // rows of a manual topic, '\0' separated, "" ends
bool    SURVIVAL_DrawHeader(void);        // status line title bar of the help / manual, true if drawn
bool    SURVIVAL_HelpKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);   // true = key used by the help pages
void    SURVIVAL_LogKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);
void    SURVIVAL_DisplayLog(void);
bool    SURVIVAL_EcoOn(void);
void    SURVIVAL_SetEco(bool on);
void    SURVIVAL_MaskScanDigits(char *freq);
const char *SURVIVAL_VfoLabel(uint8_t channel);   // "AIR BAND" or "CUSTOM" for the VFO modes
void    SURVIVAL_VfoRange(uint8_t channel, uint32_t *lower, uint32_t *upper);   // scan range of a VFO mode

// country presets (V3 build): LOCAL group, CB plan, emergency numbers
extern uint8_t gSurvivalCountry;
void        SURVIVAL_SetCountry(uint8_t country);   // rewrites the groups, keeps FAV
uint8_t     SURVIVAL_CountryCount(void);
const char *SURVIVAL_CountryName(uint8_t country);
const char *SURVIVAL_CountryRows(void);             // numbers page rows, '\0' separated, "" ends

// tools on the info screen slot (survival_tools.c)
void        SURVIVAL_ToolTick10ms(void);
void        SURVIVAL_ShowBeacon(void);    // F+1
void        SURVIVAL_ShowMeter(void);     // F+2
void        SURVIVAL_ShowStatus(void);    // F+3
bool        SURVIVAL_ToolActive(void);
const char *SURVIVAL_ToolTitle(void);
void        SURVIVAL_ToolKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);
void        SURVIVAL_ToolDisplay(void);
void        SURVIVAL_ToolClose(void);
bool        SURVIVAL_BeaconActive(void);
void        SURVIVAL_BeaconStop(void);
void        SURVIVAL_ShowCountryPage(void);   // F+9: the emergency page of the country
#define SURV_LISTEN_COUNT 4
extern const char *const gSurvivalListenNames[SURV_LISTEN_COUNT];
uint8_t     SURVIVAL_ListenWindow(void);      // 0 = off
void        SURVIVAL_SetListen(uint8_t l);
uint8_t     SURVIVAL_ListenWaitMin(void);     // minutes to the next window, 0 = open
void        SURVIVAL_ListenRestart(void);
void        SURVIVAL_SetSimple(bool on);

// team (survival_team.c): code, name, messages, checks, clock
#define SURV_TEAM_MAX    50   // one per CTCSS tone
#define SURV_NAME_COUNT  30
#define SURV_WATCH_COUNT 4
extern const char *const gSurvivalNames[SURV_NAME_COUNT];
extern const char *const gSurvivalWatchNames[SURV_WATCH_COUNT];
uint8_t     SURVIVAL_Team(void);              // 0 = off
void        SURVIVAL_SetTeam(uint8_t t);
uint8_t     SURVIVAL_TeamName(void);
void        SURVIVAL_SetTeamName(uint8_t n);
bool        SURVIVAL_TeamAutoReply(void);
void        SURVIVAL_SetTeamAutoReply(bool on);
uint8_t     SURVIVAL_TeamWatch(void);
void        SURVIVAL_SetTeamWatch(uint8_t w);
struct VFO_Info_t;
void        SURVIVAL_TeamApply(struct VFO_Info_t *pVfo);
void        SURVIVAL_TeamInit(void);
void        SURVIVAL_TeamRxOn(void);
void        SURVIVAL_TeamRxOff(void);
uint16_t    SURVIVAL_TeamIrqMask(void);
void        SURVIVAL_TeamIrq(uint16_t bits);
void        SURVIVAL_TeamSendId(void);
void        SURVIVAL_TeamTick10ms(void);
void        SURVIVAL_TeamTick1s(void);
void        SURVIVAL_TeamOpen(void);
void        SURVIVAL_TeamKeys(KEY_Code_t Key);
void        SURVIVAL_TeamDisplay(void);
const char *SURVIVAL_TeamTalker(void);
uint16_t    SURVIVAL_ClockMin(void);          // 0xFFFF: not set
void        SURVIVAL_SetClockMin(uint16_t min);
void        SURVIVAL_ShowTeam(void);          // F+3
void        SURVIVAL_Message(const char *line1, const char *line2);   // 1.5 s feedback screen
bool        SURVIVAL_Reserve(void);           // battery reserve mode is on

// FM radio with a background scan of one group on the BK4819
void    SURVIVAL_FmStarted(void);
void    SURVIVAL_FmStopped(void);
bool    SURVIVAL_FmBgScanning(void);
void    SURVIVAL_FmBgFound(void);
uint8_t SURVIVAL_ScanResumeMode(void);

#endif

#endif
