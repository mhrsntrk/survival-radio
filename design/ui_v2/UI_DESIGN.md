# Survival Radio — UI v2 proposal

Design only; no firmware changes. Source baseline: current `README.md` through v4.1, `firmware/app/survival.c`, `firmware/ui/{main,menu,fmradio,helper}.c`, and `firmware/font.c`. The captures in `design/screens/` were checked against the code: some named captures contain a different screen or an empty frame, so their filenames are not authoritative. Flash numbers below are estimates, not build measurements.

## Principles

- Put the channel name and frequency first. Keep their positions stable during scanning, receiving and transmitting.
- Show an action in words before expecting someone to remember a shortcut. `M` opens a menu everywhere; `EXIT` backs out without saving.
- Preserve the receiver behavior and stored channels. A visual redesign must not silently change transmit permissions, squelch, scan resume or favorites.
- Use large text for the thing being used, small text for settings and instructions. No scrolling labels, blinking instructions, tiny essential text, or new icon alphabet.
- Reuse the split menu, fonts, message screen and heard list. Two menu levels are enough: Basic and Advanced.

The deliberate key change is **M tap = menu; M hold = next group** on the main/FM screens. This replaces the existing opposite mapping. It makes menu discovery and confirmation consistent. Ship the mapping and visible hints together. `F` tap opens a visible shortcut/help panel; existing F+0/4/5/7 sequences work through that panel. Neither change is implemented by these deliverables.

## Pixel contract

Physical coordinates are x=0..127, y=0..63. Status is y=0..7. Content rows R0..R6 start at y=8,16,24,32,40,48,56. A two-row element owns both rows. Clear the content framebuffer before each redraw; never draw text through another element.

| Token | Actual source and placement |
|---|---|
| S | `gFontSmall`, six stored columns, eight bits vertically, **7 px advance** including the helper's leading spacing column. 18 characters maximum across a full row. |
| B | `gFontSmallBold`, same size/advance. No synthetic bold. |
| L | `gFontBig`, seven top-page columns + seven bottom-page columns; 8 px advance, 16 px tall. Maximum 16 characters. |
| D | Active Terminus `gFontBigDigits`, ten columns per page, 16 px tall, 13 px advance and +2 px inset. Decimal point is the helper's three columns of lower-page `0x60`. `-` uses glyph 10. |
| Invert | XOR the specified pixels, including their background. No grayscale. |

The brief's “6x8” is the stored bitmap; the renderer matches `helper.c`'s **7-pixel spacing**, not an artificially compressed font. Frequency `446.00625` occupies 107 px in D, so all five fractional digits can be large when starting at x=0. Frequencies at or above 1000 MHz use L for the entire string (up to 10 characters/80 px). No precision is discarded. All main radio frequencies are MHz; menu editors explicitly label MHz. FM uses one fractional digit. Tiny 3x5 is not needed.

Status on every screen: state B at x=0 (maximum seven characters: `READY`, `SCAN UP`, `SCAN DN`, `RX`, `TX`, `FM`, `MENU`, `BOOT`); optional `LOCK` S at x=49; voltage S at x=84; battery outline x=108..126, y=1..6, three fill segments. The mock battery is a proposed simple outline, not a claim to reuse its current bitmap. Voltage uses the measured value, e.g. `8.2`; battery fill is estimated. Low voltage overrides the state with `LOW BAT`, never hides lock. During RX, reserve x=49..76 for the existing RSSI bar when unlocked (the RX mockup leaves it blank to represent an unavailable reading); when locked, show `LOCK` instead. Leave unsupported status slots blank. Remove MO/CL, scan-list digits and dual-VFO decoration from this single-receiver layout.

A selection bar and a radio-state bar have different positions: menu selection belongs to its field; main SCAN/RX/TX inverts R0. FM's background receiver heading always inverts R4. Do not flash them. The status state names remain visible without relying on inversion alone.

## Screens, row by row

Mockup numbers refer to filenames in `mockups/`; `sheet.png` collects all 50 examples. Blank means cleared, not reusable space. Text below is literal example text unless in braces.

### Boot — 01

Status `BOOT`. R0–R4: existing 128x40 `BITMAP_SurvivalLogo`, unmodified, x=0. R5 B: `RADIO {version}` (mockup `vNEXT` is a placeholder, not a released version). R6 S: `M:MENU  F:HELP`. Keep the four-second maximum but permit an early exit after initialization. Do not block radio reception merely to finish the splash. During initial EEPROM provisioning, retain the splash until ready; suppress navigation and do not promise reception before setup completes.

### Main: channels, bands and radio states — 02–15

| Row | Font | Contents |
|---|---|---|
| R0 | B | Group at x=0: `PMR446`, `FRS`, `MARINE`, `HAM`, `CB`, `FAV`, `AIR`, `CUSTOM`. `PRI` at x=105 only when this stored channel is the selected priority channel. |
| R1–R2 | L | Full channel name, x=0, up to 16 characters; unnamed channel uses `CH 143`. Bands use `AIR BAND` / `CUSTOM`. No redundant Mxxx on named channels. |
| R3–R4 | D, or L above 999 MHz | Current receive frequency. During TX use actual transmit frequency, including marine duplex offset. |
| R5 | S | Channel: `LOW4 NAR SQL1`, `HIGH WIDE SQL1`, or `RX ONLY SQL1` if transmission is unavailable. Band: `AM 25K SQL1` / `FM 12.5K SQL1`; AM/USB are receive-only. |
| R6 | S | Idle/RX: `M:MENU  *:SCAN`; scanning: `*:STOP UP/DN:STOP`; locked: `HOLD F:UNLOCK`; TX: `RELEASE PTT:STOP`. |

