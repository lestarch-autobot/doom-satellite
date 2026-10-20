// ======================================================================
// \title  EngineProfiler.cpp
// \brief  cpp file for EngineProfiler component implementation class
// ======================================================================

#include "DoomSatellite/Components/EngineProfiler/EngineProfiler.hpp"

namespace Components {

EngineProfiler::EngineProfiler(const char* const compName) : EngineProfilerComponentBase(compName) {}

EngineProfiler::~EngineProfiler() {}

U32 EngineProfiler::toUs(U64 cycles, U32 cyclesPerSecond) {
    FW_ASSERT(cyclesPerSecond > 0);
    return static_cast<U32>((cycles * 1000000ULL) / cyclesPerSecond);
}

void EngineProfiler::schedIn_handler(FwIndexType portNum, U32 context) {
    struct engine_hooks_stats stats;
    engine_hooks_snapshot(&stats);
    const U32 hz = stats.cycles_per_second;

    this->tlmWrite_Ticks(stats.tick.count);

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
