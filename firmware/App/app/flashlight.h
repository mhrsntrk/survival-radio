#ifndef APP_FLASHLIGHT_H
#define APP_FLASHLIGHT_H

#ifdef ENABLE_FLASHLIGHT

#include <stdint.h>

#if !defined(ENABLE_FEAT_F4HWN) || defined(ENABLE_FEAT_F4HWN_RESCUE_OPS) || defined(ENABLE_SURVIVAL)   // survival: on, strobe, SOS
    enum FlashlightMode_t {
        FLASHLIGHT_OFF = 0,
        FLASHLIGHT_ON,
        FLASHLIGHT_BLINK,
#ifdef ENABLE_SURVIVAL
        FLASHLIGHT_STROBE,   // 3 x (30 ms on, 30 ms off), dark for the rest of the second
#endif
        FLASHLIGHT_SOS
    };

    extern enum FlashlightMode_t gFlashLightState;
    extern volatile uint16_t     gFlashLightBlinkCounter;

    void FlashlightTimeSlice(void);
#endif
void ACTION_FlashLight(void);

#endif

#endif