Readiness means idle, not permission to transmit. `RX ONLY` is calculated from the actual modulation, channel and outgoing frequency, not merely from the group mask. Transmit blocking logic retains precedence. `PRI` means “this is the priority channel,” not “two receivers are listening.” Priority checks stay at the existing every-20-channel-steps interval; AIR/CUSTOM scans do not gain priority checks.

R0 inverts during SCAN/RX/TX. SCAN UP/DN in status names direction. Channel scans show the real current channel at every redraw. AIR/CUSTOM show `123.4----` during stepping, restoring all digits immediately when stopped or receiving. Keep the current throttled band redraw; never imply the masked digits are a precise tune target. UP/DOWN tap or `*` stops **here**, without an extra step. Holding a direction starts/reverses scanning once.

On TX R5 becomes `{power} {width} {elapsed}`, e.g. `LOW4 NAR 00:12`. Release PTT stops transmission immediately, regardless of screen. On blocked TX show 34 while PTT is held, preserve the selected channel, and require release before retry. Timeout uses 45. New received activity from FM switches to this main layout; no extra modal notification obscures the call. Missing/invalid channel names fall back to channel number. Clip imported names at 16 display characters; the existing storage limit is not enlarged.

### FM listening, seeking and finding stations — 16–18, 46, 50

| Row | Font | Contents |
|---|---|---|
| R0 | B | `FM RADIO` |
| R1–R2 | D | `98.4` (MHz), or actual tuning frequency while searching |
| R3 | S | `SAVED 03`, `TUNE 87.5-108`, `SEEK UP`, `SEEK DOWN`, or `FIND ALL 07/20` |
| R4 | B, inverted | `WATCH MAR 16` / `SCAN AIR BAND` / `SCAN {name}` |
| R5 | S | `156.80000 MHz` or masked `123.4---- MHz` for an advancing band scan |
| R6 | S | `M:MENU  *:SEEK`; while seeking/finding, `*:STOP EXIT:STOP` |

R4 names are limited to 12 characters after the six-character `WATCH ` prefix (13 after `SCAN `); truncate only the display copy. Full frequency remains beneath it. FM range is visible in Tune mode and the FmTune value panel, rather than taking a permanent row.

Keep `SURVIVAL_FmStarted` behavior: with no preceding radio scan, WATCH the current BK4819 channel; with a preceding scan, continue according to FmScan. FM and background radio can coexist because BK1080 is separate. Background speech leaves FM, switches immediately to the main RX screen and stops that scan on receive completion. Returning to FM is manual using side2 or F+0. Opening settings pauses scanning at the current position; returning never silently restarts a paused scan.

`FmFind` does a full broadcast-band search into a temporary list, up to the existing 20 slots. Keep current preservation semantics: no stations found means old list remains; completion or user exit commits a nonempty new list; interruption by power loss preserves the old list. A background radio hit ends FM and follows the same nonempty-list exit rule. Label the initiating action `REPLACE LIST` in the value panel so replacement is explicit. Single SEEK does not erase saved stations.

46 is the empty saved-list state: R0 B inverted `FM SAVED`; R1 blank; R2 S `NO SAVED STATIONS`; R3 blank; R4 S `FmFind: FIND ALL`; R5 S `FmTune: TUNE`; R6 S `M:MENU EXIT:BACK`. EXIT returns to the previous radio screen. Selecting Tune works even if Saved is empty.

### Menus, group picker and value editing — 19–23

Reuse the F4HWN left-list/right-value architecture and existing menu IDs/backends. Add one level flag and a per-level item-ID table; **no new widget engine**. All list names below are at most six characters. Keep items stable rather than moving them according to context; FM-only items are appended only while FM is open.

| Row | Left x=0..47 | Right x=50..127 |
|---|---|---|
| R0 | B `BASIC` or `ADV` | B plain-language title, at most 11 chars |
| R1 | S previous item, at most six chars | Blank, or second title line when needed |
| R2–R3 | L selected item; invert x=0..47 | L value up to nine chars; longer values use S, at most 11 chars per row |
| R4 | S next item | Optional value continuation/unit |
| R5 | Blank | S explanation/unit, at most 11 chars |
| R6 | S across full width: `M:EDIT EXIT:BACK`; in edit: `M:SAVE EXIT:CANCEL` |

Divider at x=48, y=8..55 only, ending before the footer. No separator over text. The first/last neighboring item is blank, not wrapped. Navigation clamps at endpoints. In value edit also invert right R2–R3. M enters edit; another M saves; EXIT cancels. Up/down changes only the focused side. Changing Group commits immediately on the second M and returns to the chosen main/FM screen. Group order stays PMR446, FRS, MARINE, AIR, HAM, CB, FM RADIO, CUSTOM, FAV.

