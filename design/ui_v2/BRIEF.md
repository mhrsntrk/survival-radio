# UI redesign brief — Survival Radio (UV-K5, 128x64 mono LCD)

Goal (from the owner): one clear, simple interface and menu system for the survival firmware.
It must cover every screen, settings should be simplified where possible, everything easy to read
and easy to use under stress (cold hands, bad light, no manual).

Read first: `README.md` (full feature history, key map), `firmware/app/survival.c`,
`firmware/ui/main.c`, `firmware/ui/menu.c`, `firmware/ui/fmradio.c`, `firmware/ui/helper.c`,
`firmware/font.c`, screenshots in `design/screens/*.png` (real LCD captures, 4x / 2x scaled).

## Hardware / framework constraints

- LCD 128x64, 1 bit. Top 8 px = status line (battery, icons). Below: 7 text rows of 8 px (rows 0..6).
- Fonts: small 6x8 (normal + bold, ~18 chars/row), big 8x16 (2 rows, ~16 chars), big frequency digits
  (~2 rows tall), tiny 3x5. Inverted (white on black) bars are possible by XOR-ing a row.
- Keys: M (menu), UP, DOWN, EXIT, 0-9, * , F (shift), side key 1 (upper), side key 2 (lower), PTT.
  Short press and long press (0.4 s) are available on every key; F + digit gives a second layer.
- Flash: ~4.5 KB free. A redesign may also remove code (F4HWN leftovers). Prefer reusing the existing
  menu framework (left list + right value panel, menu names max 6 chars) over a new UI engine,
  unless the gain is big and the cost fits.
- Single receiver (BK4819) + separate FM broadcast receiver (BK1080).

## Current screens (what exists today)

1. Boot: logo (BMKs SURVIVAL bitmap) + RADIO + battery/version, 4 s.
2. Main, channel modes (PMR, FRS, MARINE, HAM, CB, FAV): small label left (M17 or FAV), big channel
   name, row with modulation / power (L1..HIGH) / bandwidth / SQLn, big frequency, bottom row step and
   other icons. Priority channel shows a small "1" icon.
3. Main, band modes (AIR = 118-137 AM, CUSTOM = free VFO): "AIR BAND" / "CUSTOM" as name, big frequency;
   while scanning the kHz digits are dashes (123.4----).
4. FM radio screen: FM + frequency (normal font), MR(CHxx)/VFO/A-SCAN state, band range, divider,
   background receiver line "SCAN <name>" / "WATCH <name>" + frequency.
5. Menu: F4HWN list (left: 6-char names, right: value), 40 items + hidden ones.
6. Recently heard list (F+4): title "HEARD M:GO", 6 rows "name  age".
7. 1.5 s message screen (ADDED TO FAV, PRIORITY, FAV EMPTY ...), two big lines.

Current menu (order): Group, FmScan, Eco, Sql, Power, Mode, W/N, Step, RxCTCS, RxDCS, TxCTCS, TxDCS,
TxODir, TxOffs, ScnRev, CStart, CEnd, TxPMR, TxFRS, TxMAR, TxHAM, TxCB, TxAll, ChSave, ChDele, ChName,
F1Shrt, F1Long, F2Shrt, F2Long, KeyLck, TxTOut, BatSav, BatTxt, Mic, BLTime, Contr, Beep, SysInf, Reset.

Current keys: M tap = next mode, M hold = menu; * tap = scan start/stop; up/down tap = step (or stop a
scan), hold = scan that way; F+0 = FM; F+4 = heard list; F+5 = priority; F+7 = FAV add/remove;
F hold = key lock; side1 tap monitor / hold flashlight (SOS by repeating); side2 tap FM / hold scan;
F+up/down = squelch.

Known pain points: cryptic 6-char menu names, too many items for a survival user, features hidden
behind F+digit, icons like the priority "1" are not self-explanatory, users do not know the keys.

## Deliverables (write ONLY inside `design/ui_v2/`, do not touch `firmware/` or anything else)

1. `design/ui_v2/UI_DESIGN.md`:
   - Design principles (short).
   - Screen-by-screen layout spec for every screen above (+ any new screen you propose, e.g. a key help
     screen), row by row, which font, what text, which icons, inverted bars.
   - New menu: structure (flat or 2-3 groups), every item with its new name (<= 6 chars if it stays in the
     F4HWN list, or say why another layout is worth it), plain-language value texts, what moves out
     of the menu (to defaults or keys) and what is removed. Mark "basic" vs "advanced" if you split.
   - Key map proposal: keep it learnable; list every key on every screen.
   - Estimated flash cost / savings per change and an implementation order (smallest safe steps first).
2. Mockups: render each screen at 128x64 using the REAL font bitmaps from `firmware/font.c`
   (parse the C arrays; small font 6x8 column-major bytes, big font 8x16, big digits) so the mockups are
   pixel-honest, save as PNG scaled 4x with LCD colours (background #AABE96, ink #141E14) into
   `design/ui_v2/mockups/`, plus one contact sheet `design/ui_v2/mockups/sheet.png`.
   Use `/Users/melih/Documents/UV-K5/.venv/bin/python` (Pillow installed). Keep the render script in
   `design/ui_v2/render_mockups.py`.

Be concrete and opinionated; this is for a real handheld that people may use in an emergency.
