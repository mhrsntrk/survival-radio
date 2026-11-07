/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#ifdef ENABLE_FMRADIO

#include <string.h>

#include "app/fm.h"
#include "driver/bk1080.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "misc.h"
#include "settings.h"
#include "ui/fmradio.h"
#ifdef ENABLE_SURVIVAL
    #include "app/survival.h"
#endif
#include "ui/helper.h"
#include "ui/inputbox.h"
#include "ui/main.h"
#include "ui/ui.h"

#ifdef ENABLE_SURVIVAL
// UI v2 FM screen (design/ui_v2/UI_DESIGN.md):
//   (title FM RADIO in the status line) R0-R1 frequency in big digits, R2 what the FM receiver does,
//   between two lines (R3-R5): what the radio receiver does in the background and its frequency,
//   R6 the keys that matter now
void UI_DisplayFM(void)
{
    char String[22] = {0};
    char Name[16]   = {0};

    if (UI_DisplayHelp(true))
        return;

    UI_DisplayClear();

    // R1-R2: frequency, or the number being typed
    if (gInputBoxIndex > 0 && !gEeprom.FM_IsMrMode) {
        const char *ascii = INPUTBOX_GetAscii();
        sprintf(String, "%.3s.%.1s", ascii, ascii + 3);
    } else {
        sprintf(String, "%d.%d", gEeprom.FM_FrequencyPlaying / 10, gEeprom.FM_FrequencyPlaying % 10);
    }
    {   // centred big digits (13 px, the point 3 px) and a small MHz on their baseline
        uint8_t width = 4 + 3 * 7;
        for (const char *c = String; *c; c++)
            width += (*c == '.') ? 3 : 13;
        const uint8_t x = (128 - width) / 2;
        UI_DisplayFrequency(String, x, 0, false);
        UI_PrintStringSmallNormal("MHz", x + width - 3 * 7 - 1, 0, 1);
    }

    // R2: saved / tune / seek / find, or the save / delete question
    if (gAskToSave) {
        sprintf(String, "SAVE AS %02u? HOLD M", gFM_ChannelPosition + 1);
    } else if (gAskToDelete) {
        sprintf(String, "DELETE %02u? HOLD M", gEeprom.FM_SelectedChannel + 1);
    } else if (gFM_ScanState != FM_SCAN_OFF) {
        if (gFM_AutoScan)
            sprintf(String, "FIND ALL %02u/20", gFM_ChannelPosition);
        else
            strcpy(String, (gFM_ScanState > 0) ? "SEEK UP" : "SEEK DOWN");
    } else if (gEeprom.FM_IsMrMode) {
        sprintf(String, "SAVED %02u", gEeprom.FM_SelectedChannel + 1);
    } else {
        sprintf(String, "TUNE %d%s-%d",
            BK1080_GetFreqLoLimit(gEeprom.FM_Band) / 10,
            gEeprom.FM_Band == 0 ? ".5" : "",
            BK1080_GetFreqHiLimit(gEeprom.FM_Band) / 10);
    }
    UI_PrintStringSmallNormal(String, 0, 127, 2);   // centred under the frequency
    for (uint8_t i = 0; i < 128; i++)
        gFrameBuffer[2][i] <<= 1;     // 1 px lower, away from the digits

    // R4 + R5: the radio receiver, refreshed on every scan step
    const uint8_t channel = gRxVfo->CHANNEL_SAVE;
    if (IS_MR_CHANNEL(channel))
        SETTINGS_FetchChannelName(Name, channel);
    else
        strcpy(Name, SURVIVAL_VfoLabel(channel));
    Name[12] = 0;
    sprintf(String, "%s %s", SURVIVAL_FmBgScanning() ? "SCAN" : "WATCH", Name);
    UI_PrintStringSmallBold(String, 0, 127, 4);

    const uint32_t f = gRxVfo->freq_config_RX.Frequency;
    sprintf(String, "%u.%05u", f / 100000, f % 100000);
    if (!IS_MR_CHANNEL(channel))
        SURVIVAL_MaskScanDigits(String);
    strcat(String, " MHz");
    UI_PrintStringSmallNormal(String, 0, 127, 5);

    // the receiver rows move 4 px up into a full width box (y 26..45),
    // so the key hint below reads as the FM radio's, not the receiver's
    for (uint8_t i = 0; i < 128; i++) {
        uint32_t w = 0x04 | ((uint32_t)(gFrameBuffer[4][i] | gFrameBuffer[5][i] << 8) << 4) | (1u << 21);
        if (i == 0 || i == 127)
            w |= 0x3FFFFC;            // the sides close the box, y 26..45
        gFrameBuffer[3][i] = w;
        gFrameBuffer[4][i] = w >> 8;
        gFrameBuffer[5][i] = w >> 16;
    }

    // R6: keys
    UI_PrintStringSmallNormal(
        (gFM_ScanState != FM_SCAN_OFF) ? "*:STOP" :
        (gAskToSave || gAskToDelete)   ? "EXIT:CANCEL" : "*:SEEK HOLD M:SAVE", 0, 0, 6);

    ST7565_BlitFullScreen();
}
#else
void UI_DisplayFM(void)
{
    char String[16] = {0};
    char *pPrintStr = String;
    UI_DisplayClear();

    UI_PrintString("FM", 2, 0, 0, 8);

    sprintf(String, "%d%s-%dM", 
        BK1080_GetFreqLoLimit(gEeprom.FM_Band)/10,
        gEeprom.FM_Band == 0 ? ".5" : "",
        BK1080_GetFreqHiLimit(gEeprom.FM_Band)/10
        );
    
    UI_PrintStringSmallNormal(String, 1, 0, 6);


    //uint8_t spacings[] = {20,10,5};
    //sprintf(String, "%d0k", spacings[gEeprom.FM_Space % 3]);
    //UI_PrintStringSmallNormal(String, 127 - 4*7, 0, 6);

    if (gAskToSave) {
        pPrintStr = "SAVE?";
    } else if (gAskToDelete) {
        pPrintStr = "DEL?";
    } else if (gFM_ScanState == FM_SCAN_OFF) {
        if (gEeprom.FM_IsMrMode) {
            sprintf(String, "MR(CH%02u)", gEeprom.FM_SelectedChannel + 1);
            pPrintStr = String;
        } else {
            pPrintStr = "VFO";
            for (unsigned int i = 0; i < 20; i++) {
                if (gEeprom.FM_FrequencyPlaying == gFM_Channels[i]) {
                    sprintf(String, "VFO(CH%02u)", i + 1);
                    pPrintStr = String;
                    break;
                }
            }
        }
    } else if (gFM_AutoScan) {
        sprintf(String, "A-SCAN(%u)", gFM_ChannelPosition + 1);
        pPrintStr = String;
    } else {
        pPrintStr = "M-SCAN";
    }

    UI_PrintString(pPrintStr, 0, 127, 3, 10); // memory, vfo, scan

    memset(String, 0, sizeof(String));
    if (gAskToSave || (gEeprom.FM_IsMrMode && gInputBoxIndex > 0)) {
        UI_GenerateChannelString(String, gFM_ChannelPosition);
    } else if (gAskToDelete) {
        sprintf(String, "CH-%02u", gEeprom.FM_SelectedChannel + 1);
    } else {
        if (gInputBoxIndex == 0) {
            sprintf(String, "%3d.%d", gEeprom.FM_FrequencyPlaying / 10, gEeprom.FM_FrequencyPlaying % 10);
        } else {
            const char * ascii = INPUTBOX_GetAscii();
            sprintf(String, "%.3s.%.1s",ascii, ascii + 3);
        }

        UI_DisplayFrequency(String, 36, 1, gInputBoxIndex == 0);  // frequency
        ST7565_BlitFullScreen();
        return;
    }

    UI_PrintString(String, 0, 127, 1, 10);

    ST7565_BlitFullScreen();
}
#endif


#endif
