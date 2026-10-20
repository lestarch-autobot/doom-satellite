module Components {

  @ Moves DOOM frame consumers off the engine thread.
  @
  @ frameIn and paletteIn are called synchronously on the producer's (the DOOM rate group's) thread and only copy the
  @ downsampled frame and its palette into one of two slots. The component's own, lower-priority thread then delivers
  @ the most recent slot on frameOut/paletteOut (palette first, as the engine does), so the row telemetry, the hub
  @ adapter and anything else downstream run entirely off the engine's budget. Latest wins: a frame that is still
  @ undelivered when the next one arrives is overwritten and counted in FramesOverwritten; the engine is never blocked.
  active component FrameHandoff {

    @ Downsampled frames from the engine thread; copied, the caller's buffer is not retained
    sync input port frameIn: Doom.RawFrame

    @ Palette sent ahead of each frame on the engine thread; paired with the next frameIn
    sync input port paletteIn: Doom.PaletteSend

    @ Latest frame, delivered on this component's thread
    output port frameOut: Doom.RawFrame

    @ Palette of the delivered frame, sent immediately before it
    output port paletteOut: Doom.PaletteSend

    @ One queued kick per published frame; dropped when the queue is full because the slot already holds the latest frame
    internal port deliver drop

    @ Frames accepted from the engine thread
    telemetry FramesIn: U32

    @ Frames delivered downstream on this component's thread
    telemetry FramesOut: U32

    @ Frames replaced in their slot before they were delivered (latest wins)
    telemetry FramesOverwritten: U32

    @ Longest frameOut/paletteOut delivery of the last second, in microseconds (the downstream cost moved off the engine)
    telemetry DeliverMaxUs: U32

    @ Telemetry port
    telemetry port tlmOut

    @ Time get port
    time get port timeCaller

  }

}
