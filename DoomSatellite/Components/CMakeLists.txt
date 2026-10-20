# Include project-wide components here

add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/NullPrmDb")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/FrameBufferAdapter")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/FrameRepeater")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/EngineProfiler")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/FrameHandoff")
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/FrameStreamer")
# Python components need the fprime-python bindings, which are not available on Zephyr
if (DOOM_SATELLITE_FPRIME_PYTHON)
    add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/DoomFrameReader")
    add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/DoomKeyBridge")
endif()
if (FPRIME_PLATFORM STREQUAL "Zephyr")
    add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/FatalHandler")
endif()
