#####
# 'DoomFlight' Deployment:
#
# This registers the 'DoomFlight' deployment to the build system. 
# Custom components that have not been added at the project-level should be added to 
# the list below.
#
#####

###
# Topology and Components
###

add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/Top/")
if (FPRIME_PLATFORM STREQUAL "Zephyr")
   register_fprime_zephyr_deployment(
            ${FPRIME_CURRENT_MODULE}
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/Main.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/CppAtomics.c"
            "${CMAKE_CURRENT_LIST_DIR}/PsramHeap.c"
            "${CMAKE_CURRENT_LIST_DIR}/PsramClock.c"
            "${CMAKE_CURRENT_LIST_DIR}/EngineHooks.c"
        DEPENDS
            ${FPRIME_CURRENT_MODULE}_Top
    )
    # Teensy 4.1: large malloc calls and the DoomEngine instance live in PSRAM (PsramHeap.c, psram_sections.ld)
    if (CONFIG_BOARD_TEENSY41)
        zephyr_ld_options(-Wl,--wrap=malloc -Wl,--wrap=free -Wl,--wrap=calloc -Wl,--wrap=realloc)
        # Per-phase tick timing (EngineHooks.c); the wrapped calls cross object files, see that file
        zephyr_ld_options(-Wl,--wrap=doomgeneric_Tick -Wl,--wrap=I_FinishUpdate -Wl,--wrap=DG_DrawFrame)
        zephyr_linker_sources(SECTIONS "${CMAKE_CURRENT_LIST_DIR}/psram_sections.ld")
        zephyr_include_directories("${ZEPHYR_BASE}/drivers/memc")
    endif()
endif()
