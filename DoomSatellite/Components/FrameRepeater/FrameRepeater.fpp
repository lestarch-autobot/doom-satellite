module Components {

  @ Number of outputs of each FrameRepeater output port array
  constant FRAME_REPEATER_OUTPUT_PORTS = 2

  @ Repeats DOOM frames and palettes to several consumers on their native port types.
  @
  @ Doom.RawFrame lends the caller's pixel storage for the duration of the call, so the frame is fanned out by
  @ invoking every connected frameOut port in index order with the same pixels: nothing is copied or allocated and
  @ consumers must treat the pixels as read-only. Palettes are passed by value.
  @
  @ The handlers keep no shared state between frameIn and paletteIn, so each may be driven from its own thread;
  @ concurrent calls on the same input port are not supported.
  @
  @ Each output slot can be disabled (setOutputEnabled at topology setup, SET_OUTPUT from the ground): a disabled slot
  @ is skipped like an unconnected one, which keeps a consumer that may block (the hub's synchronous UDP send when its
  @ peer is unreachable) out of the frame producer's thread until an operator enables it.
  passive component FrameRepeater {

    # ----------------------------------------------------------------------
    # Ports
    # ----------------------------------------------------------------------

    @ Frame to repeat
    sync input port frameIn: Doom.RawFrame

    @ Palette to repeat
    sync input port paletteIn: Doom.PaletteSend

    @ Repeated frames: every connected port is invoked, in index order, with the pixels offered on frameIn
    output port frameOut: [FRAME_REPEATER_OUTPUT_PORTS] Doom.RawFrame

    @ Repeated palettes: every connected port is invoked, in index order
    output port paletteOut: [FRAME_REPEATER_OUTPUT_PORTS] Doom.PaletteSend

    # ----------------------------------------------------------------------
    # Commands
    # ----------------------------------------------------------------------

    @ Enable or disable one output slot (frameOut[output] and paletteOut[output])
    sync command SET_OUTPUT(
                             slot: U8 @< Output slot, 0 to FRAME_REPEATER_OUTPUT_PORTS - 1
                             setting: Fw.Enabled @< ENABLED repeats to the slot, DISABLED skips it
                           )

    # ----------------------------------------------------------------------
    # Events
    # ----------------------------------------------------------------------

    @ An output slot was enabled or disabled
    event OutputStateChanged(
                              slot: U8 @< Output slot
                              setting: Fw.Enabled @< New state
                            ) \
      severity activity high \
      format "FrameRepeater output {} set to {}"

    @ SET_OUTPUT named a slot outside the output port arrays
    event OutputIndexInvalid(
                              slot: U8 @< Rejected slot
                            ) \
      severity warning low \
      format "FrameRepeater output {} does not exist"

    # ----------------------------------------------------------------------
    # Telemetry
    # ----------------------------------------------------------------------

    @ Frames received on frameIn and repeated
    telemetry FramesRepeated: U32

    @ Palettes received on paletteIn and repeated
    telemetry PalettesRepeated: U32

    # ----------------------------------------------------------------------
    # Standard ports
    # ----------------------------------------------------------------------

    @ Command receive port
    command recv port cmdIn

    @ Command registration port
    command reg port cmdRegOut

    @ Command response port
    command resp port cmdResponseOut

    @ Event port
    event port eventOut

    @ Text event port
    text event port textEventOut

    @ Telemetry port
    telemetry port tlmOut

    @ Time get port
    time get port timeCaller

  }

}
