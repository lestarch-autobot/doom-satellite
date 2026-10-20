// ======================================================================
// \title  DoomCoprocessorBindings.cpp
// \brief Python bindings of the DoomCoprocessor topology state and lifecycle (fprime-python)
// ======================================================================
#include <csignal>
#include <string>

#include "DoomSatellite/DoomCoprocessor/Top/DoomCoprocessorTopology.hpp"
#include "DoomSatellite/DoomCoprocessor/Top/DoomCoprocessorTopologyDefs.hpp"
#include "FprimePython/FprimePython.hpp"

namespace {

//! Stop the topology on SIGINT/SIGTERM: Python signal handlers cannot run while the interpreter thread is blocked in
//! runTopology, so the signals are handled here for the duration of the run
void signalHandler(int signum) {
    DoomCoprocessor::stopTopology();
}

//! Run the topology until a signal stops it, restoring the interpreter's signal handlers afterwards
void runTopologyUntilSignal() {
    void (*const previousInt)(int) = signal(SIGINT, signalHandler);
    void (*const previousTerm)(int) = signal(SIGTERM, signalHandler);
    DoomCoprocessor::runTopology();
    (void)signal(SIGINT, previousInt);
    (void)signal(SIGTERM, previousTerm);
}

}  // namespace

//! Bind the deployment into the fprime_py module (called by the fprime-python generated module initialization)
void setup_user_deployment(pybind11::module_& m) {
    // TopologyState holds a const char* (set from argv by the C++ main); the Python string is copied into
    // module-lifetime storage so Python need not keep it alive
    static std::string hubRemoteAddressStorage;
    pybind11::class_<DoomCoprocessor::TopologyState>(m, "TopologyState")
        .def(pybind11::init<>())
        .def_property(
            "hubRemoteAddress",
            [](const DoomCoprocessor::TopologyState& state) {
                return std::string((state.hubRemoteAddress != nullptr) ? state.hubRemoteAddress : "");
            },
            [](DoomCoprocessor::TopologyState& state, const std::string& value) {
                hubRemoteAddressStorage = value;
                state.hubRemoteAddress = hubRemoteAddressStorage.c_str();
            },
            "IPv4 address of the remote (control node) GenericHub deployment")
        .def_readwrite("hubRemotePort", &DoomCoprocessor::TopologyState::hubRemotePort,
                       "UDP port the remote GenericHub receives on")
        .def_readwrite("hubLocalPort", &DoomCoprocessor::TopologyState::hubLocalPort,
                       "UDP port this GenericHub receives on");

    // The project lifecycle (DoomCoprocessorTopology.cpp): configures the hub and the timer in addition to the
    // autocoded phases
    m.def("setup_topology", &DoomCoprocessor::setupTopology, pybind11::arg("state"),
          "Set up the DoomCoprocessor topology: autocoded phases, hub, and tasks");
    m.def("run_topology", &runTopologyUntilSignal, pybind11::call_guard<pybind11::gil_scoped_release>(),
          "Drive the rate groups until SIGINT/SIGTERM or stop_topology; releases the interpreter lock so Python "
          "components run on the F Prime threads");
    m.def("stop_topology", &DoomCoprocessor::stopTopology, pybind11::call_guard<pybind11::gil_scoped_release>(),
          "Stop the timer driving the rate groups, returning run_topology");
    m.def("teardown_topology", &DoomCoprocessor::teardownTopology, pybind11::arg("state"),
          "Stop the tasks and tear down the DoomCoprocessor topology");
}
