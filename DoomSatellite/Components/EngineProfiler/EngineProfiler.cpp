// ======================================================================
// \title  EngineProfiler.cpp
// \brief  cpp file for EngineProfiler component implementation class
// ======================================================================

#include "DoomSatellite/Components/EngineProfiler/EngineProfiler.hpp"

#include <zephyr/kernel.h>

namespace Components {

EngineProfiler::EngineProfiler(const char* const compName) : EngineProfilerComponentBase(compName), m_cpuPrev{}, m_cpuPrevTotal(0) {}

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
}

}  // namespace Components
