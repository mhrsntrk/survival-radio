
#include "app/app.h"
#include "app/chFrScanner.h"
#ifdef ENABLE_FMRADIO
    #include "app/fm.h"
#endif
#ifdef ENABLE_SURVIVAL
    #include "app/survival.h"
    #include "driver/bk4819.h"
#endif
#include "functions.h"
#include "misc.h"
#include "settings.h"
//#include "debugging.h"

int8_t            gScanStateDir;
bool              gScanKeepResult;
bool              gScanPauseMode;

#ifdef ENABLE_SCAN_RANGES
uint32_t          gScanRangeStart;
uint32_t          gScanRangeStop;
#endif

typedef enum {
    SCAN_NEXT_CHAN_SCANLIST1 = 0,
    SCAN_NEXT_CHAN_SCANLIST2,
    SCAN_NEXT_CHAN_DUAL_WATCH,
    SCAN_NEXT_CHAN_MR,
    SCAN_NEXT_NUM
} scan_next_chan_t;

scan_next_chan_t    currentScanList;
uint32_t            initialFrqOrChan;
uint8_t             initialCROSS_BAND_RX_TX;

#ifndef ENABLE_FEAT_F4HWN
    uint32_t lastFoundFrqOrChan;
#else
    uint32_t lastFoundFrqOrChan;
    uint32_t lastFoundFrqOrChanOld;
#endif

static void NextFreqChannel(void);
static void NextMemChannel(void);

#ifdef ENABLE_SURVIVAL
// Fast scan: a short first look at the RSSI after every hop. A step at the
// noise floor is left after ~20 ms, anything clearly above it gets the full
// 90 ms so the squelch can decide. The floor is learned from the empty steps.
#define SCAN_PRECHECK_10MS 2
#define SCAN_FULL_10MS     9

static bool     sPrecheck;
static uint16_t sNoiseFloor;    // RSSI (0.5 dB units) of empty steps, 0 = not known yet
static uint8_t  sDisplaySteps;
static uint8_t  sPrioSteps;     // steps since the last look at the priority channel
static bool     sPrioPeek;      // the radio is on the priority channel right now

#define PRIO_EVERY_STEPS 20

static void ScanStepDone(void)
{
    gScanPauseDelayIn_10ms = SCAN_PRECHECK_10MS;
    sPrecheck              = true;

    // channel scans redraw on every step (a fixed step count aliases with small
    // groups: with 2 FAV channels one of them was never shown); range scans
    // redraw every 4th step, their kHz digits are dashed anyway
    if (IS_MR_CHANNEL(gNextMrChannel) || ++sDisplaySteps >= 4) {
        sDisplaySteps  = 0;
        gUpdateDisplay = true;
    }
}

// true when the step is at the noise floor and can be left right away
static bool PrecheckEmpty(void)
{
    const uint16_t rssi   = BK4819_GetRSSI();
    const uint16_t margin = 4 + 2 * gEeprom.SQUELCH_LEVEL;   // 3 dB at SQL1, 5 dB at SQL3

    if (sNoiseFloor == 0) {   // first step of a scan: learn, give it the full time
        sNoiseFloor = rssi;
        return false;
    }
    if (rssi + margin < sNoiseFloor)
        sNoiseFloor = rssi;   // clearly lower: the first step was busy, drop at once
    if (rssi > sNoiseFloor + margin)
        return false;

    sNoiseFloor = (sNoiseFloor * 7 + rssi) / 8;   // follow the floor slowly
    return true;
}
#endif

