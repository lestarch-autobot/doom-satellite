// ======================================================================
// \title  EngineProfiler.cpp
// \brief  cpp file for EngineProfiler component implementation class
// ======================================================================

#include "DoomSatellite/Components/EngineProfiler/EngineProfiler.hpp"

#include <zephyr/kernel.h>

namespace Components {

EngineProfiler::EngineProfiler(const char* const compName) : EngineProfilerComponentBase(compName), m_cpuPrev{}, m_cpuPrevTotal(0), m_threadInfoNext(0) {}

EngineProfiler::~EngineProfiler() {}

U32 EngineProfiler::toUs(U64 cycles, U32 cyclesPerSecond) {
    FW_ASSERT(cyclesPerSecond > 0);
    return static_cast<U32>((cycles * 1000000ULL) / cyclesPerSecond);
}

U32 EngineProfiler::s_resetCause = 0;


void EngineProfiler::setResetCause(U32 cause) {
    s_resetCause = cause;
}

void EngineProfiler::schedIn_handler(FwIndexType portNum, U32 context) {
    struct engine_hooks_stats stats;
    engine_hooks_snapshot(&stats);
    const U32 hz = stats.cycles_per_second;

    this->tlmWrite_Ticks(stats.tick.count);
    this->tlmWrite_ResetCause(s_resetCause);
    k_thread_runtime_stats_t all;
    if (k_thread_runtime_stats_all_get(&all) == 0) {
        const U64 totalDelta = all.total_cycles - this->m_cpuPrevTotal;
        const U64 idleDelta = all.idle_cycles - this->m_cpuPrev[0];
        this->m_cpuPrevTotal = all.total_cycles;
        this->m_cpuPrev[0] = all.idle_cycles;
        this->tlmWrite_CpuIdlePct((totalDelta > 0) ? static_cast<U8>((idleDelta * 100U) / totalDelta) : 0);
    }

    const struct engine_hooks_phase* const phases[] = {&stats.tick, &stats.render, &stats.blit, &stats.emit};
    U32 maxUs[FW_NUM_ARRAY_ELEMENTS(phases)];
    U32 avgUs[FW_NUM_ARRAY_ELEMENTS(phases)];
    for (FwSizeType i = 0; i < FW_NUM_ARRAY_ELEMENTS(phases); i++) {
        maxUs[i] = toUs(phases[i]->max_cycles, hz);
        avgUs[i] = (phases[i]->count > 0) ? toUs(phases[i]->sum_cycles / phases[i]->count, hz) : 0U;
    }
    this->tlmWrite_TickMaxUs(maxUs[0]);
    this->tlmWrite_TickAvgUs(avgUs[0]);
    this->tlmWrite_RenderMaxUs(maxUs[1]);
    this->tlmWrite_RenderAvgUs(avgUs[1]);
    this->tlmWrite_BlitMaxUs(maxUs[2]);
    this->tlmWrite_BlitAvgUs(avgUs[2]);
    this->tlmWrite_EmitMaxUs(maxUs[3]);
    this->tlmWrite_EmitAvgUs(avgUs[3]);

    engine_hooks_threads_init();
    const char* name = nullptr;
    int prio = 0;
    if (engine_hooks_thread_info(this->m_threadInfoNext, &name, &prio) != 0) {
        this->log_ACTIVITY_HI_ThreadInfo(static_cast<U8>(this->m_threadInfoNext), static_cast<I8>(prio), Fw::String(name));
        this->m_threadInfoNext++;
    } else {
        this->m_threadInfoNext = 0;  // table exhausted: re-announce so a late-attaching ground sees it
    }
    this->tlmWrite_TickCpuAvgUs((stats.tick.count > 0) ? toUs(stats.tick_cpu_cycles / stats.tick.count, hz) : 0U);
    this->tlmWrite_SlipTicks(stats.slip_ticks);
    this->tlmWrite_StallTicks(stats.stall_ticks);
    this->tlmWrite_SlipWallAvgUs((stats.slip_ticks > 0) ? toUs(stats.slip_wall_cycles / stats.slip_ticks, hz) : 0U);
    this->tlmWrite_SlipCpuAvgUs((stats.slip_ticks > 0) ? toUs(stats.slip_cpu_cycles / stats.slip_ticks, hz) : 0U);
    this->tlmWrite_StallWallAvgUs((stats.stall_ticks > 0) ? toUs(stats.stall_wall_cycles / stats.stall_ticks, hz) : 0U);
    this->tlmWrite_StallCpuAvgUs((stats.stall_ticks > 0) ? toUs(stats.stall_cpu_cycles / stats.stall_ticks, hz) : 0U);
    this->tlmWrite_StallIdleAvgUs((stats.stall_ticks > 0) ? toUs(stats.stall_idle_cycles / stats.stall_ticks, hz) : 0U);
    U32 thr[ENGINE_HOOKS_MAX_THREADS] = {};
    for (U32 i = 0; i < ENGINE_HOOKS_MAX_THREADS; i++) {
        thr[i] = (stats.slip_ticks > 0) ? toUs(stats.slip_thread_cycles[i] / stats.slip_ticks, hz) : 0U;
    }
    this->tlmWrite_SlipThr00Us(thr[0]);
    this->tlmWrite_SlipThr01Us(thr[1]);
    this->tlmWrite_SlipThr02Us(thr[2]);
    this->tlmWrite_SlipThr03Us(thr[3]);
    this->tlmWrite_SlipThr04Us(thr[4]);
    this->tlmWrite_SlipThr05Us(thr[5]);
    this->tlmWrite_SlipThr06Us(thr[6]);
    this->tlmWrite_SlipThr07Us(thr[7]);
    this->tlmWrite_SlipThr08Us(thr[8]);
    this->tlmWrite_SlipThr09Us(thr[9]);
    this->tlmWrite_SlipThr10Us(thr[10]);
    this->tlmWrite_SlipThr11Us(thr[11]);
    this->tlmWrite_SlipThr12Us(thr[12]);
    this->tlmWrite_SlipThr13Us(thr[13]);
    this->tlmWrite_SlipThr14Us(thr[14]);
    this->tlmWrite_SlipThr15Us(thr[15]);
    this->tlmWrite_SlipThr16Us(thr[16]);
    this->tlmWrite_SlipThr17Us(thr[17]);
    this->tlmWrite_SlipThr18Us(thr[18]);
    this->tlmWrite_SlipThr19Us(thr[19]);
    this->tlmWrite_SlipThr20Us(thr[20]);
    this->tlmWrite_SlipThr21Us(thr[21]);
    this->tlmWrite_SlipThr22Us(thr[22]);
    this->tlmWrite_SlipThr23Us(thr[23]);
}

}  // namespace Components
