####
# DoomSatellite/DoomCoprocessor/CMakeLists.txt:
#
# The DoomCoprocessor deployment. Its frameReader component is implemented in Python through fprime-python, so the
# deployment is launched from fsw_main.py (python3 build-artifacts/<platform>/DoomCoprocessor/python/fsw_main.py),
# which imports the fprime_py module built from this deployment. Main.cpp is a stub that keeps the C++ executable
# target buildable; it does not run the topology.
####
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/Top/")

register_fprime_deployment(
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/Main.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/fsw_main.py"
    CHOOSES_IMPLEMENTATIONS
        FprimePython_PythonConsole
    DEPENDS
        ${FPRIME_CURRENT_MODULE}_Top
)

if (TARGET pybind11::embed)
    target_link_libraries("${FPRIME_CURRENT_MODULE}" PRIVATE pybind11::embed)
endif()
