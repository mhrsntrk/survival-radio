# BMKs Survival Radio

Custom firmware for the **Quansheng UV-K5** handheld that turns it into a simple, worldwide
emergency and survival receiver: every common short-range channel plan ready to scan, FM broadcast
radio with a background watch, a built-in manual, and a flashlight with strobe and SOS.

Based on [F4HWN](https://github.com/armel/uv-k5-firmware-custom) (fork of
[egzumer](https://github.com/egzumer/uv-k5-firmware-custom) / [DualTachyon](https://github.com/DualTachyon/uv-k5-firmware)).

<p align="center">
  <img src="docs/photos/radio_logo.jpg" width="240" alt="BMKs Survival Radio boot logo on a UV-K5">
  <img src="docs/photos/radio_boot_prompt.jpg" width="240" alt="PRESS M FOR MANUAL after the logo">
  <img src="docs/photos/radio_help_f.jpg" width="240" alt="F key shortcut help">
</p>
<p align="center">
  <img src="docs/photos/radio_manual_index.jpg" width="240" alt="Manual index with the survival notes">
  <img src="docs/photos/radio_manual_first_aid.jpg" width="240" alt="First aid page of the built-in manual">
  <img src="docs/screens/main_pmr.png" width="384" alt="Main screen, LCD capture">
</p>

## What it does

- **Channel groups, ready to use:** PMR446 (16), FRS (22), MARINE (VHF with coast-station duplex), AIR (the whole 118-137 MHz air band, AM), HAM calling channels, CB (40) and your own
  FAV list. `M` steps through them.
- **Fast scan** that stops on a signal. A short RSSI pre-check skips empty steps in ~20 ms.
- **Skip list:** a noisy channel stops the scan? `F+8` and the scan never stops there again.
- **FM broadcast radio** (87.5-108 MHz) that keeps watching your radio group in the background and
  switches over when someone talks.
- **CUSTOM range scan** with your own start, end and step.
- **Favourites, priority channel, recently-heard list** (with age), all on one-key shortcuts.
- **Safe transmit:** sending is allowed only on the bands you enable (PMR and FRS by default). Air band,
  FM broadcast, military air, 406 MHz beacons and marine DSC channel 70 are always blocked.
- **Built-in manual** (`F+6`): 27 topics, from the keys to MAYDAY calls, the phonetic alphabet,
  SOS signals, water, direction finding and first aid.
- **Flashlight:** on, blink, strobe and SOS with the side key.
- **Readable UI:** one meaning per row, big frequency digits, plain-language menu, help on the `F` key.

## Screens

Every image below was captured from the radio's own LCD over the programming cable
(`tools/k5screen.py`), not drawn. The photos above are the same firmware on the radio.

| | |
|:---:|:---:|
| <img src="docs/screens/main_scan.png" width="320"><br>Scanning a band (kHz digits dashed while moving) | <img src="docs/screens/main_air.png" width="320"><br>AIR band, the whole 118-137 MHz range |
| <img src="docs/screens/fm.png" width="320"><br>FM radio, with the radio group watched below | <img src="docs/screens/main_fav.png" width="320"><br>Priority channel, marked PRI |
| <img src="docs/screens/menu_basic.png" width="320"><br>Basic menu | <img src="docs/screens/menu_edit.png" width="320"><br>Editing a value |
| <img src="docs/screens/help_f.png" width="320"><br>`F` tap: the shortcut page | <img src="docs/screens/help_side.png" width="320"><br>Side keys help |
| <img src="docs/screens/boot_prompt.png" width="320"><br>After the logo: press M for the manual | <img src="docs/screens/manual_index.png" width="320"><br>Manual index |
| <img src="docs/screens/manual_topic.png" width="320"><br>A manual topic, a page at a time | <img src="docs/screens/skip.png" width="320"><br>Skip list: scans pass these by |

## Keys

| Key | Tap | Hold |
|---|---|---|
| `M` | next group | menu |
| `*` | start / stop scan | |
| `UP` / `DOWN` | channel or step, stops a scan | scan up / down |
| `0`-`9` | type a channel or frequency | |
| `EXIT` | clear typing, back | |
| `F` | shortcut help (then a key) | key lock |
| `SIDE1` | flashlight: on, blink, strobe, SOS, off | monitor (squelch open) |
| `SIDE2` | FM radio on / off | scan |
| `PTT` | talk, only where sending is allowed | |

**F shortcuts:** `F+0` FM radio, `F+4` heard list, `F+5` priority, `F+6` manual, `F+7` favourite,
`F+8` skip in scan, `F+UP/DOWN` squelch, `F+*` scan.

On the FM screen: `UP/DOWN` tune, `*` seek, hold `M` save, `F+*` find all stations, `F+3` saved / tune,
`EXIT` leave FM.

## Compatibility

- **Quansheng UV-K5 with the DP32G030 MCU** (the original UV-K5 and its UV-K5(8) / UV-K6 / UV-5R Plus
  siblings that run F4HWN). Tested on a UV-K5 with stock firmware 2.01.26 and bootloader 2.00.06.
- **Quansheng UV-K5 V3 / UV-K1 with the PY32F071 MCU** (stock firmware 7.00.x, "V3" printed above the
  barcode): use the separate V3 build, `bmks.survival.v3.bin`. Tested on a UV-K5(8) V3 with stock
  firmware 7.00.16 and bootloader 7.00.07.
- The two builds are **not interchangeable**: never flash the V1 file on a V3 radio or the other way round.
  The stock firmware version tells them apart (2.x: V1, 7.x: V3); `tools/k5link.py` prints it.
- Programming cable: the usual Kenwood-type K-plug USB cable (CH340 or similar).

## Install

Download `bmks.survival.packed.bin` (browser / official updater) or `bmks.survival.bin` (raw, for
`k5flash.py`) from the [latest release](../../releases/latest), or use the copies in [`builds/`](builds/).

1. **Back up your EEPROM first.** The radio's calibration lives in it (0x1E00-0x1FFF) and is unique to
   your unit. For example, with the radio switched on normally:
   ```
   pip install pyserial
   python3 tools/k5link.py --port /dev/ttyUSB0 --dump my_radio_eeprom.bin
   ```
   (`COM3` on Windows, `/dev/cu.usbserial-XXXX` on macOS.)
2. **Bootloader mode:** switch the radio off, hold `PTT`, switch it on, release `PTT`. The screen stays
   blank.
3. **Flash**, either way:
   - **Browser (any OS, Chrome / Edge):** open the [UV tools web flasher](https://egzumer.github.io/uvtools),
     choose *Firmware flasher*, select `bmks.survival.packed.bin`, connect and flash.
   - **Command line:**
     ```
     python3 tools/k5flash.py --port /dev/ttyUSB0 --yes builds/bmks.survival.bin
     ```
4. Switch the radio off and on again **without** `PTT`. On the first start the firmware writes its
   channel tables (your old memories are replaced), then shows the logo and *PRESS M FOR MANUAL*.

To go back, flash any other UV-K5 firmware (for example the stock Quansheng one) the same way.

### UV-K5 V3 / UV-K1

Same steps with the V3 file and the V3 flasher. The bootloader (DFU) mode is entered the same way:
`PTT` held at power-on, blank screen, the flashlight comes on.

- **Browser (Chrome / Edge):** [UVTools2](https://armel.github.io/uvtools2/?mode=flash), select
  `bmks.survival.v3.bin`, connect and flash.
- **Command line:**
  ```
  python3 tools/k5v3flash.py --port /dev/ttyUSB0 builds/bmks.survival.v3.bin --dry-run   # checks the link only
  python3 tools/k5v3flash.py --port /dev/ttyUSB0 builds/bmks.survival.v3.bin --yes
  ```

UVTools2 can also dump the calibration (*Dump Calib*); do that once the firmware runs. On the V3
the settings live in SPI flash, and the survival record (first-boot marker, CUSTOM range) has its own
sector at 0x00D000.

## Sending: read this

The firmware only lets you transmit FM, and only inside bands you switch on in *Menu > Adv*
(`TxPMR`, `TxFRS`, `TxMAR`, `TxHAM`, `TxCB`, `TxAll`). PMR446 and FRS are on by default; check that
they are licence-free where you are. Marine, amateur and many CB uses need a licence. In a real
emergency where life is at risk, the rules of most countries allow any means of calling for help.

The firmware is provided as is, without warranty. You flash it at your own risk. It is not a
certified safety or distress device: carry a proper one (PLB, marine VHF, phone) when it matters.

## Build from source

The firmware lives in [`firmware/`](firmware/) (F4HWN v4.3 tree plus the survival edition, mostly in
`app/survival.c`, `ui/survival.c` and the `ENABLE_SURVIVAL` sections). Build with Docker:

```
EDITION=survival SURVIVAL_VERSION=v6.9 tools/build-f4hwn.sh "$PWD/firmware"
```

Output: `builds/bmks.survival.bin` and `.packed.bin`.

The V3 build lives in [`firmware-v3/`](firmware-v3/) (F4HWN V3 v4.3.2 tree, the PY32F071 port, with the
same survival hooks merged in) and compiles the survival sources from `firmware/`, so the manual and
the channel tables are shared. Build with Docker (or `cmake --preset Survival` with an ARM GCC toolchain):

```
SURVIVAL_VERSION=v6.9 tools/build-f4hwn-v3.sh
```

Output: `builds/bmks.survival.v3.bin`. The manual text is
[`design/manual.txt`](design/manual.txt); `python3 tools/manual2c.py` turns it into
`firmware/app/survival_manual.c` and checks every row fits the screen.

| Path | |
|---|---|
| `firmware/` | firmware source (F4HWN based) |
| `firmware-v3/` | UV-K5 V3 / UV-K1 build (F4HWN V3 based, uses the survival sources in `firmware/`) |
| `tools/build-f4hwn.sh` | Docker build (Alpine, GCC 15) |
| `tools/k5flash.py` | bootloader flasher for raw `.bin` files |
| `tools/build-f4hwn-v3.sh` | Docker build of the V3 edition |
| `tools/k5v3flash.py` | V3 / UV-K1 bootloader flasher (UVTools2 protocol) |
| `tools/k5link.py` | version check and EEPROM dump |
| `tools/k5screen.py` | LCD capture over the cable (F4HWN screenshot protocol) |
| `tools/manual2c.py` | builds the manual from `design/manual.txt` |
| `design/ui_v2/` | the UI design and mockups |
| `docs/DEVLOG_tr.md` | development log, version by version (Turkish) |

## Credits

- [DualTachyon](https://github.com/DualTachyon/uv-k5-firmware) for the open re-implementation,
  [egzumer](https://github.com/egzumer/uv-k5-firmware-custom) and [armel / F4HWN](https://github.com/armel/uv-k5-firmware-custom)
  for the firmware this is built on, [muzkr and armel](https://github.com/armel/uv-k1-k5v3-firmware-custom)
  for the PY32F071 (V3 / UV-K1) port, armel's [UVTools2](https://armel.github.io/uvtools2/), and egzumer's [uvtools](https://github.com/egzumer/uvtools) web flasher.
- Boot logo and UI design review made with OpenAI Codex.
- BMKs Survival Radio by Melih Karakelle.

## License

Apache License 2.0, like the firmware it is based on. See [LICENSE](LICENSE).
