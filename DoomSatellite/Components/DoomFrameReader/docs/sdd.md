# Components::DoomFrameReader

## 1. Introduction

`Components::DoomFrameReader` is a passive component implemented in Python through
[fprime-python](../../../../lib/fprime-python). It reads the downsampled DOOM frames and palettes that
`Components::FrameBufferAdapter` unpacks from the GenericHub on DoomCoprocessor (DoomFlight runs the engine and sends
a packed copy of every frame over the hub) and reports frame statistics as telemetry. It exists to exercise
a Python component on the raw `Doom.RawFrame` / `Doom.PaletteSend` ports: the FPP model is autocoded to C++ bindings
and a Python base class (`DoomFrameReaderBaseAc`), and `DoomFrameReader.py` subclasses it.

## 2. Requirements

| Requirement | Description | Verification |
| --- | --- | --- |
| DOOM-FRAME-READER-001 | The component shall count each valid frame received on `frameIn` and report the count, the frame number and the mean palette index of the frame as telemetry. | Integration test |
| DOOM-FRAME-READER-002 | The component shall reject a frame whose width or height is zero or whose pixel buffer is smaller than `width * height`, emitting `FrameRejected`. | Inspection |
| DOOM-FRAME-READER-003 | The component shall count each palette received on `paletteIn` and, once a palette has been received, report the mean brightness of each frame through the last palette. | Integration test |
| DOOM-FRAME-READER-004 | The component shall not retain or modify the pixels offered on `frameIn` beyond the call. | Inspection |
| DOOM-FRAME-READER-005 | When the `DOOM_FRAME_SINK` environment variable names a `host:port`, the component shall forward each valid frame and each palette as a UDP datagram (`DFRM` / `DPAL` header, see `DoomFrameReader.py`) for the `doom-satellite-view` viewer, without blocking on the receiver. | Integration test |
| DOOM-FRAME-READER-006 | The component shall print a status line with its counters every 30 frames, since DoomCoprocessor has no telemetry downlink of its own. | Inspection |

## 3. Design

### 3.1 Ports

| Kind | Name | Type | Description |
| --- | --- | --- | --- |
| `sync input` | `frameIn` | `Doom.RawFrame` | Frame to read; the pixels are valid only during the call |
| `sync input` | `paletteIn` | `Doom.PaletteSend` | Palette the frame indices refer to |

### 3.2 Behavior

Each `frameIn` call is dispatched by the generated C++ binding to `DoomFrameReader.frameIn_handler` on the calling
thread (the hub receive path on DoomCoprocessor) after acquiring the interpreter lock. `pixels.getData()` is a
memoryview of the caller's storage; the handler sums it for the mean index and, when a palette has arrived, translates
it through a 256-byte palette-index-to-brightness table (BT.601 luma) for the mean brightness. Both are C-speed
operations on the memoryview so the handler stays well inside the 30 Hz frame period. The optional viewer datagram
is a single non-blocking `sendto` of the header and the `width * height` indices (4 kB for the 80x50 frame).

`paletteIn` rebuilds the brightness table from the 256 RGB triples of `Doom.Palette`, read through the generated
`get_rgb()` (fprime-python binds the inlined-array member `rgb: [768] U8` by value as a list of the packed R,G,B
values).

### 3.3 Events

| Name | Severity | Arguments | Description |
| --- | --- | --- | --- |
| `FrameRejected` | WARNING_LO, throttled to 5 | `width`, `height`, `size` | Frame geometry and pixel buffer disagree |

### 3.4 Telemetry

| Name | Type | Description |
| --- | --- | --- |
| `FramesRead` | `U32` | Frames read |
| `PalettesRead` | `U32` | Palettes read |
| `LastFrame` | `U32` | Frame number of the last frame read |
| `MeanPixel` | `F32` | Mean palette index over the last frame |
| `MeanBrightness` | `F32` | Mean brightness (0-255) of the last frame through the last palette; 0 until a palette arrives |

## 4. Build and run

The component is registered only when fprime-python is available (`DOOM_SATELLITE_FPRIME_PYTHON`, every non-Zephyr
build). The deployment containing it is launched from Python, see `DoomSatellite/DoomCoprocessor/fsw_main.py`.
Integration tests in `DoomSatellite/DoomFlight/test/int/frame_test.py` check `FramesRead` and `PalettesRead` across
the hub.
