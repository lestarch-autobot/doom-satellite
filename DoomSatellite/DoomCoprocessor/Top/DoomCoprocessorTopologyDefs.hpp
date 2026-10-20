// ======================================================================
// \title  DoomCoprocessorTopologyDefs.hpp
// \brief required header file containing the required definitions for the topology autocoder
// ======================================================================
#ifndef DOOMCOPROCESSOR_DOOMCOPROCESSORTOPOLOGYDEFS_HPP
#define DOOMCOPROCESSOR_DOOMCOPROCESSORTOPOLOGYDEFS_HPP

// Include autocoded FPP constants
#include "DoomSatellite/DoomCoprocessor/Top/FppConstantsAc.hpp"

// keyBridge.key*Out -> hub.serialIn[n]: fpp 3.4 emits the typed-to-serial glue with the unqualified
// `KeyEventPortSerializer` (the port lives in module Doom, the topology in DoomCoprocessor), so make it visible here.
#include "Doom/KeyEventPortAc.hpp"
using Doom::KeyEventPortBuffer;
using Doom::KeyEventPortSerializer;

// DoomSubtopology configuration phases call memset

/**
 * \brief required ping constants
 *
 * The topology autocoder requires a WARN and FATAL constant definition for each component that supports the health-ping
 * interface when a health component is in the topology. This topology has no health component.
 */
namespace PingEntries {}  // namespace PingEntries

// Definitions are placed within a namespace named after the deployment
namespace DoomCoprocessor {

/**
 * \brief required type definition to carry state
 *
 * The topology autocoder requires an object that carries state with the name `DoomCoprocessor::TopologyState`. Only
 * the type definition is required by the autocoder and the contents of this object are otherwise opaque to the
 * autocoder.
 */
struct TopologyState {
    const char* hubRemoteAddress = nullptr;  //!< IPv4 address of the remote (control node) GenericHub deployment
    U16 hubRemotePort = 0;                   //!< UDP port the remote GenericHub receives on
    U16 hubLocalPort = 0;                    //!< UDP port this GenericHub receives on
};

namespace PingEntries = ::PingEntries;
}  // namespace DoomCoprocessor
#endif