For actions (Heard, Help, Adv, Add favorite), footer is `M:OPEN EXIT:BACK` or `M:DO EXIT:BACK`, as appropriate; do not show “edit” for an immediate action. Long names such as `Squelch` explain short list names. Values requiring more than 11 characters wrap at supplied word breaks, never arbitrarily split letters. Disabled fields show `FIXED` or `RX ONLY` with the reason below; an M press leaves them unchanged. Numeric entry and rename use the full-width editor below, not an overflowing split value.

### Heard — 24–25

R0 B inverted `HEARD {selected}/{count}`. R1–R5 S: five visible entries (down from six to reserve a permanent footer); selected row inverted across 128 px. R6 S `M:GO EXIT:BACK`. Eight RAM entries remain; scroll a five-row window so selection stays visible. Name/frequency x=0, max 12 chars; age right-aligned in a four-character field ending at x=126. Use `<1m`, `2m`, `12m`, `1h`, saturating at `99h+`. Display elapsed age since most recent reception. Full VFO frequencies fit in the name field. M tunes the highlighted record and returns to main; EXIT returns without retuning. A newly heard record must not change which record the user has highlighted; snapshot the list while it is open (or pin selection by frequency).

Empty: R0 `HEARD 0/8`; R1–R2 blank; R3 `NOTHING HEARD YET`; R4–R5 blank; R6 `EXIT:BACK`. M is a no-op. Log remains volatile across power cycles.

### Visible shortcuts and key help — 26–28, 49

F tap opens page 1; Menu→Help does the same. R0 B inverted page title; R1–R5 S; R6 S `M:NEXT EXIT:BACK`. Exactly three pages. M advances and wraps; on pages 2/3, DOWN also advances and UP selects previous.

| Row | SHORTCUTS 1/3 | KEY HELP 2/3 | KEY HELP 3/3 |
|---|---|---|---|
| R1 | `0 FM RADIO` | `M MENU` | `SIDE1 MONITOR` |
| R2 | `4 HEARD` | `HOLD M NEXT GROUP` | `HOLD SIDE1 LIGHT` |
| R3 | `5 PRIORITY` | `* SCAN / STOP` | `REPEAT HOLD: SOS` |
| R4 | `7 FAVORITE` | `HOLD UP/DN SCAN` | `SIDE2 FM RADIO` |
| R5 | `UP/DN SQUELCH` | `HOLD F LOCK` | `HOLD SIDE2 SCAN` |

Page 1 is the F-prefix state: UP/DOWN adjusts and saves squelch, shows the resulting `Quiet` value editor, and consumes the prefix. Therefore page 1 is advanced with M, not DOWN. On pages 2/3 UP/DOWN changes pages. Digits 0/4/5/7 activate their listed action from any help page. F+* starts/stops scan and closes help. All unassigned digits are ignored; no hidden actions. At five seconds of inactivity, return to the invoking screen. No automatic page cycling. Long F locks instead of briefly opening help. Keep optional long 0/4/5/7 aliases on main/FM for existing users; they are not required for discovery.

### Notices and confirmations — 29–38, 43–45, 47–48

Common full-width template: R0 B inverted `NOTICE`; R1–R2 L outcome/question, maximum 16 characters; R3–R4 L channel/name/subject, maximum 16 characters; R5 S explanation, maximum 18; R6 S relevant action. All fonts are source glyphs. Do not use the screen's large type to hide the affected channel.

| State | R1–R2 | R3–R4 | R5 | R6 / behavior |
|---|---|---|---|---|
| Added | `FAV ADDED` | channel | blank | `EXIT:BACK`, 1.5 s |
| Removed | `FAV REMOVED` | channel | blank | `EXIT:BACK`, 1.5 s |
| Priority set/off | `PRIORITY` / `PRIORITY OFF` | channel | blank | `EXIT:BACK`, 1.5 s |
| Empty FAV | `FAV EMPTY` | `ADD A CHANNEL` | blank | `M:BACK F:HELP`; stay until input |
| Full FAV | `FAV FULL` | `58 OF 58` | `REMOVE ONE FIRST` | `EXIT:BACK`; stay until input |
| Priority on VFO | `SAVE TO FAV` | `FIRST` | `THEN SET PRIORITY` | `M:ADD EXIT:BACK`; M adds only, never silently sets priority |
| Blocked TX | `TX BLOCKED` | `RELEASE PTT` | `RX ONLY` | `EXIT:BACK`; release PTT first |
| TX timeout | `TX TIME LIMIT` | `RELEASE PTT` | `TRY AGAIN AFTER` | `EXIT:BACK`; retry only after release |
| Delete favorite | `REMOVE FAV?` | name | blank | `M:YES EXIT:NO`; no timeout commit |
| Reset | `RESET ALL?` | `ERASES FAV` | `RESTORES DEFAULTS` | `M:YES EXIT:NO`; no timeout commit |
| FM save | `SAVE FM 03?` | `98.4 MHz` | `UP/DN SLOT` | `M:YES EXIT:NO`; selected occupied slot says `REPLACES SLOT 03` |
| FM delete | `DELETE FM 03?` | `98.4 MHz` | blank | `M:YES EXIT:NO` |

