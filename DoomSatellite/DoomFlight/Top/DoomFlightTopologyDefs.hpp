// ======================================================================
// \title  DoomFlightTopologyDefs.hpp
// \brief required header file containing the required definitions for the topology autocoder
//
// ======================================================================
#ifndef DOOMFLIGHT_DOOMFLIGHTTOPOLOGYDEFS_HPP
#define DOOMFLIGHT_DOOMFLIGHTTOPOLOGYDEFS_HPP

// Subtopology PingEntries includes
#include "Svc/Subtopologies/CdhCore/PingEntries.hpp"
#include "Svc/Subtopologies/ComCcsds/PingEntries.hpp"
#include "Svc/Subtopologies/DataProducts/PingEntries.hpp"
#include "Svc/Subtopologies/FileHandling/PingEntries.hpp"

// SubtopologyTopologyDefs includes
#include "Svc/Subtopologies/CdhCore/SubtopologyTopologyDefs.hpp"
#include "Svc/Subtopologies/ComCcsds/SubtopologyTopologyDefs.hpp"

#include "Svc/Subtopologies/ComCcsds/Ports_ComBufferQueueEnumAc.hpp"
#include "Svc/Subtopologies/ComCcsds/Ports_ComPacketQueueEnumAc.hpp"

// Include autocoded FPP constants
#include <zephyr/drivers/uart.h>
#include <cstring>
#include "DoomSatellite/DoomFlight/Top/DoomFlight_DoomFlightPacketsTlmPacketsAc.hpp"
#include "DoomSatellite/DoomFlight/Top/FppConstantsAc.hpp"

// DoomSubtopology configuration phases: memAllocator and the BufferManager bins
#include "Doom/DoomConfig/FppConstantsAc.hpp"
#include "Doom/DoomSubtopology/SubtopologyTopologyDefs.hpp"

/**
 * \brief required ping constants
 *
 * The topology autocoder requires a WARN and FATAL constant definition for each component that supports the health-ping
 * interface. These are expressed as enum constants placed in a namespace named for the component instance. These
 * are all placed in the PingEntries namespace.
 *
 * Each constant specifies how many missed pings are allowed before a WARNING_HI/FATAL event is triggered. In the
 * following example, the health component will emit a WARNING_HI event if the component instance cmdDisp does not
 * respond for 3 pings and will FATAL if responses are not received after a total of 5 pings.
 *
 * ```c++
 * namespace PingEntries {
 * namespace cmdDisp {
 *     enum { WARN = 3, FATAL = 5 };
 * }
 * }
 * ```
 */
namespace PingEntries {
namespace DoomFlight_rateGroup10Hz {
enum { WARN = 3, FATAL = 5 };
}
namespace DoomFlight_rateGroup1Hz {
enum { WARN = 3, FATAL = 5 };
}
namespace DoomFlight_rateGroupDoom {
enum { WARN = 3, FATAL = 5 };
}
namespace DoomFlight_rateGroupTlm {
enum { WARN = 3, FATAL = 5 };
}
namespace DoomFlight_fileUplink {
enum { WARN = 3, FATAL = 5 };
}
}  // namespace PingEntries

// Definitions are placed within a namespace named after the deployment
namespace DoomFlight {

/**
 * \brief required type definition to carry state
 *
 * The topology autocoder requires an object that carries state with the name `DoomFlight::TopologyState`. Only the type
 * definition is required by the autocoder and the contents of this object are otherwise opaque to the autocoder. The
 * contents are entirely up to the definition of the project. This deployment uses subtopologies.
 */
struct TopologyState {
    const device* uartDevice;             //!< USB CDC ACM UART carrying the ground link and the touch reset
    U32 baudRate;                         //!< Baud rate for the ground link UART
    const char* hubRemoteAddress;         //!< IPv4 address of the remote GenericHub deployment
    U16 hubRemotePort;                    //!< UDP port the remote GenericHub receives on
    U16 hubLocalPort;                     //!< UDP port this GenericHub receives on
    const char* wadPath;                  //!< DOOM IWAD path; the engine is not created when it cannot be opened
    bool doomAutoStart;                   //!< Start the DOOM engine at setup instead of waiting for doom.Start
    const char* fileUplinkDirectory;      //!< Directory uplinked files may be written under
    CdhCore::SubtopologyState cdhCore;    //!< Subtopology state for CdhCore
    ComCcsds::SubtopologyState comCcsds;  //!< Subtopology state for ComCcsds
};

namespace PingEntries = ::PingEntries;
}  // namespace DoomFlight
#endif