void CHFRSCANNER_Start(const bool storeBackupSettings, const int8_t scan_direction)
{
#ifdef ENABLE_SURVIVAL
    sNoiseFloor = 0;   // bands differ, learn the floor again
    sPrecheck   = false;
    sPrioPeek   = false;
    sPrioSteps  = 0;
#endif
#ifdef ENABLE_SURVIVAL
    SURVIVAL_ApplyScanRange();
#endif

    if (storeBackupSettings) {
        initialCROSS_BAND_RX_TX = gEeprom.CROSS_BAND_RX_TX;
        gEeprom.CROSS_BAND_RX_TX = CROSS_BAND_OFF;
        gScanKeepResult = false;
    }
    
    RADIO_SelectVfos();

    gNextMrChannel   = gRxVfo->CHANNEL_SAVE;
    currentScanList = SCAN_NEXT_CHAN_SCANLIST1;
    gScanStateDir    = scan_direction;

    if (IS_MR_CHANNEL(gNextMrChannel))
    {   // channel mode
        if (storeBackupSettings) {
            initialFrqOrChan = gRxVfo->CHANNEL_SAVE;
            lastFoundFrqOrChan = initialFrqOrChan;
        }
        NextMemChannel();
    }
    else
    {   // frequency mode
        if (storeBackupSettings) {
            initialFrqOrChan = gRxVfo->freq_config_RX.Frequency;
            lastFoundFrqOrChan = initialFrqOrChan;
        }
        NextFreqChannel();
    }

#ifdef ENABLE_FEAT_F4HWN
    lastFoundFrqOrChanOld = lastFoundFrqOrChan;
#endif

    gScanPauseDelayIn_10ms = scan_pause_delay_in_2_10ms;
    gScheduleScanListen    = false;
    gRxReceptionMode       = RX_MODE_NONE;
    gScanPauseMode         = false;
}

/*
void CHFRSCANNER_ContinueScanning(void)
{
    if (IS_FREQ_CHANNEL(gNextMrChannel))
    {
        if (gCurrentFunction == FUNCTION_INCOMING)
            APP_StartListening(gMonitor ? FUNCTION_MONITOR : FUNCTION_RECEIVE);
        else
            NextFreqChannel();  // switch to next frequency
    }
    else
    {
        if (gCurrentCodeType == CODE_TYPE_OFF && gCurrentFunction == FUNCTION_INCOMING)
            APP_StartListening(gMonitor ? FUNCTION_MONITOR : FUNCTION_RECEIVE);
        else
            NextMemChannel();    // switch to next channel
    }
    
    gScanPauseMode      = false;
    gRxReceptionMode    = RX_MODE_NONE;
    gScheduleScanListen = false;
}
*/

void CHFRSCANNER_ContinueScanning(void)
{
#if defined(ENABLE_SURVIVAL) && defined(ENABLE_FMRADIO)
    // An FM seek / find-all looks for carriers on the BK1080; the BK4819 hopping next to it
    // showed up there as stations. The background scan holds still until the FM scan ends.
    if (gFmRadioMode && gFM_ScanState != FM_SCAN_OFF) {
        gScanPauseDelayIn_10ms = 10;
        gScheduleScanListen    = false;
        return;
    }
#endif
#ifdef ENABLE_SURVIVAL
    // a frequency on the skip list never stops the scan, even with the squelch open
    const bool skipped = !sPrioPeek && SURVIVAL_IsSkipped(gRxVfo->freq_config_RX.Frequency);

    if (skipped) {
        sPrecheck = false;
    } else if (gCurrentFunction != FUNCTION_INCOMING) {
        if (sPrioPeek) {
            // nothing on the priority channel: NextMemChannel goes on from gNextMrChannel
            sPrioPeek = false;
            sPrecheck = false;
        } else {
            if (sPrecheck) {
                sPrecheck = false;
                if (!PrecheckEmpty()) {
                    // something above the noise floor: give the squelch the full time
                    gScanPauseDelayIn_10ms = SCAN_FULL_10MS - SCAN_PRECHECK_10MS;
                    gScheduleScanListen    = false;
                    return;
                }
            }

            // channel scans look at the priority channel every few steps (one receiver:
            // a short call elsewhere can be missed while looking)
            if (IS_MR_CHANNEL(gNextMrChannel) && IS_MR_CHANNEL(gSurvivalPrio) &&
                gSurvivalPrio != gNextMrChannel && ++sPrioSteps >= PRIO_EVERY_STEPS)
            {
                sPrioSteps = 0;
                sPrioPeek  = true;
                gEeprom.ScreenChannel[gEeprom.RX_VFO] = gSurvivalPrio;
                RADIO_ConfigureChannel(gEeprom.RX_VFO, VFO_CONFIGURE_RELOAD);
                RADIO_SetupRegisters(true);
                gScanPauseDelayIn_10ms = SCAN_FULL_10MS;
                gScheduleScanListen    = false;
                gScanPauseMode         = false;
                gRxReceptionMode       = RX_MODE_NONE;
                return;
            }
        }
    }
    sPrecheck = false;
#endif

    if (gCurrentFunction == FUNCTION_INCOMING &&
#ifdef ENABLE_SURVIVAL
        !skipped &&
#endif
        (IS_FREQ_CHANNEL(gNextMrChannel) || gCurrentCodeType == CODE_TYPE_OFF))
    {
        APP_StartListening(gMonitor ? FUNCTION_MONITOR : FUNCTION_RECEIVE);
    }
    else
    {
        IS_FREQ_CHANNEL(gNextMrChannel) ? NextFreqChannel() : NextMemChannel();
    }

    gScanPauseMode      = false;
    gRxReceptionMode    = RX_MODE_NONE;
    gScheduleScanListen = false;
}

