module Components {
  @ Reports the per-phase CPU budget of the DOOM tick measured by the deployment's engine hooks (EngineHooks.h).
  @
  @ Once per schedIn call (1 Hz in DoomFlight) the component snapshots and resets the hook counters and emits, per
  @ phase, the longest and the mean duration over the window in microseconds. The phases partition one tick on the
  @ DOOM rate group thread: render (game logic and software rendering), blit (I_VideoBuffer to DG_ScreenBuffer
  @ copy) and emit (frame emission and everything wired downstream of it); tick is the whole doomgeneric_Tick call.
  passive component EngineProfiler {

    @ Snapshot and report the window
    sync input port schedIn: Svc.Sched

    @ Ticks completed in the window
    telemetry Ticks: U32

    @ Reset cause register (hwinfo) of this boot, as reported by Main
    telemetry ResetCause: U32

    @ CPU share over the last window of the idle thread, percent (CONFIG_THREAD_RUNTIME_STATS)
    telemetry CpuIdlePct: U8











    @ Longest doomgeneric_Tick in the window, microseconds
    telemetry TickMaxUs: U32
    @ Mean doomgeneric_Tick in the window, microseconds
    telemetry TickAvgUs: U32

    @ Longest game logic + render phase in the window, microseconds
    telemetry RenderMaxUs: U32
    @ Mean game logic + render phase in the window, microseconds
    telemetry RenderAvgUs: U32

    @ Longest I_VideoBuffer -> DG_ScreenBuffer blit in the window, microseconds
    telemetry BlitMaxUs: U32
    @ Mean I_VideoBuffer -> DG_ScreenBuffer blit in the window, microseconds
    telemetry BlitAvgUs: U32

    @ Longest DG_DrawFrame (frame emission and downstream pipeline) in the window, microseconds
    telemetry EmitMaxUs: U32
    @ Mean DG_DrawFrame (frame emission and downstream pipeline) in the window, microseconds
    telemetry EmitAvgUs: U32

    @ Telemetry port
    telemetry port tlmOut
    @ Time get port
    time get port timeCaller
  }
}