Add to FAV copies the current channel/frequency to the existing free area 143–200. If already present, ask REMOVE FAV? before deleting; this intentionally adds protection to v4.1's toggle. Removing the current FAV selects the next one, or PMR if the list becomes empty; removing priority clears it. Do not alter factory channel tables. Selecting empty FAV retains the previous radio target and displays the empty message rather than silently changing to PMR.

Pure success messages time out after 1.5 seconds; any keypad press dismisses and is consumed. Persistent errors use EXIT or M to dismiss, except the explicitly assigned actions above. All confirmation commands trigger on release; a consumed gesture includes its release event, avoiding the v3.7 screen-transition bug. Never interpret the same M release as another action on the destination screen.

### Numeric and name editors — 39–40

Full width. R0 B inverted field name and units (`CUSTOM MHz`, `AIR MHz`, `FM MHz`, `START MHz`, `END MHz`, `OFFSET MHz`, `CHANNEL`, `RENAME FAV`). R1 blank. R2 S current text; R3 blank, or an underline under the current character; R4 S instruction; R5 S secondary instruction; R6 S action footer. 39 uses `145.5----`, `TYPE 8 DIGITS`, `EXIT:ERASE`, `M:TUNE HOLD X:BACK` (X means the physical EXIT key). For menu fields replace TUNE with SAVE. 40 uses `CAMP BASE`, `UP/DN LETTER`, `M:NEXT *:SAVE`, `EXIT:ERASE/BACK`.

Frequency entry has explicit M commit; do not tune when the last digit arrives. Below 1000 MHz, three integer and five fractional digits, padding CB with a leading zero; above it, four integer and five fractional digits. Start with the existing frequency's integer width; UP/DOWN before the first digit selects the width on CUSTOM only. FM uses three integer plus one fractional digit. Dot is inserted automatically; incomplete positions are dashes. M validates completeness and the existing receiver's band/gap limits, without silently rounding. AIR tuning preserves current 108–137 MHz VFO behavior; AIR scan remains 118.000–136.975 MHz. Numeric range changes reject start greater than end with `START AFTER END` in R4; do not swap them silently. Step selection retains the firmware's 8.33 kHz arithmetic, not decimal approximations invented by this design.

Main channel digits select the group's displayed channel number (e.g. MAR 16, not EEPROM slot 39); FAV digits select 143–200; HAM digits select its 1–5 list position; NOAA WX channels use 201–207 as in the table. Editor title/label identifies this domain. Invalid selections remain in the editor with `NOT IN GROUP`. UP/DOWN clears incomplete input only when explicitly exiting; no accidental tuning from a partial number.

Rename is FAV-only and retains the existing stored-name length. UP/DOWN cycles supported printable characters, M advances the cursor, EXIT deletes or backs out if empty, * saves, long EXIT cancels. Digits insert literal digits; F and its shortcuts are inactive while entering text. Empty names display the channel-number fallback. No touchscreen-like keyboard is proposed.

### About and maintenance — 41–42

About: R0 B inverted `ABOUT`; R1 S `BMKs {version}`; R2 S edition; R3 S measured `BATTERY 8.20V`; R4 S build identifier; R5 blank; R6 `EXIT:BACK`. Mockup says `BUILD: PROPOSAL` intentionally.

Hidden service entry remains the existing PTT+upper-side-key power-on gesture. 42 is its landing page: R0 B inverted `SERVICE`; R1–R3 S summaries `F Lock: READ ONLY`, `BatCal: 8.20V`, `BatTyp: 1600mAh`; R4 S `CALIBRATION TOOLS`; R5 blank; R6 `M:EDIT EXIT:BACK`. UP/DOWN selects a row (invert the selected row in implementation); M opens the normal split editor. Existing band-lock is shown read-only, not an alternative to the survival permission mask. BatCal and BatTyp retain their current limits/backends. No new calibration defaults are written. Suppress PTT transmission until the startup chord has been released. This service route is not a third everyday settings group.

## Complete menu inventory

Basic is the entry menu. Advanced opens through `Adv` and EXIT returns to Basic. Keep names in the left column short but explain them in the right title. Slashes below separate selectable values, not text to fit on one line. Line breaks in long titles/values are explicit at word boundaries. Existing stored values not offered by a simplified selector are displayed as `Custom` and preserved until the user commits a replacement.

### Basic — 10 entries, plus four FM entries when FM is open

| Name | Right title | Values/action and scope |
|---|---|---|
| Group | Use group | PMR446 / FRS / MARINE / AIR / HAM / CB / FM RADIO / CUSTOM / FAV. Current group initially selected. |
| Heard | Last heard | `OPEN LIST`; eight recent receptions. |
| Favs | Favorite | `ADD` or `REMOVE`; current underlying radio channel, with remove confirmation. In FM shows `RADIO ONLY`; does not save a broadcast station. |
| Prio | Priority | `SET HERE` / `CLEAR`; current stored channel. In AIR/CUSTOM opens save-first notice; in FM `RADIO ONLY`. |
| Quiet | Squelch | 0 `OPEN`, 1–9; R5 `0=OPEN`, higher numbers reject weaker signals. Preserve owner's SQL1. F+UP/DOWN remains a shortcut. |
| Power | TX power | `LOW 1`…`LOW 5` / `MID` / `HIGH`; retain existing `USER` as `Custom`. No new claim of calibrated watts. RX-only contexts display `RX ONLY`. |
| Eco | Save power | `ON` / `OFF` / `Custom` readout. ON = existing 1:5 duty, 10 s light, 5/10 brightness; OFF = existing 1:4, 60 s, 10/10. Editing Light independently yields Custom. |
| Radio | FM radio | `OPEN` / `CLOSE`, same operation as side2. |
| Adv | Advanced | `OPEN`; no password or concealed gesture. |
| Help | Key help | `OPEN`; three pages above. |
| FmTune | FM tuning | FM only: `Saved` / `Tune`; R5 band range. |
| FmSave | Save FM | FM only: opens save/slot/overwrite confirmation. |
| FmDel | Delete FM | FM only: selected saved station confirmation; disabled in unsaved Tune mode. |
| FmFind | Find all | FM only: `REPLACE` + `LIST` on two value rows; M opens `FIND ALL FM? / REPLACE LIST` (47), M confirms, EXIT cancels. Uses notice layout. |

