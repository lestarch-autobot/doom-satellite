# Components::FrameStreamer

Streams the native DOOM frame (`Doom.FRAME_WIDTH` x `Doom.FRAME_HEIGHT` palette indices plus the 768-byte palette) to
a UDP peer from its own thread, latest wins.

## Design

- **Capture (engine thread).** The deployment's link-time hook on `DG_DrawFrame` (`DoomFlight/EngineHooks.c`) calls
  `frame_streamer_capture(pixels, bgra)` before the engine's own draw path downsamples the screen buffer in place.
  `capture` copies the frame into one of two deployment-provided slots (the one not being sent) and the engine palette
  (`colors[256]`, BGRA) into RGB triples, publishes the slot and kicks the `deliver` internal port. A frame that was
  published but not yet picked up is replaced and counted in `FramesSkipped`. `SET_RATE` keeps only every
  divisor-th frame; a disabled stream copies nothing.
- **Send (streamer thread, below the engine).** `deliver` takes the published slot and sends it through a
  `Drv::UdpSocket` as 1 palette chunk + 45 pixel chunks of at most 1472 bytes each (fits one Ethernet frame, no IPv4
  fragmentation). A send failure drops the rest of that frame (`SendErrors`); `SOCK_DISCONNECTED` reopens the socket.
- **Chunk wire format** (big-endian, 20-byte header): magic `"DSF1"` U32, frameSeq U32, width U16, height U16,
  offset U32, length U16, kind U8 (0 pixels, 1 palette), flags U8 (bit 0 = last chunk), then `length` payload bytes.
  Every chunk is self-describing, so a receiver can start mid-stream and tolerate loss (it reassembles by frameSeq and
  offset and drops incomplete frames when a newer one completes).
- **Commands.** `ENABLE`, `SET_RATE(divisor)`, `SET_PEER(address, port)`; all report `StreamState`.
- **Telemetry.** `FramesSent`, `FramesSkipped`, `ChunksSent`, `SendErrors`, `TxTimeMaxUs` (one frame, all chunks),
  `CopyTimeMaxUs` (the engine-thread copy).

## Requirements

| Id | Requirement | Verification |
| --- | --- | --- |
| FS-001 | The engine thread shall spend only the frame copy on the stream; sends run on the streamer thread. | Bench (`CopyTimeMaxUs`, `TxTimeMaxUs`) |
| FS-002 | No datagram shall exceed 1472 bytes. | Inspection (`DATAGRAM_MAX_BYTES`), bench capture |
| FS-003 | A newer frame shall replace an unsent older one (no queue build-up). | `FramesSkipped` on the bench |
| FS-004 | The stream shall be startable/stoppable and rate-limited from the ground. | Commands on the bench |
