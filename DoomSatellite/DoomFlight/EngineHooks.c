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

/* Per-slip attribution: run time of every kernel thread across each DOOM tick (CONFIG_SCHED_THREAD_USAGE_ALL) */
#define DOOM_PERIOD_MS 33U
#define SLIP_US (DOOM_PERIOD_MS * 1000U + 1U)
#define STALL_US 100000U
static k_tid_t thread_tids[ENGINE_HOOKS_MAX_THREADS];
static char thread_names[ENGINE_HOOKS_MAX_THREADS][CONFIG_THREAD_MAX_NAME_LEN];
static int thread_prios[ENGINE_HOOKS_MAX_THREADS];
static uint64_t thread_start[ENGINE_HOOKS_MAX_THREADS];
static uint32_t thread_count;
static int idle_index = -1;
static uint64_t self_start;
static uint64_t cycle_self_start;
static uint32_t prev_tick_end;
static uint16_t phase_bins[DOOM_PERIOD_MS];
static int prev_late;
static int tick_catchup;
static uint32_t tick_seq;
static int have_prev_end;

static void collect_thread(const struct k_thread* thread, void* user) {
    (void)user;
    if (thread_count < ENGINE_HOOKS_MAX_THREADS) {
        k_tid_t tid = (k_tid_t)thread;
        const char* name = k_thread_name_get(tid);
        thread_tids[thread_count] = tid;
        strncpy(thread_names[thread_count], (name != NULL) ? name : "?", CONFIG_THREAD_MAX_NAME_LEN - 1);
        thread_prios[thread_count] = k_thread_priority_get(tid);
        if ((name != NULL) && (strncmp(name, "idle", 4) == 0)) {
            idle_index = (int)thread_count;
        }
        thread_count++;
    }
}

void engine_hooks_threads_init(void) {
    if (thread_count == 0) {
        k_thread_foreach_unlocked(collect_thread, NULL);
        stats.thread_count = thread_count;
    }
}

int engine_hooks_thread_info(uint32_t i, const char** name, int* prio) {
    if (i >= thread_count) {
        return 0;
    }
    *name = thread_names[i];
    *prio = thread_prios[i];
    return 1;
}

uint32_t engine_hooks_tick_seq(void) {
    return tick_seq;
}

int engine_hooks_tick_is_catchup(void) {
    return in_tick && tick_catchup;
}

void engine_hooks_sample_threads(uint64_t out[ENGINE_HOOKS_MAX_THREADS]) {
    for (uint32_t i = 0; i < thread_count; i++) {
        k_thread_runtime_stats_t rt;
        out[i] = (k_thread_runtime_stats_get(thread_tids[i], &rt) == 0) ? rt.execution_cycles : 0;
    }
}

void engine_hooks_account_skip(const uint64_t start[ENGINE_HOOKS_MAX_THREADS], uint32_t age_cycles) {
    uint64_t now[ENGINE_HOOKS_MAX_THREADS];
    engine_hooks_sample_threads(now);
    unsigned int key = irq_lock();
    stats.skip_count++;
    stats.skip_age_cycles += age_cycles;
    for (uint32_t i = 0; i < thread_count; i++) {
        stats.skip_thread_cycles[i] += now[i] - start[i];
    }
    irq_unlock(key);
}

static uint64_t thread_cycles(k_tid_t tid) {
    k_thread_runtime_stats_t rt;
    return (k_thread_runtime_stats_get(tid, &rt) == 0) ? rt.execution_cycles : 0;
}

