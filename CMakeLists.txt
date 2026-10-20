####
# This sets up the build system for the 'doom-satellite' project, including components and deployments from
# DoomSatellite/. It also imports the core F Prime components.
#
# Deployments:
#   DoomFlight:      Teensy 4.1 / RP2040 (Zephyr) flight software. Built with the default 'zephyr' toolchain.
#   DoomCoprocessor: arm-linux coprocessor software. Built with the 'aarch64-linux' toolchain.
####
cmake_minimum_required(VERSION 3.24.2)
# Zephyr is loaded only for the Zephyr toolchain and not when testing (F Prime unit tests run on Linux)
if (NOT BUILD_TESTING AND CMAKE_TOOLCHAIN_FILE MATCHES "/zephyr\\.cmake$")
    find_package(Zephyr HINTS "${CMAKE_CURRENT_LIST_DIR}/lib/zephyr-workspace")
endif()
project(doom-satellite C CXX)

# fprime-python supplies the Python component bindings used by DoomCoprocessor. It is added to the library list here
# rather than in settings.ini so that the Zephyr (DoomFlight) build, which has no Python interpreter and would otherwise
# compile the pybind11-based FprimePython module, never sees it. The JSON model export is the input of the
# fprime-python autocoder.
set(DOOM_SATELLITE_SETTINGS_LIBRARY_LOCATIONS "${FPRIME_LIBRARY_LOCATIONS}")
if (NOT CMAKE_TOOLCHAIN_FILE MATCHES "/zephyr\\.cmake$")
    list(APPEND FPRIME_LIBRARY_LOCATIONS "${CMAKE_CURRENT_LIST_DIR}/lib/fprime-python")
    list(REMOVE_DUPLICATES FPRIME_LIBRARY_LOCATIONS)
    set(FPRIME_ENABLE_JSON_MODEL_GENERATION ON)
    set(DOOM_SATELLITE_FPRIME_PYTHON ON)
endif()

###
# F' Core Setup
# This includes all of the F prime core components, and imports the make-system.
###
include("${CMAKE_CURRENT_LIST_DIR}/lib/fprime/cmake/FPrime.cmake")

if (DOOM_SATELLITE_FPRIME_PYTHON)
    # FPrime.cmake writes the (absolute) library list back to the cache, and fprime-util refuses a build cache whose
    # FPRIME_LIBRARY_LOCATIONS differs from settings.ini: restore the settings.ini value in the cache while keeping the
    # full list in the normal variable for the rest of the configure
    set(DOOM_SATELLITE_FULL_LIBRARY_LOCATIONS "${FPRIME_LIBRARY_LOCATIONS}")
    set(DOOM_SATELLITE_SETTINGS_LIBRARY_LOCATIONS_ABS)
    foreach (LIBLOC IN LISTS DOOM_SATELLITE_SETTINGS_LIBRARY_LOCATIONS)
        get_filename_component(LIBLOC_ABS "${LIBLOC}" ABSOLUTE)
        list(APPEND DOOM_SATELLITE_SETTINGS_LIBRARY_LOCATIONS_ABS "${LIBLOC_ABS}")
    endforeach()
    set(FPRIME_LIBRARY_LOCATIONS "${DOOM_SATELLITE_SETTINGS_LIBRARY_LOCATIONS_ABS}" CACHE STRING "F prime library locations" FORCE)
    set(FPRIME_LIBRARY_LOCATIONS "${DOOM_SATELLITE_FULL_LIBRARY_LOCATIONS}")
endif()

# NOTE: register custom targets between these two lines
if (DOOM_SATELLITE_FPRIME_PYTHON AND NOT DEFINED FPRIME_SUB_BUILD_TARGETS)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(PYBIND11_FINDPYTHON ON)
    # Locate the pip-installed pybind11 CMake package via the interpreter
    execute_process(COMMAND python3 -m pybind11 --cmakedir
            OUTPUT_VARIABLE PYBIND11_CMAKEDIR OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    find_package(pybind11 CONFIG REQUIRED PATHS "${PYBIND11_CMAKEDIR}" "$ENV{VIRTUAL_ENV}/lib/*/site-packages/")
    link_libraries("pybind11::module" "pybind11::headers")
    register_fprime_target(target/fprime_python)
endif()
fprime_setup_included_code()

# This includes project-wide objects
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/DoomSatellite")

if (FPRIME_PLATFORM STREQUAL "Zephyr")
    # Upstream DOOM's config.h defines VERSION, which the NXP SoC headers (reached through the POSIX headers DOOM
    # includes after config.h) use as a register name. Pull the kernel headers in first: later includes are guarded,
    # so VERSION is never expanded inside them and the upstream sources stay untouched.
    target_compile_options(fprime_stress_doomgeneric PRIVATE -include zephyr/kernel.h)
    set_target_properties(Svc_FatalHandler PROPERTIES EXCLUDE_FROM_ALL TRUE)
    # fprime-zephyr drivers this topology does not use
    set_target_properties(fprime-zephyr_Drv_ZephyrSpiDriver fprime-zephyr_Drv_LoRa PROPERTIES EXCLUDE_FROM_ALL TRUE)
    set_target_properties(Drv_TcpClient Drv_TcpServer PROPERTIES EXCLUDE_FROM_ALL TRUE)
else()
    # fprime-zephyr is a library location for every toolchain (settings.ini) and its modules need the Zephyr headers,
    # so on native builds keep everything it registers out of the default target (nothing there is selected anyway).
    function(doom_exclude_directory_targets dir)
        get_property(dir_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(dir_target IN LISTS dir_targets)
            set_target_properties("${dir_target}" PROPERTIES EXCLUDE_FROM_ALL TRUE)
        endforeach()
        get_property(sub_dirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
        foreach(sub_dir IN LISTS sub_dirs)
            doom_exclude_directory_targets("${sub_dir}")
        endforeach()
    endfunction()
    foreach(zephyr_dir default/zephyr-config fprime-zephyr/Os fprime-zephyr/Fw fprime-zephyr/Drv fprime-zephyr/Svc
                       fprime-zephyr/Fs)
        doom_exclude_directory_targets("${CMAKE_CURRENT_LIST_DIR}/lib/fprime-zephyr/${zephyr_dir}")
    endforeach()
endif()
