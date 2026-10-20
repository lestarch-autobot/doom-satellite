module DoomFlight {

  # ----------------------------------------------------------------------
  # Base ID Convention
  # ----------------------------------------------------------------------
  #
  # All Base IDs follow the 8-digit hex format: 0xDSSCCxxx
  #
  # Where:
  #   D   = Deployment digit (1 for this deployment)
  #   SS  = Subtopology digits (00 for main topology, 01-05 for subtopologies)
  #   CC  = Component digits (00, 01, 02, etc.)
  #   xxx = Reserved for internal component items (events, commands, telemetry)
  #

  # ----------------------------------------------------------------------
  # Defaults
  # ----------------------------------------------------------------------

  module Default {
    constant QUEUE_SIZE = 10
    constant STACK_SIZE = 8 * 1024 # Must match prj.conf thread stack size
  }

  # ----------------------------------------------------------------------
  # Active component instances
  # ----------------------------------------------------------------------

  instance rateGroup10Hz: Svc.ActiveRateGroup base id 0x10001000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 3

  instance rateGroup1Hz: Svc.ActiveRateGroup base id 0x10002000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 4

  @ One doomgeneric tick per cycle at DOOM_RATE_HZ (DoomFlightTopology.cpp). Measured on the Teensy 4.1
  @ with the engine in 90 MHz QSPI PSRAM, native 320x200: the DOOM thread needs 84% of the CPU at 35 Hz
  @ and 68-70% at 30 Hz, and the downlink, telemetry and event threads need ~15%, so 35 Hz only fits by
  @ starving them; 30 Hz leaves ~18% idle.
  @ Priority: lowest. A long tick then runs in the time left over by command, event, telemetry and
  @ downlink service instead of stalling them. The 1 Hz telemetry burst still costs this group ~100 ms
  @ a second (the Zephyr UART driver busy-waits in uart_poll_out while USB CDC-ACM drains).
  @ Queue: 60 cycles (2 s at 30 Hz). The health ping is queued behind the pending cycles, and a full
  @ queue drops it (PingIn is a drop port), so the queue must hold the longest tick plus the burst: a level
  @ load measures up to 1.7 s (RgMaxTime), i.e. ~50 cycles. The engine paces game time per tick, so queued
  @ cycles replay deterministically; only the queue size bounds the wall-clock lag.
  instance rateGroupDoom: Svc.ActiveRateGroup base id 0x10003000 \
    queue size 60 \
    stack size Default.STACK_SIZE \
    priority 9

  @ Ticks the telemetry packetizer at the DOOM rate so one complete frame reaches the ground per engine tick. Kept off
  @ rateGroupDoom on purpose: that group's 60-deep queue replays every tick missed during a level/demo stall, which
  @ would turn into a burst of packetizer runs; here the queue is shallow and ActiveRateGroup drops the excess cycles.
  @ The group and tlmSend sit above the DOOM thread so each run is bounded work stolen from the engine, never a backlog.
  instance rateGroupTlm: Svc.ActiveRateGroup base id 0x10005000 \
    queue size 2 \
    stack size Default.STACK_SIZE \
    priority 7

  @ Receives files from the ground (the DOOM WAD) into the LittleFS mount at /lfs
  instance fileUplink: Svc.FileUplink base id 0x10004000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 6


  # ----------------------------------------------------------------------
  # Queued component instances
  # ----------------------------------------------------------------------


  # ----------------------------------------------------------------------
  # Passive component instances
  # ----------------------------------------------------------------------

  instance chronoTime: Zephyr.ZephyrTime base id 0x10010000

  instance rateGroupDriver: Svc.RateGroupDriver base id 0x10011000

  instance timer: Zephyr.ZephyrRateDriver base id 0x10013000

  instance comDriver: Zephyr.ZephyrUartDriver base id 0x10014000

  instance nullPrmDb: Components.NullPrmDb base id 0x10015000

  instance touchReset: Zephyr.ZephyrTouchReset base id 0x10016000

  instance hub: Svc.GenericHub base id 0x10017000

  instance hubComDriver: Drv.Udp base id 0x10018000

  instance hubByteStreamAdapter: Drv.ByteStreamBufferAdapter base id 0x10019000

  instance hubBufferManager: Svc.BufferManager base id 0x1001A000

  instance cmdSplitter: Svc.CmdSplitter base id 0x1001B000

  @ Repeats each downsampled frame and palette to the row telemetry processor and to the hub packer
  instance frameRepeater: Components.FrameRepeater base id 0x1001C000

  @ Packs the repeated frames and palettes into hub buffers for DoomCoprocessor
  instance frameAdapter: Components.FrameBufferAdapter base id 0x1001D000

}
