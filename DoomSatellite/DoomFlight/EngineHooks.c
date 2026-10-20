// Link-time wrappers around the doomgeneric entry points that bound each phase of a DOOM tick (EngineHooks.h). Linked
// with -Wl,--wrap=doomgeneric_Tick -Wl,--wrap=I_FinishUpdate -Wl,--wrap=DG_DrawFrame (CMakeLists.txt). The wrapped
// calls all cross object-file boundaries (DoomEngine.cpp -> doomgeneric.c -> d_main.c -> i_video.c -> DoomEngine.cpp),
// which --wrap requires; a call inside the defining file would bypass the wrapper.
#include <string.h>
#include <zephyr/kernel.h>
#include "DoomSatellite/Components/EngineProfiler/EngineHooks.h"

extern void __real_doomgeneric_Tick(void);
extern void __real_I_FinishUpdate(void);
extern void __real_DG_DrawFrame(void);

static struct engine_hooks_stats stats;
static uint32_t tick_start;
static uint32_t finish_start;
static int in_tick;
static int render_recorded;

static void record(struct engine_hooks_phase* phase, uint32_t cycles) {
    unsigned int key = irq_lock();
    phase->count++;
    phase->sum_cycles += cycles;
    if (cycles > phase->max_cycles) {
        phase->max_cycles = cycles;
    }
    irq_unlock(key);
}

void __wrap_doomgeneric_Tick(void) {
    tick_start = k_cycle_get_32();
    in_tick = 1;
    render_recorded = 0;
    __real_doomgeneric_Tick();
    record(&stats.tick, k_cycle_get_32() - tick_start);
    in_tick = 0;
}

void __wrap_I_FinishUpdate(void) {
    finish_start = k_cycle_get_32();
    // A wipe calls I_FinishUpdate repeatedly inside one tick; only the first call ends the render phase.
    if (in_tick && !render_recorded) {
        record(&stats.render, finish_start - tick_start);
        render_recorded = 1;
    }
    __real_I_FinishUpdate();
}

void __wrap_DG_DrawFrame(void) {
    const uint32_t draw_start = k_cycle_get_32();
    record(&stats.blit, draw_start - finish_start);
    __real_DG_DrawFrame();
    record(&stats.emit, k_cycle_get_32() - draw_start);
}

void engine_hooks_snapshot(struct engine_hooks_stats* out) {
    unsigned int key = irq_lock();
    *out = stats;
    memset(&stats, 0, sizeof(stats));
    irq_unlock(key);
    out->cycles_per_second = sys_clock_hw_cycles_per_sec();
}
