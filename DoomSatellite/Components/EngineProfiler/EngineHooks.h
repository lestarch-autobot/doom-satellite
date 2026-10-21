// ======================================================================
// \title  EngineHooks.h
// \brief  Per-phase timing of a DOOM tick, collected by link-time wrappers around the engine entry points
//
// The deployment wraps doomgeneric_Tick, I_FinishUpdate and DG_DrawFrame (-Wl,--wrap, see DoomFlight/EngineHooks.c)
// and records how long each phase of a tick takes. The engine (doomgeneric) and Doom::DoomEngine are untouched:
// the wrappers only timestamp and forward. Components::EngineProfiler reads and resets the counters once a second.
//
// Phases of one tick (all on the DOOM rate group thread):
//   tick   doomgeneric_Tick() entry to return: game logic + render + blit + emit
//   render tick entry to the first I_FinishUpdate() of the tick: game logic and software rendering into I_VideoBuffer
//   blit   I_FinishUpdate() entry to DG_DrawFrame() entry: the I_VideoBuffer -> DG_ScreenBuffer copy
//   emit   DG_DrawFrame() entry to return: Doom::DoomEngine frame emission and everything wired downstream of it
// ======================================================================
#ifndef Components_EngineHooks_H
#define Components_EngineHooks_H

#include <stdint.h>
#define ENGINE_HOOKS_MAX_THREADS 24

#ifdef __cplusplus
extern "C" {
#endif

struct engine_hooks_phase {
    uint32_t count;      //!< Samples in the window
    uint32_t max_cycles; //!< Longest sample, CPU cycles
    uint64_t sum_cycles; //!< Sum of samples, CPU cycles
};

struct engine_hooks_stats {
    struct engine_hooks_phase tick;
    struct engine_hooks_phase render;
    struct engine_hooks_phase blit;
    struct engine_hooks_phase emit;
    uint32_t cycles_per_second; //!< CPU cycle counter rate
    uint64_t tick_cpu_cycles;   //!< Sum of the DOOM thread's own run time over all ticks in the window
    uint32_t slip_ticks;        //!< Ticks with wall time in [slip, stall)
    uint32_t stall_ticks;       //!< Ticks with wall time >= stall (level switch class)
    uint64_t slip_wall_cycles;  //!< Sum of wall time of slipped ticks
    uint64_t slip_cpu_cycles;   //!< Sum of the DOOM thread's own run time inside slipped ticks
    uint64_t stall_wall_cycles; //!< Sum of wall time of stalled ticks
    uint64_t stall_cpu_cycles;  //!< Sum of the DOOM thread's own CPU time inside stalled ticks
    uint64_t stall_idle_cycles; //!< Sum of idle-thread time inside stalled ticks
    uint32_t thread_count;      //!< Threads in the attribution table
    uint64_t slip_thread_cycles[ENGINE_HOOKS_MAX_THREADS]; //!< Run time of each table thread inside slipped ticks
};
//! Enumerates the kernel threads once (call after the topology has started) for per-slip attribution
void engine_hooks_threads_init(void);
//! Name and priority of table thread i; returns 0 if i is out of range
int engine_hooks_thread_info(uint32_t i, const char** name, int* prio);

//! Copy the counters accumulated since the previous call and reset them. Safe to call from any thread.
void engine_hooks_snapshot(struct engine_hooks_stats* out);

#ifdef __cplusplus
}
#endif

#endif
