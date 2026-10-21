// ======================================================================
// \title  EngineProfiler.hpp
// \brief  hpp file for EngineProfiler component implementation class
// ======================================================================

#ifndef Components_EngineProfiler_HPP
#define Components_EngineProfiler_HPP

#include "DoomSatellite/Components/EngineProfiler/EngineHooks.h"
#include "DoomSatellite/Components/EngineProfiler/EngineProfilerComponentAc.hpp"

namespace Components {

class EngineProfiler final : public EngineProfilerComponentBase {
  public:
    explicit EngineProfiler(const char* const compName  //!< The component name
    );
    ~EngineProfiler();

  private:
    void schedIn_handler(FwIndexType portNum, U32 context) override;

    //! Convert a cycle count to whole microseconds at the hook clock rate
    static U32 toUs(U64 cycles, U32 cyclesPerSecond);

  public:
    //! Record the hwinfo reset cause of this boot for the ResetCause channel (called from Main before the topology starts)
    static void setResetCause(U32 cause);

  private:
    static U32 s_resetCause;
    U64 m_cpuPrev[6];      //!< previous-window cycle totals per bucket
    U64 m_cpuPrevTotal;
    U32 m_threadInfoNext;  //!< next attribution-table index to announce via ThreadInfo
};

}  // namespace Components

#endif
