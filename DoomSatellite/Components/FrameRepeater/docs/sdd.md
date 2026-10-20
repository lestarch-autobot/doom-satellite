# Components::FrameRepeater

## 1. Introduction

`Components::FrameRepeater` is a passive component that repeats DOOM frames (`Doom.RawFrame`) and palettes
(`Doom.PaletteSend`) to several consumers on their native port types. It replaces the `Svc::BufferRepeater` that
DoomFlight used on the packed `Fw::Buffer` form of the same data: fanning out the typed ports needs no allocation and
no copy, and keeps the pack/unpack conversion (`Components::FrameBufferAdapter`) confined to the GenericHub boundary.

In DoomSatellite:

- **DoomFlight** repeats each downsampled frame and palette to `frameTlmProcessor` (row and palette telemetry over
  the UART downlink) and to `frameAdapter`, which packs it for the GenericHub link to DoomCoprocessor, where the
  Python `frameReader` consumes it.

## 2. Requirements

| Requirement | Description | Verification |
| --- | --- | --- |
| FRAME-REPEATER-001 | The component shall invoke every connected `frameOut` port, in index order, with the frame number, geometry and pixels received on `frameIn`, without copying the pixels. | Unit test |
| FRAME-REPEATER-002 | The component shall invoke every connected `paletteOut` port, in index order, with the palette received on `paletteIn`. | Unit test |
| FRAME-REPEATER-003 | The component shall skip unconnected output ports. | Unit test |
| FRAME-REPEATER-004 | A consumer redirecting its `Fw::Buffer` argument shall not affect the buffer seen by later consumers or by the caller. | Unit test |
| FRAME-REPEATER-005 | The component shall report the number of frames and palettes repeated as telemetry. | Unit test, integration test |
| FRAME-REPEATER-006 | Each output slot shall be individually enable-able and disable-able, at topology setup and by command; a disabled slot shall be skipped like an unconnected one, and a slot index outside the output arrays shall be rejected with a validation error. | Unit test |

## 3. Design

### 3.1 Ports

| Kind | Name | Type | Description |
| --- | --- | --- | --- |
| `sync input` | `frameIn` | `Doom.RawFrame` | Frame to repeat |
| `sync input` | `paletteIn` | `Doom.PaletteSend` | Palette to repeat |
| `output` | `frameOut` | `[FRAME_REPEATER_OUTPUT_PORTS] Doom.RawFrame` | Repeated frames |
| `output` | `paletteOut` | `[FRAME_REPEATER_OUTPUT_PORTS] Doom.PaletteSend` | Repeated palettes |

`FRAME_REPEATER_OUTPUT_PORTS` (2) is defined in `FrameRepeater.fpp`.

### 3.2 Buffer ownership

`Doom.RawFrame` lends the caller's pixel storage for the duration of the call. The repeater invokes each connected
output in turn with a fresh `Fw::Buffer` view of that storage, so every consumer sees the caller's pixels and none can
redirect another's view. Consumers must treat the pixels as read-only and must not keep the view after returning;
the storage belongs to the upstream component (the hub's receive buffer on DoomFlight, the downsampler's frame on
DoomCoprocessor).

### 3.3 Concurrency

The input ports are `sync`: `frameIn` and `paletteIn` touch no shared state, so each may be driven from its own
thread, but concurrent calls on the same input port are not supported. In both deployments each input is driven from a
single thread (the hub receive thread on DoomFlight, the DOOM rate group on DoomCoprocessor).

### 3.4 Output gate

All slots start enabled. `setOutputEnabled(output, enabled)` sets a slot at topology setup and the `SET_OUTPUT(slot,
setting)` command changes it from the ground (`OutputStateChanged` event; `OutputIndexInvalid` + `VALIDATION_ERROR` for
an index outside the arrays). A disabled slot is skipped exactly like an unconnected one, so a consumer that may block
the producer's thread can be kept out of the loop until an operator wants it. DoomFlight starts its `HUB` slot
disabled: that slot drives the hub's synchronous UDP send from the DOOM rate group thread, and with the coprocessor
unreachable on the link the send blocked the engine indefinitely on the bench.

### 3.5 Telemetry

| Name | Type | Description |
| --- | --- | --- |
| `FramesRepeated` | `U32` | Frames received on `frameIn` and repeated |
| `PalettesRepeated` | `U32` | Palettes received on `paletteIn` and repeated |

## 4. Unit tests

| Test | Requirements |
| --- | --- |
| `Repeat.Frames` | 001, 004, 005 |
| `Repeat.Palettes` | 002, 005 |
| `Repeat.SkipsUnconnectedOutputs` | 003, 005 |
| `Repeat.OutputGate` | 005, 006 |

Integration tests in `DoomSatellite/DoomFlight/test/int/frame_test.py` exercise 005 across the physical hub with DOOM
running on DoomCoprocessor.
