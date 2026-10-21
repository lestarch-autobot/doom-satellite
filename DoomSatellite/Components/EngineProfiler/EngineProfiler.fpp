module Components {
  @ Reports the per-phase CPU budget of the DOOM tick measured by the deployment's engine hooks (EngineHooks.h).
  @
  @ Once per schedIn call (1 Hz in DoomFlight) the component snapshots and resets the hook counters and emits, per
  @ phase, the longest and the mean duration over the window in microseconds. The phases partition one tick on the
  @ DOOM rate group thread: render (game logic and software rendering), blit (I_VideoBuffer to DG_ScreenBuffer
  @ copy) and emit (frame emission and everything wired downstream of it); tick is the whole doomgeneric_Tick call.
  passive component EngineProfiler {

    @ Mean of the DOOM thread's own run time per tick (CPU, excludes preemption), microseconds
    telemetry TickCpuAvgUs: U32
    @ Ticks in the window whose wall time was 33-100 ms (steady-state slips)
    telemetry SlipTicks: U32
    @ Ticks in the window whose wall time was >= 100 ms (level-switch class stalls)
    telemetry StallTicks: U32
    @ Mean wall time of slipped ticks, microseconds
    telemetry SlipWallAvgUs: U32
    @ Mean of the DOOM thread's own run time inside slipped ticks, microseconds
    telemetry SlipCpuAvgUs: U32
    @ Mean wall time of stalled ticks, microseconds
    telemetry StallWallAvgUs: U32
    @ Mean DOOM-thread CPU time of ticks >= 100 ms wall (level/demo switch class)
    telemetry StallCpuAvgUs: U32
    @ Mean idle-thread time inside ticks >= 100 ms wall
    telemetry StallIdleAvgUs: U32
    @ Kernel thread table entry for per-slip attribution (index, priority, name); one per second after start
    event ThreadInfo(index: U8, prio: I8, name: string size 24) severity activity high format "thread {} prio {} {}"
    @ Run time of attribution-table thread 00 inside slipped ticks, mean microseconds per slipped tick
    telemetry SlipThr00Us: U32
    @ Run time of attribution-table thread 01 inside slipped ticks, mean microseconds per slipped tick
    telemetry SlipThr01Us: U32
    @ Run time of attribution-table thread 02 inside slipped ticks, mean microseconds per slipped tick
    telemetry SlipThr02Us: U32
    @ Run time of attribution-table thread 03 inside slipped ticks, mean microseconds per slipped tick
    telemetry SlipThr03Us: U32
    @ Run time of attribution-table thread 04 inside slipped ticks, mean microseconds per slipped tick
    telemetry SlipThr04Us: U32
    @ Run time of attribution-table thread 05 inside slipped ticks, mean microseconds per slipped tick
    telemetry SlipThr05Us: U32
    @ Run time of attribution-table thread 06 inside slipped ticks, mean microseconds per slipped tick
    telemetry SlipThr06Us: U32
    @ Run time of attribution-table thread 07 inside slipped ticks, mean microseconds per slipped tick
    telemetry SlipThr07Us: U32
    @ Run time of attribution-table thread 08 inside slipped ticks, mean microseconds per slipped tick
    telemetry SlipThr08Us: U32
    @ Run time of attribution-table thread 09 inside slipped ticks, mean microseconds per slipped tick
    telemetry SlipThr09Us: U32
    @ Run time of attribution-table thread 10 inside slipped ticks, mean microseconds per slipped tick
    telemetry SlipThr10Us: U32
    @ Run time of attribution-table thread 11 inside slipped ticks, mean microseconds per slipped tick
    telemetry SlipThr11Us: U32
    @ Run time of attribution-table thread 12 inside slipped ticks, mean microseconds per slipped tick
    telemetry SlipThr12Us: U32
    @ Run time of attribution-table thread 13 inside slipped ticks, mean microseconds per slipped tick
    telemetry SlipThr13Us: U32
    @ Run time of attribution-table thread 14 inside slipped ticks, mean microseconds per slipped tick
    telemetry SlipThr14Us: U32
    @ Run time of attribution-table thread 15 inside slipped ticks, mean microseconds per slipped tick
    telemetry SlipThr15Us: U32
    @ Run time of attribution-table thread 16 inside slipped ticks, mean microseconds per slipped tick
    telemetry SlipThr16Us: U32
    @ Run time of attribution-table thread 17 inside slipped ticks, mean microseconds per slipped tick
    telemetry SlipThr17Us: U32
    @ Run time of attribution-table thread 18 inside slipped ticks, mean microseconds per slipped tick
    telemetry SlipThr18Us: U32
    @ Run time of attribution-table thread 19 inside slipped ticks, mean microseconds per slipped tick
    telemetry SlipThr19Us: U32
    @ Run time of attribution-table thread 20 inside slipped ticks, mean microseconds per slipped tick
    telemetry SlipThr20Us: U32
    @ Run time of attribution-table thread 21 inside slipped ticks, mean microseconds per slipped tick
    telemetry SlipThr21Us: U32
    @ Run time of attribution-table thread 22 inside slipped ticks, mean microseconds per slipped tick
    telemetry SlipThr22Us: U32
    @ Run time of attribution-table thread 23 inside slipped ticks, mean microseconds per slipped tick
    telemetry SlipThr23Us: U32
    @ Stream frames superseded before the streamer picked them up, this window (from FrameStreamer)
    telemetry SkipCount: U32
    @ Mean age (publish to supersede) of those frames, microseconds
    telemetry SkipAgeAvgUs: U32
    telemetry SkipThr00Us: U32
    telemetry SkipThr01Us: U32
    telemetry SkipThr02Us: U32
    telemetry SkipThr03Us: U32
    telemetry SkipThr04Us: U32
    telemetry SkipThr05Us: U32
    telemetry SkipThr06Us: U32
    telemetry SkipThr07Us: U32
    telemetry SkipThr08Us: U32
    telemetry SkipThr09Us: U32
    telemetry SkipThr10Us: U32
    telemetry SkipThr11Us: U32
    telemetry SkipThr12Us: U32
    telemetry SkipThr13Us: U32
    telemetry SkipThr14Us: U32
    telemetry SkipThr15Us: U32
    telemetry SkipThr16Us: U32
    telemetry SkipThr17Us: U32
    telemetry SkipThr18Us: U32
    telemetry SkipThr19Us: U32
    telemetry SkipThr20Us: U32
    telemetry SkipThr21Us: U32
    telemetry SkipThr22Us: U32
    telemetry SkipThr23Us: U32
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

    event port eventOut

    text event port logTextOut
    @ Time get port
    time get port timeCaller
  }
}
