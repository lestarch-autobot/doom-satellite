module DoomCoprocessor {

  # ----------------------------------------------------------------------
  # Symbolic constants for port numbers
  # ----------------------------------------------------------------------

  enum Ports_RateGroups {
    rateGroup1Hz
  }

  @ GenericHub bufferIn/bufferOut port carrying the packed DOOM frames and palettes (same index on DoomFlight)
  constant HUB_FRAME_PORT = 0

  deployment topology DoomCoprocessor {

  # ----------------------------------------------------------------------
  # Instances used in the topology. The DOOM engine, downsampler and frame telemetry run on DoomFlight; this
  # deployment is the remote end of DoomFlight's GenericHub (commands in, events out, packed frames in) and reads the
  # downsampled frames in Python (frameReader).
  # ----------------------------------------------------------------------
    instance cmdDisp
    instance chronoTime
    instance hub
    instance hubComDriver
    instance hubByteStreamAdapter
    instance hubBufferManager
    instance rateGroup1Hz
    instance rateGroupDriver
    instance linuxTimer
    instance frameAdapter
    instance frameReader

  # ----------------------------------------------------------------------
  # Pattern graph specifiers
  # ----------------------------------------------------------------------

    command connections instance cmdDisp

    # Events go to the control node (DoomFlight) over the hub. The hub transport chain (hubByteStreamAdapter,
    # hubBufferManager, hubComDriver) is excluded: its events would re-enter the hub they report on.
    event connections instance hub {
      cmdDisp
      rateGroup1Hz
      frameAdapter
      frameReader
    }

    # No telemetry connections: DoomFlight's TlmPacketizer only knows its own channels, so remote channels sent over
    # hub.tlmOut would be dropped there. frameReader reports its counters on its console instead.

    time connections instance chronoTime

  # ----------------------------------------------------------------------
  # Direct graph specifiers
  # ----------------------------------------------------------------------

    connections RateGroups {
      linuxTimer.CycleOut -> rateGroupDriver.CycleIn

      rateGroupDriver.CycleOut[Ports_RateGroups.rateGroup1Hz] -> rateGroup1Hz.CycleIn
      rateGroup1Hz.RateGroupMemberOut[0] -> hubBufferManager.schedIn
    }

    connections HubCommands {
      hub.cmdDispOut -> cmdDisp.seqCmdBuff
      cmdDisp.seqCmdStatus -> hub.cmdRespIn
    }

    connections Frames {
      # Packed frames and palettes from DoomFlight: hub -> unpack -> Python reader. The unpacked frame is a borrowed
      # view of the hub buffer, valid only during the synchronous call chain; the buffer returns to the hub after.
      hub.bufferOut[HUB_FRAME_PORT] -> frameAdapter.packedIn
      frameAdapter.packedInReturn   -> hub.bufferOutReturn[HUB_FRAME_PORT]
      frameAdapter.frameOut         -> frameReader.frameIn
      frameAdapter.paletteOut       -> frameReader.paletteIn
    }

    connections Hub {
      # Hub -> adapter -> UDP (send)
      hub.toBufferDriver                      -> hubByteStreamAdapter.bufferIn
      hubByteStreamAdapter.bufferInReturn     -> hub.toBufferDriverReturn
      hubByteStreamAdapter.toByteStreamDriver -> hubComDriver.$send

      # UDP -> adapter -> hub (receive)
      hubComDriver.$recv                              -> hubByteStreamAdapter.fromByteStreamDriver
      hubByteStreamAdapter.fromByteStreamDriverReturn -> hubComDriver.recvReturnIn
      hubByteStreamAdapter.bufferOut                  -> hub.fromBufferDriver
      hub.fromBufferDriverReturn                      -> hubByteStreamAdapter.bufferOutReturn
      hubComDriver.ready                              -> hubByteStreamAdapter.byteStreamDriverReady

      # Shared buffer allocation
      hub.allocate            -> hubBufferManager.bufferGetCallee
      hub.deallocate          -> hubBufferManager.bufferSendIn
      hubComDriver.allocate   -> hubBufferManager.bufferGetCallee
      hubComDriver.deallocate -> hubBufferManager.bufferSendIn
    }
  }
}