static int attribute_tick(uint32_t tick_cycles, uint32_t wall_cycles, int catchup) {
    const uint32_t wall_us = (uint32_t)(((uint64_t)wall_cycles * 1000000ULL) / sys_clock_hw_cycles_per_sec());
    const uint64_t now_self = thread_cycles(k_current_get());
    const uint64_t self_cycles = now_self - cycle_self_start;
    unsigned int key = irq_lock();
    stats.tick_cpu_cycles += now_self - self_start;
    irq_unlock(key);
    (void)tick_cycles;
    const uint64_t idle_cycles =
        (idle_index >= 0) ? (thread_cycles(thread_tids[idle_index]) - thread_start[idle_index]) : 0;

    if ((wall_us >= STALL_US) || (catchup && (wall_us >= SLIP_US))) {
        key = irq_lock();
        stats.stall_ticks++;
        stats.stall_wall_cycles += wall_cycles;
        stats.stall_cpu_cycles += self_cycles;
        stats.stall_idle_cycles += idle_cycles;
        irq_unlock(key);
    } else if (wall_us >= SLIP_US) {
        uint64_t delta[ENGINE_HOOKS_MAX_THREADS];
        for (uint32_t i = 0; i < thread_count; i++) {
            delta[i] = thread_cycles(thread_tids[i]) - thread_start[i];
        }
        key = irq_lock();
        stats.slip_ticks++;
        stats.slip_wall_cycles += wall_cycles;
        stats.slip_cpu_cycles += self_cycles;
        for (uint32_t i = 0; i < thread_count; i++) {
            stats.slip_thread_cycles[i] += delta[i];
        }
        irq_unlock(key);
    }
    return (wall_us >= SLIP_US);
}

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
    self_start = thread_cycles(k_current_get());
    tick_start = k_cycle_get_32();
    const uint32_t start_ph = (uint32_t)(k_uptime_get() % (int64_t)DOOM_PERIOD_MS);
    /* A catch-up tick is one whose post was already queued: the previous tick outran its period, or this one
     * started without waiting for a post (a normal wait leaves >= 20 ms between ticks; replays and the bursts of
     * higher-priority work between them leave far less). The phase estimate cannot tell how many periods late a
     * replayed cycle is, so the gap test carries the chain through the replay. */
    tick_catchup = have_prev_end &&
                   (prev_late || ((tick_start - prev_tick_end) < (15U * (sys_clock_hw_cycles_per_sec() / 1000U))));
    tick_seq++;
    in_tick = 1;
    render_recorded = 0;
    __real_doomgeneric_Tick();
    const uint32_t tick_end = k_cycle_get_32();
    record(&stats.tick, tick_end - tick_start);
    in_tick = 0;
    /* ActiveRateGroup counts a slip when the next post lands while its handler (this tick) is still running,
     * i.e. when dispatch latency + tick wall exceeds the 33 ms period. Posts come from the 1 kHz driver, so the
     * tick-start millisecond modulo 33 clusters on the post's phase (its mode); ticks replayed back-to-back after
     * a stall (catch-up) are classed with the stall, not as steady-state slips. */
    phase_bins[start_ph]++;
    if (phase_bins[start_ph] >= 60000U) {
        for (uint32_t i = 0; i < DOOM_PERIOD_MS; i++) {
            phase_bins[i] >>= 1;
        }
    }
    uint32_t mode = 0;
    for (uint32_t i = 1; i < DOOM_PERIOD_MS; i++) {
        if (phase_bins[i] > phase_bins[mode]) {
            mode = i;
        }
    }
    const uint32_t latency_ms = (start_ph + DOOM_PERIOD_MS - mode) % DOOM_PERIOD_MS;
    const uint32_t cycles_per_ms = sys_clock_hw_cycles_per_sec() / 1000U;
    const uint32_t lateness = latency_ms * cycles_per_ms + (tick_end - tick_start);
    const int catchup = tick_catchup;
    prev_late = attribute_tick(tick_end - tick_start, have_prev_end ? lateness : 0U, catchup);
    for (uint32_t i = 0; i < thread_count; i++) {
        thread_start[i] = thread_cycles(thread_tids[i]);
    }
    cycle_self_start = thread_cycles(k_current_get());
    prev_tick_end = k_cycle_get_32();
    have_prev_end = 1;
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

/* Native frame hand-off for Components::FrameStreamer (weak: absent when the deployment has no streamer). At this
 * point DG_ScreenBuffer holds the complete 320x200 indices and colors[] the engine palette; the downsampler that runs
 * inside the real DG_DrawFrame packs the screen buffer in place, so the copy has to happen first. */
struct engine_color {
    uint32_t b : 8;
    uint32_t g : 8;
    uint32_t r : 8;
    uint32_t a : 8;
};
extern uint8_t* DG_ScreenBuffer;
extern struct engine_color colors[256];
void frame_streamer_capture(const uint8_t* pixels, const uint8_t* bgra) __attribute__((weak));

void __wrap_DG_DrawFrame(void) {
    const uint32_t draw_start = k_cycle_get_32();
    record(&stats.blit, draw_start - finish_start);
    if (frame_streamer_capture != NULL) {
        frame_streamer_capture(DG_ScreenBuffer, (const uint8_t*)colors);
    }
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
