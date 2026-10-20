module DoomFlight {

  # ----------------------------------------------------------------------
  # Symbolic constants for port numbers
  # ----------------------------------------------------------------------

  enum Ports_RateGroups {
    rateGroup10Hz
    rateGroup1Hz
    rateGroupDoom
    rateGroupTlm
  }

  @ frameRepeater output slots: the local row telemetry and the hub packer
  enum Ports_FrameRepeater {
    LOCAL
    HUB
  }

  @ GenericHub bufferIn/bufferOut port carrying the packed DOOM frames and palettes (same index on DoomCoprocessor)
  constant HUB_FRAME_PORT = 0

  deployment topology DoomFlight {

  # ----------------------------------------------------------------------
  # Subtopology imports
  # ----------------------------------------------------------------------
    import CdhCore.Subtopology
    import ComCcsds.Subtopology

  # ----------------------------------------------------------------------
  # DoomSubtopology instances: the engine, its buffer manager, the downsampler and the frame telemetry run here
  # ----------------------------------------------------------------------
    instance DoomSubtopology.doom
    instance DoomSubtopology.doomBufferManager
    instance DoomSubtopology.frameDownsampler
    instance DoomSubtopology.frameTlmProcessor

  # ----------------------------------------------------------------------
  # Instances used in the topology
  # ----------------------------------------------------------------------
    instance chronoTime
    instance rateGroup10Hz
    instance rateGroup1Hz
    instance rateGroupDoom
    instance rateGroupTlm
    instance fileUplink
    instance rateGroupDriver
    instance timer
    instance comDriver
    instance nullPrmDb
    instance touchReset
    instance cmdSplitter
    instance hub
    instance hubComDriver
    instance hubByteStreamAdapter
    instance hubBufferManager
    instance frameRepeater
    instance frameAdapter
    instance engineProfiler

  # ----------------------------------------------------------------------
  # Pattern graph specifiers
  # ----------------------------------------------------------------------

    command connections instance CdhCore.cmdDisp
    event connections instance CdhCore.events
    text event connections instance CdhCore.textLogger
    health connections instance CdhCore.$health
    time connections instance chronoTime
    telemetry connections instance CdhCore.tlmSend
    param connections instance nullPrmDb

  # ----------------------------------------------------------------------
  # Telemetry packets
  # ----------------------------------------------------------------------

  include "DoomFlightPackets.fppi"

  # ----------------------------------------------------------------------
  # Direct graph specifiers
  # ----------------------------------------------------------------------

    connections ComCcsds_CdhCore {
      # Core events and telemetry to communication queue
      CdhCore.events.PktSend -> ComCcsds.comQueue.comPacketQueueIn[ComCcsds.Ports_ComPacketQueue.EVENTS]
      CdhCore.tlmSend.PktSend -> ComCcsds.comQueue.comPacketQueueIn[ComCcsds.Ports_ComPacketQueue.TELEMETRY]

      # Router to command splitter: local opcodes to the command dispatcher, remote opcodes to the hub
      ComCcsds.fprimeRouter.commandOut -> cmdSplitter.CmdBuff
      cmdSplitter.LocalCmd[0]          -> CdhCore.cmdDisp.seqCmdBuff
      CdhCore.cmdDisp.seqCmdStatus     -> cmdSplitter.seqCmdStatus
      cmdSplitter.forwardSeqCmdStatus  -> ComCcsds.fprimeRouter.cmdResponseIn
    }

    connections Communications {
      # ComDriver buffer allocations
      comDriver.allocate      -> ComCcsds.commsBufferManager.bufferGetCallee
      comDriver.deallocate    -> ComCcsds.commsBufferManager.bufferSendIn

      # ComDriver <-> ComStub (Uplink)
      comDriver.$recv                     -> ComCcsds.comStub.drvReceiveIn
      ComCcsds.comStub.drvReceiveReturnOut -> comDriver.recvReturnIn

      # ComStub <-> ComDriver (Downlink)
      ComCcsds.comStub.drvSendOut      -> comDriver.$send
      comDriver.ready         -> ComCcsds.comStub.drvConnected
    }

    connections FileUplink {
      # Uplinked file packets (the WAD) are written under /lfs
      ComCcsds.fprimeRouter.fileOut -> fileUplink.bufferSendIn
      fileUplink.bufferSendOut      -> ComCcsds.fprimeRouter.fileBufferReturnIn
    }

    connections HubRemote {
      # Commands for the remote deployment and their responses
      cmdSplitter.RemoteCmd -> hub.cmdDispIn
      hub.cmdRespOut        -> ComCcsds.fprimeRouter.cmdResponseIn

      # Remote events join the local downlink
      hub.eventOut -> CdhCore.events.LogRecv
    }

    connections HubTelemetry {
      # Remote telemetry is packetized for the local downlink
      hub.tlmOut -> CdhCore.tlmSend.TlmRecv
    }

    connections FramePipeline {
      # Synchronous on the rateGroupDoom thread: engine -> downsampler -> repeater -> row and palette telemetry,
      # plus a packed copy of each frame and palette to DoomCoprocessor over the hub
      DoomSubtopology.doom.frameOut               -> DoomSubtopology.frameDownsampler.frameIn
      DoomSubtopology.doom.paletteOut             -> DoomSubtopology.frameDownsampler.paletteIn
      DoomSubtopology.frameDownsampler.frameOut   -> frameRepeater.frameIn
      DoomSubtopology.frameDownsampler.paletteOut -> frameRepeater.paletteIn
      frameRepeater.frameOut[Ports_FrameRepeater.LOCAL]   -> DoomSubtopology.frameTlmProcessor.frameIn
      frameRepeater.paletteOut[Ports_FrameRepeater.LOCAL] -> DoomSubtopology.frameTlmProcessor.paletteIn
      frameRepeater.frameOut[Ports_FrameRepeater.HUB]     -> frameAdapter.frameIn
      frameRepeater.paletteOut[Ports_FrameRepeater.HUB]   -> frameAdapter.paletteIn
      frameAdapter.packedOut             -> hub.bufferIn[HUB_FRAME_PORT]
      hub.bufferInReturn[HUB_FRAME_PORT] -> frameAdapter.packedOutReturn
    }

    connections Hub {
      # Hub -> adapter -> UDP (send)
      hub.toBufferDriver                    -> hubByteStreamAdapter.bufferIn
      hubByteStreamAdapter.bufferInReturn   -> hub.toBufferDriverReturn
      hubByteStreamAdapter.toByteStreamDriver -> hubComDriver.$send

      # UDP -> adapter -> hub (receive)
      hubComDriver.$recv                                -> hubByteStreamAdapter.fromByteStreamDriver
      hubByteStreamAdapter.fromByteStreamDriverReturn   -> hubComDriver.recvReturnIn
      hubByteStreamAdapter.bufferOut                    -> hub.fromBufferDriver
      hub.fromBufferDriverReturn                        -> hubByteStreamAdapter.bufferOutReturn
      hubComDriver.ready                                -> hubByteStreamAdapter.byteStreamDriverReady

      # Shared buffer allocation
      hub.allocate            -> hubBufferManager.bufferGetCallee
      hub.deallocate          -> hubBufferManager.bufferSendIn
      hubComDriver.allocate   -> hubBufferManager.bufferGetCallee
      hubComDriver.deallocate -> hubBufferManager.bufferSendIn
    }

    connections RateGroups {
      # timer to drive rate group
      timer.CycleOut -> rateGroupDriver.CycleIn

      # High rate (10Hz) rate group
      rateGroupDriver.CycleOut[Ports_RateGroups.rateGroup10Hz] -> rateGroup10Hz.CycleIn
      rateGroup10Hz.RateGroupMemberOut[0] -> comDriver.schedIn

      # Slow rate (1Hz) rate group
      rateGroupDriver.CycleOut[Ports_RateGroups.rateGroup1Hz] -> rateGroup1Hz.CycleIn
      rateGroup1Hz.RateGroupMemberOut[0] -> ComCcsds.comQueue.run
      rateGroup1Hz.RateGroupMemberOut[1] -> CdhCore.$health.Run
      rateGroup1Hz.RateGroupMemberOut[2] -> ComCcsds.commsBufferManager.schedIn
      rateGroup1Hz.RateGroupMemberOut[3] -> engineProfiler.schedIn
      rateGroup1Hz.RateGroupMemberOut[4] -> ComCcsds.aggregator.timeout
      rateGroup1Hz.RateGroupMemberOut[5] -> CdhCore.Subtopology.eventsRun
      rateGroup1Hz.RateGroupMemberOut[6] -> hubBufferManager.schedIn
      rateGroup1Hz.RateGroupMemberOut[7] -> DoomSubtopology.doomBufferManager.schedIn

      # DOOM rate group (DOOM_RATE_HZ in DoomFlightTopology.cpp): one engine tick per cycle
      rateGroupDriver.CycleOut[Ports_RateGroups.rateGroupDoom] -> rateGroupDoom.CycleIn
      rateGroupDoom.RateGroupMemberOut[0] -> DoomSubtopology.doom.schedIn

      rateGroupDriver.CycleOut[Ports_RateGroups.rateGroupTlm] -> rateGroupTlm.CycleIn
      rateGroupTlm.RateGroupMemberOut[0] -> CdhCore.tlmSend.Run
    }

    connections DoomFlight {

    }

  }

}
