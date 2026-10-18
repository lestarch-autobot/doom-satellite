# doom-satellite

DOOM as flight software. `DoomFlight` runs the stock [doomgeneric](https://github.com/ozkl/doomgeneric) engine inside
an F Prime topology on a PJRC Teensy 4.1 (NXP i.MX RT1062 + 8 MB PSRAM, Zephyr RTOS); `DoomCoprocessor` is the Linux
side of the `Svc.GenericHub` link. The engine, frame pipeline and YAMCS display come from
[fprime-stress](https://github.com/LeStarch/fprime-stress) (`lib/fprime-stress`); this repository adds the hardware
(PSRAM heap and clock, program-flash WAD filesystem) and the two deployments.

```
Teensy 4.1 (DoomFlight)                                        ground
 rateGroupDoom 30 Hz -> DoomEngine -> FrameDownsampler ---> FrameTlmProcessor -> TlmPacketizer -> CCSDS -> USB CDC
 /lfs/doom1.wad (LittleFS in program flash)   PSRAM: 6 MiB zone + screen buffer        |
                                                                                 fprime-yamcs + doom-display (browser)
```

This guide starts after:

```bash
git clone --recurse-submodules <this repo> doom-satellite && cd doom-satellite
python3 -m venv fprime-venv && . fprime-venv/bin/activate
pip install -r requirements.txt      # from the repository root: it also installs the two project tools below
```

`requirements.txt` installs the F Prime and Zephyr Python dependencies plus two console tools from this tree:
`fprime-get-doom` (WAD fetch, `lib/fprime-stress/tools/fprime-get-doom`) and `doom-satellite-merge-wad` (Teensy WAD
image, `tools/doom-satellite-tools`). Submodules: `lib/fprime` tracks nasa/fprime `devel` plus the pending
[IpCfg socket-header change](https://github.com/nasa/fprime/pull/6122), `lib/fprime-zephyr` the fprime-community
`ota-with-patch` branch, `lib/fprime-stress` main.

## 1. Toolchain and Zephyr modules

- Install the [Zephyr SDK](https://docs.zephyrproject.org/latest/develop/toolchains/zephyr_sdk.html) 1.0.x (arm
  toolchain is enough; `west sdk install -t arm-zephyr-eabi` picks 1.0.1 from the pinned Zephyr v4.4.2) and export `ZEPHYR_SDK_INSTALL_DIR=<sdk dir>` if you did not register it with cmake.
- Fetch the Zephyr modules named in `west.yml` (NXP HAL, CMSIS, picolibc, littlefs); the Zephyr tree itself is the
  `lib/zephyr-workspace/zephyr` submodule:

  ```bash
  west update
  ```

- Flashing needs [`teensy_loader_cli`](https://github.com/PaulStoffregen/teensy_loader_cli) on the machine the Teensy is
  plugged into.

## 2. Get the WAD (fprime-get-doom plugin)

The shareware `doom1.wad` is not committed. `fprime-get-doom` (installed by `requirements.txt`) downloads it from
public mirrors and verifies the v1.9 size and SHA-256:

```bash
fprime-get-doom -o doom1.wad
```

(`fprime-get-doom --help` lists `--mirror`, `--force` and `--print-sha256`.)

## 3. Build DoomFlight (Teensy 4.1)

`settings.ini` makes `zephyr` the default toolchain and `teensy41` the default board (`-DBOARD=<other>` to override):

```bash
fprime-util generate
fprime-util build -j"$(nproc)"
```

Outputs: `build-artifacts/zephyr.hex` (firmware, linked into the first 1 MB of the 8 MB program flash) and the ground
dictionary `build-artifacts/zephyr/fprime-zephyr-deployment/dict/DoomFlightTopologyDictionary.json`.

The WAD lives in a LittleFS partition in the rest of the program flash that `teensy_loader_cli` can program
(`boards/teensy41.overlay`: `wad` partition at 0x100000, 6912 KiB, ending at the loader's 0x7C0000 limit), mounted at
`/lfs`; the engine opens `/lfs/doom1.wad` at boot. Merge the filesystem into the hex
so one flash pass programs both:

```bash
doom-satellite-merge-wad build-artifacts/zephyr.hex doom1.wad DoomFlight-teensy41.hex
```

The WAD only needs to be flashed once; a later firmware-only `zephyr.hex` leaves the partition intact. With doom1.wad
in place about 2.7 MB of `/lfs` is free for files uploaded from the ground (`Svc.FileUplink` writes under `/lfs`), enough
for PWADs but not for the retail `DOOM.WAD`/`DOOM2.WAD` (12-14 MB). The engine opens one WAD, `/lfs/doom1.wad`, at
boot: to play another, stop DOOM (`Doom.Stop`), upload the file as `/lfs/doom1.wad` and power-cycle the board.

## 4. Flash

Nothing may hold the Teensy's serial port while flashing (stop any ground software first). Put the board in HalfKay
(press the program button, or open its CDC port at 134 baud: `Zephyr.ZephyrTouchReset` reboots into the bootloader)
and program it:

```bash
teensy_loader_cli --mcu=TEENSY41 -w -v DoomFlight-teensy41.hex
```

The board re-enumerates as a USB CDC ACM device (typically `/dev/ttyACM0`, 115200 baud) carrying both the Zephyr
console and the F Prime uplink/downlink. DOOM auto-starts (`doomAutoStart` in `DoomFlight/Main.cpp`) once the WAD is
read; `DoomSubtopology.doom.State` reports `RUNNING`.

## 5. Run the ground system and play

YAMCS is the supported ground system. Install `fprime-yamcs` (bundles its own JRE) in the venv and start it from the
repository root; `fprime-gds.yml` there carries the whole configuration (UART on `/dev/ttyACM0` at 115200, the
dictionary above, the DOOM display web extension from fprime-stress, and the frame channels kept realtime-only since
they are a stream that should not be archived). Command-line flags override the file, e.g. `--uart-device`:

```bash
pip install fprime-yamcs
fprime-yamcs                       # or: fprime-yamcs --uart-device /dev/ttyACM1
```

Open http://localhost:8090, click the red **DOOM** button in the lower-right corner, click the canvas to focus it and
play with WASD / arrow keys, Space (use), Ctrl (fire), Tab (automap), Escape/Enter (menus). The panel's Start/Stop,
Reset and Record buttons map to the `Doom.Start` / `Doom.Stop` / `Doom.Reset` commands and `.seq` recording; key
presses are `Doom.KeyDown` / `Doom.KeyUp` commands restricted to the `Doom.DoomKey` enumeration (no `y`, so Quit
cannot be confirmed from the ground). Useful channels: `DoomSubtopology.doom.FrameCount` (measure the true frame rate
from its delta, the nominal `FrameRateHz` channel is virtual), `DoomSubtopology.frameTlmProcessor.FramesDropped`,
`DoomFlight.rateGroupDoom.RgCycleSlips`.

The engine renders at 30 Hz, but the ground sees complete frames at the rate of `rateGroupTlm` (`TLM_RATE_HZ` in
`DoomSatellite/DoomFlight/Top/DoomFlightTopology.cpp`, 2 Hz): every frame is 51 telemetry packets (palette + 50 rows),
and on the Teensy 5 Hz already costs the engine a quarter of its ticks while 10 Hz and up trip the health ping on
`rateGroupDoom`. 1 Hz is the slip-free setting; 2 Hz trades ~300 `rateGroupDoom` cycle slips a minute for the rate.

`fprime-gds` reads the same `fprime-gds.yml` (ignoring the `yamcs-*` keys with a warning) and works for commanding and
telemetry without the display: `fprime-gds`.

## 6. DoomCoprocessor (Linux, optional)

The coprocessor terminates the `Svc.GenericHub` UDP link over the Teensy's Ethernet (defaults: flight 192.168.11.1,
coprocessor 192.168.11.2, ports 50555/50556; see `DoomSatellite/DoomCoprocessor/Main.cpp -h`). Native build:

```bash
fprime-util generate native && fprime-util build native -j"$(nproc)"
build-artifacts/Linux/DoomSatellite_DoomCoprocessor/bin/DoomSatellite_DoomCoprocessor -a <teensy address>
```

## 7. Tests

```bash
(cd lib/fprime-stress/Doom && fprime-util check -j"$(nproc)" --recursive)      # component unit tests
pytest --dictionary build-artifacts/zephyr/fprime-zephyr-deployment/dict/DoomFlightTopologyDictionary.json \
       DoomSatellite/DoomFlight/test/int/frame_test.py                          # against a running fprime-gds
```

## Layout

| Path | Contents |
| --- | --- |
| `DoomSatellite/DoomFlight/` | Teensy deployment: topology, `Main.cpp`, `PsramHeap.c` (libc malloc >= 16 KiB served from PSRAM), `PsramClock.c` (FlexSPI2 at 120 MHz), `psram_sections.ld`, integration tests |
| `DoomSatellite/DoomCoprocessor/` | Linux hub endpoint |
| `DoomSatellite/Components/` | Project components (hub frame adapter, null parameter DB, fatal handler) |
| `boards/` | Zephyr board overlays and Kconfig fragments (`teensy41`, `adafruit_feather_rp2040`) |
| `fprime-gds.yml`, `settings.ini` | Ground (fprime-yamcs / fprime-gds) and build (toolchain, default board) configuration |
| `tools/doom-satellite-tools/` | `doom-satellite-merge-wad`: LittleFS WAD image merged into the Teensy hex |
| `lib/fprime-stress` | DOOM engine component, frame pipeline, `fprime-get-doom`, YAMCS `doom-display` |
| `lib/fprime`, `lib/fprime-zephyr`, `lib/zephyr-workspace` | F Prime, its Zephyr OSAL/drivers, Zephyr |
