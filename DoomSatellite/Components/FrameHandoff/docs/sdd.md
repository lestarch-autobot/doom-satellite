# Components::FrameHandoff

Moves the consumers of the downsampled DOOM frame off the engine thread.

## Problem

`Doom.RawFrame` is a synchronous hand-off: the engine's `frameOut` call (on the `rateGroupDoom` thread) returns only
after every downstream consumer has finished with the pixels. On DoomFlight those consumers were the 50 row telemetry
writes of `FrameTlmProcessor` and the hub packing of `FrameBufferAdapter`, roughly 3 ms of every 33 ms tick measured on
the Teensy 4.1 (`EngineProfiler.EmitAvgUs`). Any consumer that blocks, such as a network send, stalls the engine.

## Design

`FrameHandoff` is an active component with one internal port. Its input handlers run on the caller's thread and do
only a copy:

- `paletteIn` stores the palette (sent by the engine immediately before each frame).
- `frameIn` copies the frame and the stored palette into one of two slots, publishes that slot and queues one
  `deliver` kick. The kick is `drop` on queue-full: the slot already holds the newest frame.

`deliver` runs on the component's own thread: it takes the published slot, sends `paletteOut` then `frameOut` from it
(the engine's order), and releases the slot. Two slots suffice because the delivery thread holds at most one slot
while the writer always targets the other; a slot that is published but not yet delivered when the next frame arrives
is simply overwritten (latest wins, `FramesOverwritten`). The engine thread never waits on the delivery thread.

The component's priority is set below the engine's in the topology (`instances.fpp`), so delivery consumes only the
budget the engine leaves over. If delivery falls behind, frames are dropped here instead of slowing the engine.

## Telemetry

| Channel | Meaning |
| --- | --- |
| `FramesIn` | Frames accepted on `frameIn` |
| `FramesOut` | Frames delivered on `frameOut` |
| `FramesOverwritten` | Frames replaced before delivery |
| `DeliverMaxUs` | Longest `paletteOut`+`frameOut` delivery during the last second |

Telemetry is written at most once per second, from the delivery thread.

## Constraints

- Frame dimensions must equal `Doom.DOWNSAMPLED_WIDTH` x `Doom.DOWNSAMPLED_HEIGHT` (asserted): the slots are sized
  for the downsampled frame, not the native one.
- `frameIn`/`paletteIn` are not reentrant: one producer thread.