void CHFRSCANNER_Found(void)
{
    if (gEeprom.SCAN_RESUME_MODE > 80) {
        if (!gScanPauseMode) {
            gScanPauseDelayIn_10ms = scan_pause_delay_in_5_10ms * (gEeprom.SCAN_RESUME_MODE - 80) * 5;
            gScanPauseMode = true;
        }
    } else {
        gScanPauseDelayIn_10ms = 0;
    }

    // gScheduleScanListen is always false...
    gScheduleScanListen = false;

    /*
    if(gEeprom.SCAN_RESUME_MODE > 1 && gEeprom.SCAN_RESUME_MODE < 26)
    {
        if (!gScanPauseMode)
        {
            gScanPauseDelayIn_10ms = scan_pause_delay_in_5_10ms * (gEeprom.SCAN_RESUME_MODE - 1) * 5;
            gScheduleScanListen    = false;
            gScanPauseMode         = true;
        }
    }
    else
    {
        gScanPauseDelayIn_10ms = 0;
        gScheduleScanListen    = false;
    }
    */

    /*
    switch (gEeprom.SCAN_RESUME_MODE)
    {
        case SCAN_RESUME_TO:
            if (!gScanPauseMode)
            {
                gScanPauseDelayIn_10ms = scan_pause_delay_in_1_10ms;
                gScheduleScanListen    = false;
                gScanPauseMode         = true;
            }
            break;

        case SCAN_RESUME_CO:
        case SCAN_RESUME_SE:
            gScanPauseDelayIn_10ms = 0;
            gScheduleScanListen    = false;
            break;
    }
    */

#ifdef ENABLE_FEAT_F4HWN
    lastFoundFrqOrChanOld = lastFoundFrqOrChan;
#endif

    if (IS_MR_CHANNEL(gRxVfo->CHANNEL_SAVE)) { //memory scan
        lastFoundFrqOrChan = gRxVfo->CHANNEL_SAVE;
    }
    else { // frequency scan
        lastFoundFrqOrChan = gRxVfo->freq_config_RX.Frequency;
    }


    gScanKeepResult = true;
}

#ifdef ENABLE_SURVIVAL
// stop on the channel/frequency the scan is on now (Stop alone goes back to the
// start position when nothing was found)
void CHFRSCANNER_StopHere(void)
{
    lastFoundFrqOrChan = IS_MR_CHANNEL(gNextMrChannel) ? gRxVfo->CHANNEL_SAVE : gRxVfo->freq_config_RX.Frequency;
    gScanKeepResult    = true;
    CHFRSCANNER_Stop();
}
#endif