### Advanced — ordered list

| Name | Right title | Values / constraints |
|---|---|---|
| FmScan | FM scan | `WATCH ONLY` (old OFF), `LAST GROUP`, PMR446 / FRS / MARINE / AIR / HAM / CB. Retain current backend options; LAST GROUP also preserves FAV/CUSTOM. Only continues a scan already active before FM. |
| Mod | Modulation | FM / AM / USB. AIR fixed AM; factory group presets fixed to their table. Editable in CUSTOM and FAV. AM/USB never imply TX. |
| Width | Bandwidth | NARROW / WIDE. Fixed for factory presets; editable CUSTOM/FAV. |
| Step | Tune step | Values in kHz: 0.01, 0.05, 0.1, 0.25, 0.5, 1, 1.25, 2.5, 5, 6.25, 8.33, 9, 10, 12.5, 15, 20, 25, 30, 50, 100, 125, 200, 250, 500. Numeric value in R2, `kHz` R5. Applicable to AIR/CUSTOM. Existing 8.33 option remains. |
| RxTone | Hear tone | OFF / current CTCSS frequencies, e.g. `88.5 Hz`. |
| RxCode | Hear code | OFF / existing DCS values, e.g. `D023N`, `D023I`; R5 `N=NORMAL` or `I=INVERT`. |
| TxTone | Send tone | OFF / current CTCSS frequency table. |
| TxCode | Send code | OFF / existing normal/inverted DCS table. |
| Shift | TX shift | OFF / PLUS / MINUS. Factory marine duplex stays fixed. Editable CUSTOM/FAV. |
| Offset | TX offset | Existing numeric MHz limits; full-width editor. Does not override permission checks. |
| Resume | After call | `STOP`, `WAIT 5s`, `ON SILENCE`, using stored values 0, 81, 1 respectively. The last resumes after the existing 250 ms carrier delay; helper says `0.25s DELAY`. Preserve other stored resume timings as Custom until edited; FM background hits still force STOP. |
| Start | Scan start | CUSTOM MHz full-width editor; other groups show `CUSTOM ONLY` (two lines). |
| End | Scan end | As Start; validate against Start and receiver limits. |
| TxPMR | PMR send | `BLOCK` / `ALLOW`, existing mask bit. |
| TxFRS | FRS send | `BLOCK` / `ALLOW`, existing mask bit. |
| TxMAR | Marine send | `BLOCK` / `ALLOW`, existing mask bit. |
| TxHAM | Ham send | `BLOCK` / `ALLOW`, existing mask bit. |
| TxCB | CB send | `BLOCK` / `ALLOW`, existing mask bit. |
| TxAll | Wide TX | `BLOCK` / `ALLOW`; second confirmation `WIDE TX? / BYPASS GROUPS` (48), R5 `BLOCKS STILL APPLY`, not a bypass of hard exclusions. Preserve an existing enabled setting until explicitly changed; show R5 `OVERRIDES` under individual group bits while enabled. |
| Rename | FAV name | FAV-only full-width editor; elsewhere `FAV ONLY`. |
| Lock | Auto lock | OFF / 30s / 60s / 120s; other existing values displayed as Custom, not discarded. Manual F hold always available. |
| TxTime | TX limit | 30s / 60s / 120s / 180s; map to stored indices 5, 11, 23, 35 using (seconds/5)-1; retain other valid existing timers as Custom until edited. OFF is not offered by the current backend. Never change a running transmission's timer through a menu. |
| Mic | Mic gain | Existing gain table in dB, correctly format half-dB steps as .0/.5. No new DSP. |
| Light | Backlight | OFF / 10s / 30s / 60s / ALWAYS; preserve other stored values as Custom. Does not change receive duty or brightness. |
| Contr | Contrast | Existing 1–15 scale, current default 10; live preview, EXIT restores old value. |
| Beep | Key sound | OFF / ON. TX/error indications retain their existing policy; UI does not depend on a beep. |
| About | Radio info | Opens 41. |
| Reset | Reset radio | One `ALL` action, explicit confirmation, restores defaults and factory channels and erases favorites. No misleading VFO reset. |

Tone/code families are mutually exclusive per direction in the backend: enabling RxCode disables RxTone and vice versa; same for TX. The right helper shows the affected counterpart as `TONE OFF` / `CODE OFF` before commit. Do not call tones private or secure. Preset-tone changes remain possible; modulation/width/duplex guards protect the underlying channel plan. Group TX permissions remain overlapping frequency permissions exactly as implemented, not independent promises of eligibility. This document is not a new regulatory plan.

