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
};

//! Copy the counters accumulated since the previous call and reset them. Safe to call from any thread.
void engine_hooks_snapshot(struct engine_hooks_stats* out);

#ifdef __cplusplus
}
#endif

#endif
