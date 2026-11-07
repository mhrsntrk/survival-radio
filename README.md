# Survival Radio

Custom firmware for the **Quansheng UV-K5 V3 / UV-K1** handheld that turns it into a simple
emergency and survival receiver for Europe: the country's channels and emergency numbers ready to
use, FM broadcast radio with a background watch, an SOS beacon, a signal meter for finding people,
a built-in manual and a flashlight with strobe and SOS.

Based on [F4HWN for the UV-K1 / UV-K5 V3](https://github.com/armel/uv-k1-k5v3-firmware-custom)
(PY32F071 port of [F4HWN](https://github.com/armel/uv-k5-firmware-custom), fork of
[egzumer](https://github.com/egzumer/uv-k5-firmware-custom) / [DualTachyon](https://github.com/DualTachyon/uv-k5-firmware)),
and on [BMKs Survival Radio](https://github.com/melihkarakelle/bmks-survival-radio) by Melih Karakelle.

## What it does

- **Country presets (EU27 + UK).** Chosen on the first start, changed in *Menu > Region*. The
  country sets:
  - the **LOCAL** group, e.g. PMR 7-7 with tone 85.4 Hz in Spain, PMR 8-16 with 114.8 Hz in Italy,
    Freenet in Germany, UK marine channels 0/M1/M2;
  - the **CB plan**: CEPT 40, German 80, UK 27/81, Polish channels 5 kHz lower in AM;
  - the **country page** (`F+9`): emergency, mountain and sea rescue numbers, local channels and
    the public FM station that carries official news.
- **Channel groups**: PMR446, LOCAL, MARINE (with coast-station duplex), AIR (118-137 MHz AM),
  HAM calling channels, CB, FM broadcast, CUSTOM range and your own FAV list. `M` steps through them.
- **SOS beacon** (`F+1`): Morse SOS as a tone on the current channel, 10 s every 2 minutes for
  30 minutes, with the flashlight on SOS. For life-threatening emergencies only.
- **Signal meter** (`F+2`): signal strength in dBm with a bar, peak hold and a beep whose pitch
  follows the signal. Turn around slowly to find the direction of a person or a 121.5 MHz beacon.
- **Team** (`F+3`, *Menu > Team / Name*): radios with the same team code hear only each other
  on PMR (the code is a CTCSS tone) and talk to each other in short data bursts:
  - **messages**: 12 preset messages (*COME TO ME*, *NEED HELP*, *I AM LOST*, ...) that reach
    the others even when a voice would not be understood;
  - **check**: `*` asks who hears you; team radios answer by themselves with their name, how
    strong your signal reached them and their battery;
  - **who talked**: after PTT the others see your name;
  - **watch** (*Menu > Adv > Watch*): a check every 10-60 minutes, with an alarm when a partner
    stops answering.
- **Clock** (*Menu > Clock*): set once after switching on (or copied from a team partner); listen
  windows then open on the hour on every radio.
- **Status** (*Menu > Status*): battery percent and hours left while listening, with Eco and
  with FM. Below 20 % the radio goes into **battery reserve** (Eco and listen windows).
- **Hands free** (*Menu > Adv > VOX*) and a **roger beep** at the end of each call.
- **Listen windows** (*Menu > Listen*): the receiver sleeps and wakes for 5 or 10 minutes every
  30 or 60 minutes; agree the times with your group.
- **Simple mode** (*Menu > Simple*): only PMR, LOCAL, FM and FAV, and the key lock also locks PTT.
- **SAFE LIMITS** (*Menu > Limits*, on by default): PMR446 is sent at about 0.25 W in narrow FM
  whatever the channel says, and CB is listen only (27 MHz harmonics land in the air and marine
  bands). Switch it **off in an emergency** for full power and CB.
- **Fast scan** that stops on a signal, a **skip list** for noisy channels (`F+8`), a **priority
  channel** (`F+5`), **recently heard** list (`F+4`).
- **FM broadcast radio** (87.5-108 MHz) that keeps watching a radio group in the background and
  switches over when someone talks; with Eco the watch runs in short bursts to save battery.
- **Safe transmit**: sending only on the bands you allow. Air band, FM broadcast, military air,
  406 MHz beacons, marine 70/75/76, AIS and GNSS bands are always blocked.
- **Built-in manual** (`F+6`): 38 topics, from the keys to MAYDAY calls, SOS and alpine distress
  signals, helicopter signals, water, direction finding, cold and heat, and first aid.
- **Flashlight**: on, blink, strobe and SOS with the side key.
- **Settings that survive power cuts**: the survival settings are kept in a CRC-checked record that
  alternates between two flash sectors; channel table updates keep FAV, the skip list and settings.
  The first start also copies the radio's factory calibration to a spare flash sector.

## Keys

| Key | Tap | Hold |
|---|---|---|
| `M` | next group | menu |
| `*` | start / stop scan | |
| `UP` / `DOWN` | channel or step, stops a scan | scan up / down |
| `0`-`9` | type a channel or frequency | |
| `EXIT` | clear typing, back | |
| `F` | shortcut help (then a key) | key lock (PTT too) |
| `SIDE1` | flashlight: on, blink, strobe, SOS, off | monitor (squelch open) |
| `SIDE2` | FM radio on / off | scan |
| `PTT` | talk, only where sending is allowed | |

**F shortcuts:** `F+1` SOS beacon, `F+2` signal meter, `F+3` team, `F+4` heard list,
`F+5` priority, `F+6` manual, `F+7` favourite, `F+8` skip in scan, `F+9` country page,
`F+0` FM radio, `F+UP/DOWN` squelch, `F+*` scan.

## Compatibility

- **Quansheng UV-K5 V3 / UV-K5(8) V3 / UV-K1** with the **PY32F071** MCU: stock firmware 7.00.x,
  "V3" printed above the barcode. Tested on a UV-K5(8) V3 with stock firmware 7.00.16 and
  bootloader 7.00.07.
- **Not** for the original UV-K5 with the DP32G030 MCU (stock firmware 2.x).
  `tools/k5link.py` prints the stock firmware version.
- Programming cable: the usual Kenwood-type K-plug USB cable (CH340 or similar).

## Install

Use `survival-radio.bin` from the [latest release](../../releases/latest), `builds/survival-radio.bin`,
or build it yourself (below).

1. **Back up the radio first**, with the radio switched on normally:
   ```
   pip install pyserial
   python3 tools/k5link.py --port /dev/ttyUSB0 --dump my_radio_eeprom.bin
   ```
   (`COM3` on Windows, `/dev/cu.usbserial-XXXX` on macOS.) Once Survival Radio runs, also save the
   calibration with [UVTools2](https://armel.github.io/uvtools2/) (*Dump Calib*).
2. **Bootloader mode:** switch the radio off, hold `PTT`, switch it on, release `PTT`. The screen
   stays blank and the flashlight comes on.
3. **Flash**, either way:
   - **Browser (Chrome / Edge):** [UVTools2](https://armel.github.io/uvtools2/?mode=flash), select
     `survival-radio.bin`, connect and flash.
   - **Command line:**
     ```
     python3 tools/k5v3flash.py --port /dev/ttyUSB0 builds/survival-radio.bin --dry-run   # link check only
     python3 tools/k5v3flash.py --port /dev/ttyUSB0 builds/survival-radio.bin --yes
     ```
4. Switch the radio off and on again **without** `PTT`. On the first start the firmware writes its
   channel tables (your old memories are replaced) and asks for the country: `UP/DOWN` to choose,
   `M` twice to confirm, `EXIT` keeps the generic *EUROPE* preset.

To go back, flash any other UV-K1 / UV-K5 V3 firmware the same way.

On Linux, give your user access to the cable once, e.g. with a udev rule:
`SUBSYSTEM=="tty", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess"`.

## Sending: read this

The firmware only lets you transmit FM, and only inside bands you switch on in *Menu > Adv*
(`TxPMR`, `TxMAR`, `TxHAM`, `TxCB`, `TxAll`). PMR446 is on by default. The UV-K5 has a removable
antenna and is not type approved for PMR446, so even there it is, strictly, not licence free; SAFE
LIMITS keeps it close to what PMR446 allows. Marine, amateur and many CB uses need a licence. In a
real emergency where life is at risk, the rules of most countries allow any means of calling for help.

The firmware is provided as is, without warranty. You flash it at your own risk. It is not a
certified safety or distress device: carry a proper one (PLB, marine VHF, phone) when it matters.

## Build from source

GitHub Actions builds every push (artifact *survival-radio*); a tag `v*` publishes a release.

```
SURVIVAL_VERSION=v1.0 tools/build.sh        # Docker, output builds/survival-radio.bin
```

Without Docker: in `firmware/`, `cmake --preset Survival && cmake --build --preset Survival` with the
ARM GNU toolchain 13.3, CMake and Ninja on the `PATH` (output `firmware/build/Survival/firmware.bin`).

The manual text is [`design/manual.txt`](design/manual.txt); `python3 tools/manual2c.py` turns it
into `firmware/App/app/survival_manual.c` and checks every row fits the screen. The country data is
in [`firmware/App/app/survival_country.c`](firmware/App/app/survival_country.c).

| Path | |
|---|---|
| `firmware/` | firmware source (F4HWN V3 v4.3.2 tree with the survival edition) |
| `firmware/App/app/survival*.c` | channel groups, country presets, tools, team, manual |
| `tools/build.sh` | Docker build |
| `tools/k5v3flash.py` | bootloader flasher (UVTools2 protocol) |
| `tools/k5link.py` | version check and EEPROM dump |
| `tools/k5screen.py` | LCD capture over the cable (F4HWN screenshot protocol) |
| `tools/manual2c.py` | builds the manual from `design/manual.txt` |
| `tools/logo_text.py` | builds the boot logo |
| `docs/DEVLOG_tr.md` | development log of BMKs Survival Radio (Turkish) |

## Credits

- [DualTachyon](https://github.com/DualTachyon/uv-k5-firmware) for the open re-implementation,
  [egzumer](https://github.com/egzumer/uv-k5-firmware-custom) and [armel / F4HWN](https://github.com/armel/uv-k5-firmware-custom)
  for the firmware this is built on, [muzkr and armel](https://github.com/armel/uv-k1-k5v3-firmware-custom)
  for the PY32F071 port, and armel's [UVTools2](https://armel.github.io/uvtools2/).
- [joaquimorg](https://github.com/joaquimorg/uv-k5-firmware-custom) for the Messenger, whose FSK
  set-up the team packets follow.
- [BMKs Survival Radio](https://github.com/melihkarakelle/bmks-survival-radio) by Melih Karakelle,
  the survival edition this firmware grew from.

## License

Apache License 2.0, like the firmware it is based on. See [LICENSE](LICENSE).