### Accounting for every old item and removed path

| Current item(s) | Disposition |
|---|---|
| Group, Eco, Sql, Power | Basic Group, Eco, Quiet, Power. |
| FmScan | Advanced FmScan; change OFF text to WATCH ONLY because receiver still watches. |
| Mode, W/N, Step | Advanced Mod, Width, Step. |
| RxCTCS, RxDCS, TxCTCS, TxDCS | Advanced RxTone, RxCode, TxTone, TxCode; no combined tone editor cost. |
| TxODir, TxOffs, ScnRev, CStart, CEnd | Advanced Shift, Offset, Resume, Start, End. |
| TxPMR, TxFRS, TxMAR, TxHAM, TxCB, TxAll | Retained in Advanced; no automatic change of mask/defaults. |
| ChSave, ChDele | Remove menu routes; Basic Favs / F+7 handles free-slot add and confirmed removal. Remove arbitrary overwrite/delete of preset slots. Keep shared channel-write routines. |
| ChName | Advanced Rename, FAV only. |
| F1Shrt, F1Long, F2Shrt, F2Long | Remove reassignable UI; fixed monitor/light and FM/scan. Migrate action routing without rewriting unrelated EEPROM fields. Existing custom side bindings are intentionally replaced by this documented map. |
| KeyLck, TxTOut | Advanced Lock, TxTime. |
| BatSav | Remove independent control; Eco owns duty cycle. Preserve stored custom values until Eco is explicitly changed. |
| BatTxt | Remove formatting choice; fixed voltage plus battery icon. |
| Mic, BLTime, Contr, Beep, SysInf | Advanced Mic, Light, Contr, Beep, About. |
| Reset | Single confirmed ALL; both current reset variants reinitialize channels, so do not advertise a settings-only reset. |
| Hidden F Lock, BatCal, BatTyp | Service landing: read-only F Lock, retain BatCal and BatTyp backends. |
| Hidden upstream-only menu IDs | No user route in Survival; retain compile guards for other editions. Delete only code demonstrably still linked into Survival. |

Keep AM fix, RSSI, existing 1750 Hz capability and backend protections. Preserve 1750 Hz on **PTT+side1 tap**, only during an already permitted transmission, using its existing action. Do not reintroduce disabled DTMF, spectrum, scramble, code-scanner or dual-watch controls. The README's v3.0 cleanup estimate predates v3.1: much of that saving has already happened.

## Complete key map and input rules

“Hold” means the existing 0.4 s threshold. A hold suppresses its tap. Repeated UP/DOWN in lists/editors uses the existing key-repeat timer. Other held keys trigger at most once, not repeatedly. Unlisted combinations have **no action**; they are not forwarded to upstream bindings. The table accounts for digits individually by grouping keys with identical behavior. X in the compact numeric-editor hint means EXIT, never a new physical key.

### Global physical controls (apply to every screen except explicit boot/TX exceptions)

