module Components {

  @ fprime-python
  @ Reads the downsampled DOOM frames and palettes in Python.
  @
  @ Implemented in DoomFrameReader.py through fprime-python: each frameIn call is dispatched to the Python object on
  @ the calling thread (the DOOM rate group) under the interpreter lock, so the handler must stay short. The pixels
  @ are a read-only view of the caller's storage that is valid only during the call.
  passive component DoomFrameReader {

    # ----------------------------------------------------------------------
    # Ports
    # ----------------------------------------------------------------------

    @ Frame to read; width * height palette indices, valid only during the call
    sync input port frameIn: Doom.RawFrame

    @ Palette the frame indices refer to
    sync input port paletteIn: Doom.PaletteSend

    # ----------------------------------------------------------------------
    # Events
    # ----------------------------------------------------------------------

    @ A frame was not read because its geometry and pixel buffer disagree
    event FrameRejected(
      width: U16 @< Frame width
      height: U16 @< Frame height
      $size: FwSizeType @< Pixel buffer size in bytes
    ) severity warning low \
      format "Frame rejected: {}x{} in {} bytes" \
      throttle 5

    # ----------------------------------------------------------------------
    # Telemetry
    # ----------------------------------------------------------------------

    @ Frames read
    telemetry FramesRead: U32

    @ Palettes read
    telemetry PalettesRead: U32

    @ Frame number of the last frame read
    telemetry LastFrame: U32

    @ Mean palette index over the last frame read
    telemetry MeanPixel: F32

    @ Mean brightness (0-255) of the last frame read through the last palette; 0 until a palette arrives
    telemetry MeanBrightness: F32

    # ----------------------------------------------------------------------
    # Standard ports
    # ----------------------------------------------------------------------

    time get port timeCaller
    import Fw.Event
    import Fw.Channel

  }

}
