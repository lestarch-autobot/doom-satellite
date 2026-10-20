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
            "${CMAKE_CURRENT_LIST_DIR}/ScreenBuffers.c"
        DEPENDS
            ${FPRIME_CURRENT_MODULE}_Top
    )
    # Teensy 4.1: large malloc calls and the DoomEngine instance live in PSRAM (PsramHeap.c, psram_sections.ld)
    if (CONFIG_BOARD_TEENSY41)
        zephyr_ld_options(-Wl,--wrap=malloc -Wl,--wrap=free -Wl,--wrap=calloc -Wl,--wrap=realloc)
        # Per-phase tick timing (EngineHooks.c); the wrapped calls cross object files, see that file
        zephyr_ld_options(-Wl,--wrap=doomgeneric_Tick -Wl,--wrap=I_FinishUpdate -Wl,--wrap=DG_DrawFrame)
        # I_VideoBuffer in DTCM (ScreenBuffers.c)
        zephyr_ld_options(-Wl,--wrap=Z_Malloc -Wl,--wrap=Z_Free)
        # The software renderer and the frame blit run from ITCM instead of XIP flash (CONFIG_CODE_DATA_RELOCATION)
        set(DOOMGENERIC_SRC "${CMAKE_CURRENT_LIST_DIR}/../../lib/fprime-stress/Doom/DoomEngine/doomgeneric/doomgeneric")
        foreach(hot r_draw.c r_main.c r_segs.c r_plane.c r_things.c r_bsp.c r_data.c r_sky.c m_fixed.c v_video.c i_video.c)
            zephyr_code_relocate(FILES "${DOOMGENERIC_SRC}/${hot}" LOCATION ITCM_TEXT)
        endforeach()
        # T15: the UDP frame-stream TX path (sockets -> net_context -> ipv4/udp -> ethernet L2/ARP -> ENET driver) and the
        # kernel primitives it touches also run from ITCM: executed ~700 times/s while DOOM thrashes the XIP flash cache.
        set(NET_TX_HOT
            "${ZEPHYR_BASE}/subsys/net/ip/net_if.c" "${ZEPHYR_BASE}/subsys/net/ip/net_context.c"
            "${ZEPHYR_BASE}/subsys/net/ip/net_pkt.c" "${ZEPHYR_BASE}/subsys/net/ip/udp.c"
            "${ZEPHYR_BASE}/subsys/net/ip/ipv4.c" "${ZEPHYR_BASE}/subsys/net/ip/net_core.c"
            "${ZEPHYR_BASE}/subsys/net/ip/net_tc.c" "${ZEPHYR_BASE}/subsys/net/ip/icmpv4.c"
            "${ZEPHYR_BASE}/subsys/net/l2/ethernet/ethernet.c" "${ZEPHYR_BASE}/subsys/net/l2/ethernet/arp.c"
            "${ZEPHYR_BASE}/drivers/ethernet/eth_nxp_enet.c" "${ZEPHYR_BASE}/lib/net_buf/buf.c"
            "${ZEPHYR_BASE}/subsys/net/lib/sockets/sockets.c" "${ZEPHYR_BASE}/subsys/net/lib/sockets/sockets_inet.c"
            "${ZEPHYR_BASE}/kernel/sched.c" "${ZEPHYR_BASE}/kernel/timeout.c" "${ZEPHYR_BASE}/kernel/mutex.c"
            "${ZEPHYR_BASE}/kernel/sem.c" "${ZEPHYR_BASE}/kernel/mem_slab.c"
            "${CMAKE_CURRENT_LIST_DIR}/../../Components/FrameStreamer/FrameStreamer.cpp"
            "${FPRIME_FRAMEWORK_PATH}/Drv/Ip/IpSocket.cpp" "${FPRIME_FRAMEWORK_PATH}/Drv/Ip/UdpSocket.cpp"
        )
        foreach(hot IN LISTS NET_TX_HOT)
            zephyr_code_relocate(FILES "${hot}" LOCATION ITCM_TEXT)
        endforeach()
        # P2-T2: the downlink chain (TlmPacketizer -> ComQueue -> ComAggregator -> UART/CDC) runs at the ground frame
        # rate x 51 packets; same XIP-cache argument as the stream path.
        set(DOWNLINK_HOT
            "${FPRIME_FRAMEWORK_PATH}/Svc/TlmPacketizer/TlmPacketizer.cpp"
            "${FPRIME_FRAMEWORK_PATH}/Svc/ComQueue/ComQueue.cpp"
            "${FPRIME_FRAMEWORK_PATH}/Svc/ComAggregator/ComAggregator.cpp"
            "${FPRIME_FRAMEWORK_PATH}/Fw/Types/Serializable.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/../../lib/fprime-zephyr/fprime-zephyr/Drv/ZephyrUartDriver/ZephyrUartDriver.cpp"
            "${ZEPHYR_BASE}/subsys/usb/device_next/class/usbd_cdc_acm.c"
        )
        foreach(hot IN LISTS DOWNLINK_HOT)
            zephyr_code_relocate(FILES "${hot}" LOCATION ITCM_TEXT)
        endforeach()
# fprime-stress asks for -O2 on the engine, but Zephyr appends its CONFIG_SIZE_OPTIMIZATIONS -Os after the target
# options and the last -O wins. Per-source options come after both, so the engine really builds at -O2 here.
get_target_property(DOOMGENERIC_SOURCES fprime_stress_doomgeneric SOURCES)
set_source_files_properties(${DOOMGENERIC_SOURCES} TARGET_DIRECTORY fprime_stress_doomgeneric PROPERTIES COMPILE_OPTIONS "-O2")
        zephyr_linker_sources(SECTIONS "${CMAKE_CURRENT_LIST_DIR}/psram_sections.ld")
        zephyr_include_directories("${ZEPHYR_BASE}/drivers/memc")
    endif()
endif()