- PTT: cancel pending edits/confirmations, stop scan here, return to the actual current radio channel and transmit only if allowed. Never tune a highlighted-but-uncommitted menu/heard item. In FM, leave broadcast listening for the underlying radio channel. Release always ends TX. Key lock does not block PTT.
- Side1 tap: toggle monitor on the underlying radio receiver; return to its main screen, cancel edits. In FM it leaves FM. Side1 hold: cycle existing flashlight OFF→ON→SOS→OFF without navigation or committing anything. During TX, side1 tap instead invokes the existing 1750 Hz burst; hold still controls light.
- Side2 tap: toggle FM from main/FM/help/notices; from menu, editors or confirmations first cancel pending state, then toggle. Side2 hold: cancel pending UI, return to radio, start/stop radio scan; while FM is playing, toggles BK4819 background scan in the chosen FmScan group (WATCH ONLY uses current group's normal scan). It does not find broadcast stations. This explicit shortcut may start a new background scan; automatic FM entry still only continues an existing one.
- F hold: cancel edits, return to main/FM and toggle keypad lock. During TX only release PTT, side1 and light actions are processed; other keys are ignored. On locked screens ignore keypad taps/holds except F hold, show the unlock footer; side keys still work and lock stays set.
- During boot, PTT/side actions are processed only after initialization and after releasing the startup chord. M/EXIT dismiss ready splash; F opens help after readiness; UP/DOWN, digits and * do nothing. During initialization all keypad input is ignored. No queuing a held PTT into a surprise first transmission.

### Screen-local keypad behavior (global controls above are inherited)

| Screen | M tap / hold | UP, DOWN tap / hold | EXIT tap / hold | Digits 0–9 tap / hold | * tap / hold | F tap |
|---|---|---|---|---|---|---|
| Main, idle | Basic menu / next group | One channel/step / scan direction | No-op / no-op | Start numeric editor / 0 FM, 4 Heard, 5 Prio, 7 Favs; holds 1,2,3,6,8,9 no-op | Start scan / same once | Help/prefix |
| Main, scanning | Stop here + menu / stop + next group | Stop here / set scan direction | Stop here / same | Stop here + numeric editor / same long shortcuts as idle | Stop here / same once | Stop here + help |
| FM, listening | Basic menu / leave FM for next group | Saved station or 0.1 MHz step / seek direction | Return to underlying radio / same | Tune frequency or saved slot editor / 0 exit FM, 4 Heard (leave FM), 5/7 RADIO ONLY notice; other holds no-op | Seek up / same once | Help/prefix |
| FM, seek/find | Stop seek + menu / stop + next group | Stop seek / seek chosen direction | Stop seek/find / same | Ignored / same long shortcuts as FM | Stop seek/find / same once | Stop seek + help |
| Basic/Advanced list | Edit/open/action / same | Previous/next item / repeat, clamp | Parent screen / return main or FM | All ignored / ignored | Ignored / ignored | Open help, return to same cursor |
| Split value edit | Save / same | Change value / repeat, clamp | Cancel / cancel to home | Numeric values open field editor; otherwise ignored / ignored | Ignored / ignored | Ignored |
| Heard populated | Tune selected / same | Previous/next / repeat, clamp | Return without tuning / same | Ignored / ignored | Ignored / ignored | Help |
| Heard empty | No-op / no-op | No-op / no-op | Return / same | Ignored / ignored | Ignored / ignored | Help |
| Help page 1 | Next page / same | Squelch adjustment / repeat adjustment | Return / same | 0/4/5/7 actions; 1/2/3/6/8/9 ignored / same once | Radio scan toggle and return / same | Close help |
| Help pages 2/3 | Next page / same | Previous/next page / repeat | Return / same | As help page 1 / same | As help page 1 / same | Close help |
| Success notice | Dismiss, consume / same | Dismiss, consume / same | Dismiss / same | Dismiss, consume / same | Dismiss, consume / same | Dismiss, consume |
| Persistent notice | Dismiss or labeled action / same | Ignore / ignore | Dismiss / same | Ignore / ignore | Ignore / ignore | Help only if labeled; otherwise ignore |
| Confirmation | Confirm on release / same once | No-op except FM save slot / repeat slot | Cancel / same | Ignore / ignore | Ignore / ignore | Ignore |
| Numeric editor | Validate/save or tune / same | Select CUSTOM width before first digit; otherwise no-op / same | Erase last digit, empty cancels / cancel entirely | Insert literal digit / insert once; no shortcut aliases | Ignore / ignore | Ignore |
| Name editor | Advance character / same | Cycle character / repeat | Erase; empty cancels / cancel entirely | Insert literal digit / insert once | Save / same | Ignore |
| About | Return / same | Ignore / ignore | Return / same | Ignore / ignore | Ignore / ignore | Ignore |
| Service landing | Open selected / same | Select service item / repeat | Exit service to main / same | Ignore / ignore | Ignore / ignore | Ignore |

Service field editors inherit split-edit controls; read-only F Lock ignores M. All dialog cancellation through global controls also restores previewed Contrast. A foreground RX call may update status without destroying an unsaved edit; a PTT gesture always discards it. Display hints use the current context, not a carousel.

F+0, F+4, F+5, F+7, F+UP/DOWN and F+* are the visible help-panel actions. FM help page 1 (49) changes R1 to `0 EXIT FM`, R3 to `5 RADIO ONLY` and R4 to `7 RADIO ONLY`. In FM, F+* retains its legacy full-band broadcast find behavior, opening the same replacement confirmation as FmFind; label this contextual difference in the FM Help page 1 R5 as `* FIND ALL FM`. In main Help R5 remains `UP/DN SQUELCH`. Squelch adjustment still works in either context and returns to its editor. No other F+digit combinations exist in the proposal.

## Flash estimate, implementation order and acceptance

Units are approximate additional **linked flash bytes**, including strings; “save” is a potential removal, not already available space. Existing fonts and boot bitmap add zero. Planning baseline is the brief's ~4.5 KB free, not an independently measured current binary. Do not spend historical cleanup twice.

| Change | Added bytes | Potential saving | Order / independent acceptance |
|---|---:|---:|---|
| Main spacing, full digits, explicit PRI, state/footer text | 180–350 | 80–180 | 1. Render-only; replay idle/scan/RX/TX, duplex TX frequency and 4-digit MHz. |
| FM labels, footer, state placement | 100–200 | 30–80 | 2. Preserve WATCH/SCAN and speech handoff; test AIR masking and empty station list. |
| Menu relabels, two ID tables, Basic/Advanced, footers | 350–600 | 80–200 | 3. Trace every existing setting to its ID, ensure cancel writes nothing. |
| Fixed side bindings; remove obsolete save/delete/battery-format/duty routes | 40–100 | 300–700 | 4. Retain shared backends. Verify factory channels protected and custom EEPROM values preserved. |
| M mapping, F help (three pages), prefix/long-press dispatch | 500–800 | 0–100 | 5. Ship mappings and hints atomically; check tap/hold/release and all screen transitions. |
| Heard footer, five-row window, stable selection, ages | 80–180 | 0–40 | 6. Retain eight-entry capacity; test a ninth reception while list open. |
| Reused confirmations/notices, FAV deletion, FM save/find routes | 300–550 | 50–130 | 7. No EEPROM write on cancel; full/empty/occupied slot paths. |
| Full-width entry, group-relative channel validation, rename routing | 450–750 | 50–150 | 8. 8/9-digit frequency and gap limits, marine/WX numbering, invalid ranges, name limit. |
| About/service landing, settings guards and migration | 180–350 | 50–150 | 9. Preserve calibration, permissions and FAV; no table-version bump for UI. |
| **Total** | **2180–3880** | **640–1730** | **Net +450..3240 bytes**; even with no savings, upper estimate fits 4.5 KB with about 620 bytes left. |

These ranges are engineering estimates with shared helper costs charged once. Do not count screenshot removal as necessary budget: retain capture support during review. A snapshot of eight heard entries uses roughly 128 bytes additional RAM, depending on alignment; consider stable selection by frequency if RAM is tighter. ID tables and strings belong in flash. Continue using the existing framebuffer; no second 1 KB framebuffer is required by the proposed firmware (the Python renderer is unrelated).

Before each firmware phase, record the actual Survival map/size with the same compiler and enabled features; keep at least 512 bytes application headroom. If a phase exceeds that, defer optional group-relative direct numeric selection and the service landing, retain existing direct-entry semantics with explicit labels, and re-review the affected spec before shipping. Do not remove receiver protections or radio features to make a purely cosmetic change fit.

No firmware implementation or build is part of this task. Implementation must keep the Survival channel-table marker unchanged: a UI revision must not trigger WriteChannels or erase FAV. Existing settings survive until explicitly edited; fixed side bindings and the new M mapping are the stated exceptions. Reset remains deliberately destructive only after its explicit confirmation.

Review gates: all rows fit their declared rectangles; no selected bar crosses a divider/footer; masked frequency becomes precise on stop; empty lists cannot tune; M release is consumed on transitions; F hold does not open help first; PTT release works from every UI state; blocked ranges remain blocked even with TxAll; saving a radio favorite never saves an FM broadcast frequency; cancellation has no EEPROM side effect. On-device capture and key-sequence checks are required before calling the proposal implemented.

## Reproduce and inspect

Run from the repository root:

```sh
/Users/melih/Documents/UV-K5/.venv/bin/python design/ui_v2/render_mockups.py
```

The script reads active font arrays after removing C comments and selecting the actual preprocessor branches (including ENABLE_SMALL_BOLD). It validates array counts/widths, reproduces page/column bit order and helper spacing, and parses the real boot bitmap. Each screen starts as a 128x64 one-bit image and becomes a 512x256 PNG by nearest-neighbor scaling with background `#AABE96` and ink `#141E14`. No operating-system font, interpolation, font approximation or image-generation model is involved. Captions outside screens on the contact sheet also use the real small font. Bounds checks reject overflowing strings, and output checks verify dimensions and two-color palettes.

The delivered mockups are examples of each layout and its important states, not a simulated live firmware build. The row specifications define the remaining data-driven variants such as each individual tone value, error reason and FM band. Source font/bitmap attribution remains with Dual Tachyon and the repository's existing Apache-2.0 notices.

## Created-file inventory

All paths below are relative to `design/ui_v2/`. BRIEF.md and codex_run.log existed before this task and are not deliverables.

- `UI_DESIGN.md`
- `render_mockups.py`
- `mockups/01_boot.png`
- `mockups/02_pmr.png`
- `mockups/03_frs.png`
- `mockups/04_marine.png`
- `mockups/05_ham.png`
- `mockups/06_cb.png`
- `mockups/07_fav.png`
- `mockups/08_air.png`
- `mockups/09_custom.png`
- `mockups/10_air_scan.png`
- `mockups/11_channel_scan.png`
- `mockups/12_received.png`
- `mockups/13_transmit.png`
- `mockups/14_locked.png`
- `mockups/15_custom_ghz.png`
- `mockups/16_fm_watch.png`
- `mockups/17_fm_scan.png`
- `mockups/18_fm_autostore.png`
- `mockups/19_menu_basic.png`
- `mockups/20_menu_edit.png`
- `mockups/21_menu_advanced.png`
- `mockups/22_menu_group.png`
- `mockups/23_menu_fm.png`
- `mockups/24_heard.png`
- `mockups/25_heard_empty.png`
- `mockups/26_help_shortcuts.png`
- `mockups/27_help_controls.png`
- `mockups/28_help_side.png`
- `mockups/29_fav_added.png`
- `mockups/30_priority.png`
- `mockups/31_fav_empty.png`
- `mockups/32_fav_full.png`
- `mockups/33_prio_vfo.png`
- `mockups/34_tx_blocked.png`
- `mockups/35_confirm_delete.png`
- `mockups/36_confirm_reset.png`
- `mockups/37_fm_save.png`
- `mockups/38_fm_delete.png`
- `mockups/39_frequency_entry.png`
- `mockups/40_name_entry.png`
- `mockups/41_system_info.png`
- `mockups/42_service.png`
- `mockups/43_fav_removed.png`
- `mockups/44_priority_off.png`
- `mockups/45_tx_timeout.png`
- `mockups/46_fm_empty.png`
- `mockups/47_confirm_fm_find.png`
- `mockups/48_confirm_wide_tx.png`
- `mockups/49_help_fm.png`
- `mockups/50_fm_seek.png`
- `mockups/sheet.png`
