module DoomCoprocessor {

  # ----------------------------------------------------------------------
  # Base ID Convention
  # ----------------------------------------------------------------------
  #
  # All Base IDs follow the 8-digit hex format: 0xDSSCCxxx
  #
  # Where:
  #   D   = Deployment digit (2 for this deployment)
  #   SS  = Subtopology digits (00 for main topology, D0 for DoomSubtopology: DoomConfig.fpp BASE_ID 0x2D000000)
  #   CC  = Component digits (00, 01, 02, etc.)
  #   xxx = Reserved for internal component items (events, commands, telemetry)
  #
  # Every id is >= 0x20000000 so DoomFlight's CmdSplitter forwards these opcodes across the hub.

  # ----------------------------------------------------------------------
  # Defaults
  # ----------------------------------------------------------------------

  module Default {
    constant QUEUE_SIZE = 10
    constant STACK_SIZE = 64 * 1024
  }

  # ----------------------------------------------------------------------
  # Active component instances
  # ----------------------------------------------------------------------

  instance cmdDisp: Svc.CommandDispatcher base id 0x20000000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 35

  @ 1 Hz housekeeping
  instance rateGroup1Hz: Svc.ActiveRateGroup base id 0x20007000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 41

  # ----------------------------------------------------------------------
  # Passive component instances
  # ----------------------------------------------------------------------

  instance chronoTime: Svc.ChronoTime base id 0x20001000

  instance hub: Svc.GenericHub base id 0x20002000

  instance hubComDriver: Drv.Udp base id 0x20003000

  instance hubByteStreamAdapter: Drv.ByteStreamBufferAdapter base id 0x20004000

  instance hubBufferManager: Svc.BufferManager base id 0x20005000

  instance rateGroupDriver: Svc.RateGroupDriver base id 0x20008000

  instance linuxTimer: Svc.LinuxTimer base id 0x20009000

  @ Unpacks the frames and palettes DoomFlight sends over the hub (Components.FrameBufferAdapter packedIn side)
  instance frameAdapter: Components.FrameBufferAdapter base id 0x2000A000

  @ Python (fprime-python) reader of the unpacked frames
  instance frameReader: Components.DoomFrameReader base id 0x2000C000

  @ Python (fprime-python) bridge from the viewer's keyboard to the engine's key ports over the hub
  instance keyBridge: Components.DoomKeyBridge base id 0x2000D000

}