void CHFRSCANNER_Stop(void)
{
    if(initialCROSS_BAND_RX_TX != CROSS_BAND_OFF) {
        gEeprom.CROSS_BAND_RX_TX = initialCROSS_BAND_RX_TX;
        initialCROSS_BAND_RX_TX = CROSS_BAND_OFF;
    }
    
    gScanStateDir = SCAN_OFF;

    const uint32_t chFr = gScanKeepResult ? lastFoundFrqOrChan : initialFrqOrChan;
    const bool channelChanged = chFr != initialFrqOrChan;
    if (IS_MR_CHANNEL(gNextMrChannel)) {
        gEeprom.MrChannel[gEeprom.RX_VFO]     = chFr;
        gEeprom.ScreenChannel[gEeprom.RX_VFO] = chFr;
        RADIO_ConfigureChannel(gEeprom.RX_VFO, VFO_CONFIGURE_RELOAD);

        if(channelChanged) {
            SETTINGS_SaveVfoIndices();
            gUpdateStatus = true;
        }
    }
    else {
        gRxVfo->freq_config_RX.Frequency = chFr;
        RADIO_ApplyOffset(gRxVfo);
        RADIO_ConfigureSquelchAndOutputPower(gRxVfo);
        if(channelChanged) {
            SETTINGS_SaveChannel(gRxVfo->CHANNEL_SAVE, gEeprom.RX_VFO, gRxVfo, 1);
        }
    }

    #ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
        gEeprom.CURRENT_STATE = 0;
        SETTINGS_WriteCurrentState();
    #endif

    RADIO_SetupRegisters(true);
    gUpdateDisplay = true;
}

static void NextFreqChannel(void)
{
#ifdef ENABLE_SCAN_RANGES
    if(gScanRangeStart) {
        gRxVfo->freq_config_RX.Frequency = APP_SetFreqByStepAndLimits(gRxVfo, gScanStateDir, gScanRangeStart, gScanRangeStop);
    }
    else
#endif
        gRxVfo->freq_config_RX.Frequency = APP_SetFrequencyByStep(gRxVfo, gScanStateDir);

    RADIO_ApplyOffset(gRxVfo);
    RADIO_ConfigureSquelchAndOutputPower(gRxVfo);
    RADIO_SetupRegisters(true);

#ifdef ENABLE_SURVIVAL
    ScanStepDone();
#else
#ifdef ENABLE_FASTER_CHANNEL_SCAN
    gScanPauseDelayIn_10ms = 9;   // 90ms
#else
    gScanPauseDelayIn_10ms = scan_pause_delay_in_6_10ms;
#endif

    gUpdateDisplay     = true;
#endif
}

