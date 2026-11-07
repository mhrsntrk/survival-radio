/* BMKs Survival Radio: country presets (UV-K5 V3 / UV-K1 build only)
 *
 * The country picks the LOCAL group (it replaces FRS, which is not licence-free in
 * Europe), the CB channel plan and the emergency numbers page. Transmit permission
 * still comes from the band switches (Menu > Adv > Tx*), the presets add no TX band.
 */

#ifndef APP_SURVIVAL_COUNTRY_H
#define APP_SURVIVAL_COUNTRY_H

#if defined(ENABLE_SURVIVAL) && defined(SURVIVAL_TARGET_V3)

#include <stdint.h>

enum {
    SURV_CB_CEPT = 0,   // CEPT 40 channels FM
    SURV_CB_DE80,       // CEPT 40 + German channels 41-80 (26.565-26.955 MHz)
    SURV_CB_UK,         // CEPT 40 + UK 27/81 plan (27.60125-27.99125 MHz)
    SURV_CB_PL,         // CEPT 40 shifted 5 kHz down, AM (Polish practice)
};

#define SURV_LOCAL_AM      (1u << 0)
#define SURV_LOCAL_WIDE    (1u << 1)
#define SURV_LOCAL_MAX     8

typedef struct {
    uint32_t freq;      // 10 Hz units
    uint8_t  tone;      // CTCSS_Options index + 1 sent on TX, 0 = none (RX stays open)
    uint8_t  flags;
    char     name[11];
} SurvivalLocal_t;

typedef struct {
    char                   code[3];   // ISO 3166 alpha-2, "EU" = generic
    const char            *name;
    uint8_t                cb;
    uint8_t                localCount;
    const SurvivalLocal_t *local;
    const char            *numbers;   // '\0' separated rows, "" ends
} SurvivalCountry_t;

extern const SurvivalCountry_t gSurvivalCountries[];
extern const uint8_t           gSurvivalCountryCount;

#define SURV_COUNTRY_GENERIC 0        // "EU": CEPT CB, no local channels, 112

#endif

#endif
