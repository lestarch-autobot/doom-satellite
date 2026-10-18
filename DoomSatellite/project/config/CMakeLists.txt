####
# DoomSatellite/project/config/CMakeLists.txt:
#
# Project configuration overrides. PlatformCfg.fpp (OS handle sizes) and IpCfg.hpp (socket headers) are tuned for
# Zephyr and are only applied to the Zephyr (DoomFlight) build; other platforms use the F Prime defaults.
####
set(DOOM_CONFIG_OVERRIDES
    "${CMAKE_CURRENT_LIST_DIR}/AcConstants.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/CdhCoreConfig.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/CdhCoreTlmConfig.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/CdhCoreFatalHandlerConfig.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/ComCcsdsConfig.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/ComCfg.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/CommandDispatcherImplCfg.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/DoomConfig.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/FpConstants.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/FpConfig.h"
    "${CMAKE_CURRENT_LIST_DIR}/TlmPacketizerCfg.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/TlmPacketizerCfg.hpp"
)
if (FPRIME_PLATFORM STREQUAL "Zephyr")
    list(APPEND DOOM_CONFIG_OVERRIDES
        "${CMAKE_CURRENT_LIST_DIR}/IpCfg.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/PlatformCfg.fpp"
        "${CMAKE_CURRENT_LIST_DIR}/ZephyrUartDriverCfg.hpp"
    )
endif()

register_fprime_config(
    CONFIGURATION_OVERRIDES
        ${DOOM_CONFIG_OVERRIDES}
    INTERFACE
)
