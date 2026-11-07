/* Survival Radio: country presets
 *
 * Rows of the numbers page are at most 18 characters (7 px font, 128 px).
 * Sources: national emergency services; mountain channels from the Spanish
 * mountain federation (Canal 7-7) and Rete Radio Montana (8-16); Freenet from
 * BNetzA Vfg 45/2025; UK CB 27/81 and marine M1/M2 from Ofcom.
 */

#ifdef ENABLE_SURVIVAL

#include <stddef.h>

#include "app/survival_country.h"
#include "misc.h"

// CTCSS_Options index + 1
#define TONE_85_4   (7 + 1)
#define TONE_114_8  (16 + 1)

static const SurvivalLocal_t LOCAL_ES[] = {
    {44608125, TONE_85_4,  0, "PMR 7-7"},     // mountain channel: PMR 7, tone 85.4 Hz (code 7)
};
static const SurvivalLocal_t LOCAL_IT[] = {
    {44609375, TONE_114_8, 0, "PMR 8-16"},    // Rete Radio Montana: PMR 8, tone 114.8 Hz (code 16)
};
// Freenet 149 MHz, licence-free in Germany: receive only here (no TX band, the
// UV-K5 is not a type-approved Freenet radio)
static const SurvivalLocal_t LOCAL_DE[] = {
    {14902500, 0, 0, "FREENET 1"},
    {14903750, 0, 0, "FREENET 2"},
    {14905000, 0, 0, "FREENET 3"},
    {14908750, 0, 0, "FREENET 4"},
    {14910000, 0, 0, "FREENET 5"},
    {14911250, 0, 0, "FREENET 6"},
};
// UK marine: channel 0 (coastguard / lifeboats), M1 and M2 (marinas), receive only
static const SurvivalLocal_t LOCAL_UK[] = {
    {15600000, 0, SURV_LOCAL_WIDE, "MAR 0 CG"},
    {15785000, 0, SURV_LOCAL_WIDE, "MAR M1"},
    {16142500, 0, SURV_LOCAL_WIDE, "MAR M2"},
};

#define L(x) ARRAY_SIZE(x), x

const SurvivalCountry_t gSurvivalCountries[] = {
    {"EU", "EUROPE",      SURV_CB_CEPT, 0, NULL, "112 EMERGENCY\0"},
    {"AT", "AUSTRIA",     SURV_CB_CEPT, 0, NULL, "112 EMERGENCY\0" "133 POLICE\0" "144 AMBULANCE\0" "140 MOUNTAIN\0" "    RESCUE\0"},
    {"BE", "BELGIUM",     SURV_CB_CEPT, 0, NULL, "112 EMERGENCY\0" "101 POLICE\0"},
    {"BG", "BULGARIA",    SURV_CB_CEPT, 0, NULL, "112 EMERGENCY\0"},
    {"HR", "CROATIA",     SURV_CB_CEPT, 0, NULL, "112 EMERGENCY\0" "195 SEA RESCUE\0"},
    {"CY", "CYPRUS",      SURV_CB_CEPT, 0, NULL, "112 EMERGENCY\0" "199 EMERGENCY\0"},
    {"CZ", "CZECHIA",     SURV_CB_CEPT, 0, NULL, "112 EMERGENCY\0" "155 AMBULANCE\0" "158 POLICE\0" "150 FIRE\0" "1210 MOUNTAIN\0" "    RESCUE\0"},
    {"DK", "DENMARK",     SURV_CB_CEPT, 0, NULL, "112 EMERGENCY\0" "114 POLICE\0" "    (NOT URGENT)\0"},
    {"EE", "ESTONIA",     SURV_CB_CEPT, 0, NULL, "112 EMERGENCY\0"},
    {"FI", "FINLAND",     SURV_CB_CEPT, 0, NULL, "112 EMERGENCY\0"},
    {"FR", "FRANCE",      SURV_CB_CEPT, 0, NULL, "112 EMERGENCY\0" "15 AMBULANCE\0" "17 POLICE\0" "18 FIRE\0" "196 SEA RESCUE\0" "114 BY TEXT\0"},
    {"DE", "GERMANY",     SURV_CB_DE80, L(LOCAL_DE),  "112 EMERGENCY\0" "110 POLICE\0"},
    {"GR", "GREECE",      SURV_CB_CEPT, 0, NULL, "112 EMERGENCY\0" "100 POLICE\0" "166 AMBULANCE\0" "108 COASTGUARD\0"},
    {"HU", "HUNGARY",     SURV_CB_CEPT, 0, NULL, "112 EMERGENCY\0"},
    {"IE", "IRELAND",     SURV_CB_CEPT, 0, NULL, "112 OR 999\0"},
    {"IT", "ITALY",       SURV_CB_CEPT, L(LOCAL_IT),  "112 EMERGENCY\0" "118 AMBULANCE\0" "1530 COASTGUARD\0"},
    {"LV", "LATVIA",      SURV_CB_CEPT, 0, NULL, "112 EMERGENCY\0"},
    {"LT", "LITHUANIA",   SURV_CB_CEPT, 0, NULL, "112 EMERGENCY\0"},
    {"LU", "LUXEMBOURG",  SURV_CB_CEPT, 0, NULL, "112 EMERGENCY\0" "113 POLICE\0"},
    {"MT", "MALTA",       SURV_CB_CEPT, 0, NULL, "112 EMERGENCY\0"},
    {"NL", "NETHERLANDS", SURV_CB_CEPT, 0, NULL, "112 EMERGENCY\0"},
    {"PL", "POLAND",      SURV_CB_PL,   0, NULL, "112 EMERGENCY\0" "985 MOUNTAIN /\0" "    WATER RESCUE\0"},
    {"PT", "PORTUGAL",    SURV_CB_CEPT, 0, NULL, "112 EMERGENCY\0"},
    {"RO", "ROMANIA",     SURV_CB_CEPT, 0, NULL, "112 EMERGENCY\0"},
    {"SK", "SLOVAKIA",    SURV_CB_CEPT, 0, NULL, "112 EMERGENCY\0" "18300 MOUNTAIN\0" "    RESCUE\0"},
    {"SI", "SLOVENIA",    SURV_CB_CEPT, 0, NULL, "112 EMERGENCY\0" "113 POLICE\0"},
    {"ES", "SPAIN",       SURV_CB_CEPT, L(LOCAL_ES),  "112 EMERGENCY\0" "062 GUARDIA CIVIL\0" "    MOUNTAIN, SEA\0" "091 POLICE\0"},
    {"SE", "SWEDEN",      SURV_CB_CEPT, 0, NULL, "112 EMERGENCY\0"},
    {"UK", "UK",          SURV_CB_UK,   L(LOCAL_UK),  "999 OR 112\0" "101 POLICE\0" "    (NOT URGENT)\0" "999 SMS: REGISTER\0" "    FIRST\0"},
};

const uint8_t gSurvivalCountryCount = ARRAY_SIZE(gSurvivalCountries);

#endif