static void NextMemChannel(void)
{
    static unsigned int prev_mr_chan = 0;
    const bool          enabled      = (gEeprom.SCAN_LIST_DEFAULT > 0 && gEeprom.SCAN_LIST_DEFAULT < 4) ? gEeprom.SCAN_LIST_ENABLED[gEeprom.SCAN_LIST_DEFAULT - 1] : true;
    const int           chan1        = (gEeprom.SCAN_LIST_DEFAULT > 0 && gEeprom.SCAN_LIST_DEFAULT < 4) ? gEeprom.SCANLIST_PRIORITY_CH1[gEeprom.SCAN_LIST_DEFAULT - 1] : -1;
    const int           chan2        = (gEeprom.SCAN_LIST_DEFAULT > 0 && gEeprom.SCAN_LIST_DEFAULT < 4) ? gEeprom.SCANLIST_PRIORITY_CH2[gEeprom.SCAN_LIST_DEFAULT - 1] : -1;
    const unsigned int  prev_chan    = gNextMrChannel;
    unsigned int        chan         = 0;

    //char str[64] = "";

    if (enabled)
    {
        switch (currentScanList)
        {
            case SCAN_NEXT_CHAN_SCANLIST1:
                prev_mr_chan = gNextMrChannel;
    
                //sprintf(str, "-> Chan1 %d\n", chan1 + 1);
                //LogUart(str);

                if (chan1 >= 0)
                {
                    if (RADIO_CheckValidChannel(chan1, false, gEeprom.SCAN_LIST_DEFAULT))
                    {
                        currentScanList = SCAN_NEXT_CHAN_SCANLIST1;
                        gNextMrChannel   = chan1;
                        break;
                    }
                }

                [[fallthrough]];
            case SCAN_NEXT_CHAN_SCANLIST2:

                //sprintf(str, "-> Chan2 %d\n", chan2 + 1);
                //LogUart(str);

                if (chan2 >= 0)
                {
                    if (RADIO_CheckValidChannel(chan2, false, gEeprom.SCAN_LIST_DEFAULT))
                    {
                        currentScanList = SCAN_NEXT_CHAN_SCANLIST2;
                        gNextMrChannel   = chan2;
                        break;
                    }
                }

                [[fallthrough]];
            /*
            case SCAN_NEXT_CHAN_SCANLIST3:
                if (chan3 >= 0)
                {
                    if (RADIO_CheckValidChannel(chan3, false, 0))
                    {
                        currentScanList = SCAN_NEXT_CHAN_SCANLIST3;
                        gNextMrChannel   = chan3;
                        break;
                    }
                }
                [[fallthrough]];
            */
            // this bit doesn't yet work if the other VFO is a frequency
            case SCAN_NEXT_CHAN_DUAL_WATCH:
                // dual watch is enabled - include the other VFO in the scan
//              if (gEeprom.DUAL_WATCH != DUAL_WATCH_OFF)
//              {
//                  chan = (gEeprom.RX_VFO + 1) & 1u;
//                  chan = gEeprom.ScreenChannel[chan];
//                  if (IS_MR_CHANNEL(chan))
//                  {
//                      currentScanList = SCAN_NEXT_CHAN_DUAL_WATCH;
//                      gNextMrChannel   = chan;
//                      break;
//                  }
//              }

            default:
            case SCAN_NEXT_CHAN_MR:
                currentScanList = SCAN_NEXT_CHAN_MR;
                gNextMrChannel   = prev_mr_chan;
                chan             = 0xff;
                break;
        }
    }

    if (!enabled || chan == 0xff)
    {       
#ifdef ENABLE_SURVIVAL
        chan = SURVIVAL_FindNextChannel(gNextMrChannel + gScanStateDir, gScanStateDir, true, gEeprom.SCAN_LIST_DEFAULT);
#else
        chan = RADIO_FindNextChannel(gNextMrChannel + gScanStateDir, gScanStateDir, true, gEeprom.SCAN_LIST_DEFAULT);
#endif
        if (chan == 0xFF)
        {   // no valid channel found
            chan = MR_CHANNEL_FIRST;
        }
        
        gNextMrChannel = chan;

        //sprintf(str, "----> Chan %d\n", chan + 1);
        //LogUart(str);
    }

    if (gNextMrChannel != prev_chan)
    {
        gEeprom.MrChannel[    gEeprom.RX_VFO] = gNextMrChannel;
        gEeprom.ScreenChannel[gEeprom.RX_VFO] = gNextMrChannel;

        RADIO_ConfigureChannel(gEeprom.RX_VFO, VFO_CONFIGURE_RELOAD);
        RADIO_SetupRegisters(true);

#ifndef ENABLE_SURVIVAL
        gUpdateDisplay = true;
#endif
    }

#ifdef ENABLE_SURVIVAL
    ScanStepDone();
#else
#ifdef ENABLE_FASTER_CHANNEL_SCAN
    gScanPauseDelayIn_10ms = 9;  // 90ms .. <= ~60ms it misses signals (squelch response and/or PLL lock time) ?
#else
    gScanPauseDelayIn_10ms = scan_pause_delay_in_3_10ms;
#endif
#endif

    if (enabled)
        if (++currentScanList >= SCAN_NEXT_NUM)
            currentScanList = SCAN_NEXT_CHAN_SCANLIST1;  // back round we go
}
