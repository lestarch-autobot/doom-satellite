// ======================================================================
// \title  DoomFlightTopology.cpp
// \brief cpp file containing the topology instantiation code
//
// ======================================================================
// Provides access to autocoded functions
#include <DoomSatellite/DoomFlight/Top/DoomFlightTopologyAc.hpp>
#include <DoomSatellite/DoomFlight/Top/Ports_FrameRepeaterEnumAc.hpp>

// Necessary project-specified types
#include <Fw/Types/MallocAllocator.hpp>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#include <cstring>

extern "C" uint8_t* stream_slot(unsigned int index);  // ScreenBuffers.c: PSRAM slots for frameStreamer

#include <zephyr/net/net_if.h>
//! Carrier state of the default (Ethernet) interface, for frameStreamer's link check
static bool ethernetCarrierUp() {
    struct net_if* iface = net_if_get_default();
    return (iface != nullptr) && net_if_is_up(iface) && net_if_is_carrier_ok(iface);
}

// Allows easy reference to objects in FPP/autocoder required namespaces
using namespace DoomFlight;

// Instantiate a malloc allocator for cmdSeq buffer allocation
Fw::MallocAllocator mallocator;

constexpr FwSizeType BASE_RATEGROUP_PERIOD_MS = 1;  // 1Khz

// Helper function to calculate the period for a given rate group frequency
constexpr FwSizeType getRateGroupPeriod(const FwSizeType hz) {
    return 1000 / (hz * BASE_RATEGROUP_PERIOD_MS);
}

// DOOM's native cadence is 35 Hz; the Teensy runs 30 Hz. The engine memory lives in QSPI PSRAM (90 MHz), where
// a native 320x200 tick (logic, render, downsample, 50 row channels) measures 20-31 ms: 84% of the CPU at 35 Hz,
// which starves the telemetry and downlink threads (or, with the engine at the lowest priority, falls behind
// until the health ping is late). At 30 Hz the engine takes ~70% and the board holds the rate with a working
// downlink. The engine paces game time per tick, so play runs at 30/35 of real time. The 640x400 upscale and a
// frame copy used to cost ~50 ms more and forced 5 Hz. See the rateGroupDoom comment in instances.fpp.
constexpr FwSizeType DOOM_RATE_HZ = 30;
// Ground frame rate: one TlmPacketizer run per tick of rateGroupTlm. A complete 80x50 frame is 51 packets (palette +
// 50 rows), ~7.5 kB on the wire. Measured on the Teensy with the engine at 30 Hz: 10 Hz and above starve the engine
// until health reports rateGroupDoom's ping late (~113 kB/s is also where the CDC link tops out); 5 Hz still pulls
// the engine down to ~22 Hz; 2 Hz keeps it at ~29 Hz with ~300 rate group slips/min (1 Hz: ~16/min). A higher
// ground rate needs a cheaper encoding than 51 channel packets per frame.
constexpr FwSizeType TLM_RATE_HZ = 15;

// The 1 kHz base timer is divided into the 10Hz, 1Hz and DOOM rate groups with 0 offset
Svc::RateGroupDriver::DividerSet rateGroupDivisorsSet{{
    // Array of divider objects
    {getRateGroupPeriod(10), 0},            // 10Hz
    {getRateGroupPeriod(1), 0},             // 1Hz
    {getRateGroupPeriod(DOOM_RATE_HZ), 0},  // DOOM
    {getRateGroupPeriod(TLM_RATE_HZ), 0},   // telemetry packetizer (ground frame rate)
}};

// Rate groups may supply a context token to each of the attached children whose purpose is set by the project. The
// 10Hz and 1Hz tokens are unused; DoomEngine reads its tick period in microseconds from the DOOM rate group's token.
U32 rateGroup10HzContext[Svc::ActiveRateGroup::CONNECTION_COUNT_MAX] = {getRateGroupPeriod(10)};
U32 rateGroup1HzContext[Svc::ActiveRateGroup::CONNECTION_COUNT_MAX] = {getRateGroupPeriod(1)};
U32 rateGroupDoomContext[Svc::ActiveRateGroup::CONNECTION_COUNT_MAX] = {
    static_cast<U32>(getRateGroupPeriod(DOOM_RATE_HZ) * BASE_RATEGROUP_PERIOD_MS * 1000)};
U32 rateGroupTlmContext[Svc::ActiveRateGroup::CONNECTION_COUNT_MAX] = {getRateGroupPeriod(TLM_RATE_HZ)};

enum TopologyConstants {
    HUB_BUFFER_MANAGER_ID = 300,
    HUB_SMALL_BUFFER_SIZE = 1024,  // Commands, events, telemetry and packed palettes
    HUB_SMALL_BUFFER_COUNT = 8,
    HUB_LARGE_BUFFER_SIZE = 4096,  // UDP receive buffers and packed frames (4,009 bytes, 4,025 with the hub header)
    HUB_LARGE_BUFFER_COUNT = 12,
    STREAM_PORT = 50601,  // raw UDP native-frame stream to the coprocessor (Components::FrameStreamer)
    STREAM_DIVISOR = 1,   // stream every engine frame (30 fps); SET_RATE raises the divisor from the ground
    HUB_RECV_PRIORITY = 5,
    HUB_RECONNECT_PRIORITY = 6,
};

// Opcodes at or above the DoomCoprocessor base id are forwarded through the hub
constexpr FwOpcodeType REMOTE_BASE_OPCODE = 0x20000000;

#if defined(CONFIG_USBD_CDC_ACM_CLASS)
constexpr U32 CDC_FLOW_CONTROL_POLL_MS = 100;
static const struct device* cdcUartDevice = nullptr;
static struct k_work_delayable cdcFlowControlWork;

//! CDC ACM discards writes once its TX FIFO is full unless flow control is set, in which case writes block. Writes
//! block only while the host asserts DTR, so a closed host port drops downlink bytes instead of stalling the sender.
static void updateCdcFlowControl(struct k_work* work) {
    U32 dtr = 0;
    struct uart_config uartConfig;
    if ((uart_line_ctrl_get(cdcUartDevice, UART_LINE_CTRL_DTR, &dtr) == 0) &&
        (uart_config_get(cdcUartDevice, &uartConfig) == 0)) {
        uartConfig.flow_ctrl = (dtr != 0) ? UART_CFG_FLOW_CTRL_RTS_CTS : UART_CFG_FLOW_CTRL_NONE;
        (void)uart_configure(cdcUartDevice, &uartConfig);
    }
    (void)k_work_schedule(k_work_delayable_from_work(work), K_MSEC(CDC_FLOW_CONTROL_POLL_MS));
}
#endif

/**
 * \brief configure/setup components in project-specific way
 *
 * This is a *helper* function which configures/sets up each component requiring project specific input. This includes
 * allocating resources, passing-in arguments, etc. This function may be inlined into the topology setup function if
 * desired, but is extracted here for clarity.
 */
void configureTopology(const TopologyState& state) {
    // Rate group driver needs a divisor list
    rateGroupDriver.configure(rateGroupDivisorsSet);
    // Rate groups require context arrays.
    rateGroup10Hz.configure(rateGroup10HzContext, FW_NUM_ARRAY_ELEMENTS(rateGroup10HzContext));
    rateGroup1Hz.configure(rateGroup1HzContext, FW_NUM_ARRAY_ELEMENTS(rateGroup1HzContext));
    rateGroupDoom.configure(rateGroupDoomContext, FW_NUM_ARRAY_ELEMENTS(rateGroupDoomContext));
    rateGroupTlm.configure(rateGroupTlmContext, FW_NUM_ARRAY_ELEMENTS(rateGroupTlmContext));
    // Reboot into the bootloader when the host opens the console at the board's touch baud rate
    if (touchReset.configure(state.uartDevice) != Fw::Success::SUCCESS) {
        printk("Touch reset unavailable\n");
    }

    Svc::BufferManager::BufferBins hubBins;
    memset(&hubBins, 0, sizeof(hubBins));
    hubBins.bins[0].bufferSize = HUB_SMALL_BUFFER_SIZE;
    hubBins.bins[0].numBuffers = HUB_SMALL_BUFFER_COUNT;
    hubBins.bins[1].bufferSize = HUB_LARGE_BUFFER_SIZE;
    hubBins.bins[1].numBuffers = HUB_LARGE_BUFFER_COUNT;
    hubBufferManager.setup(HUB_BUFFER_MANAGER_ID, 0, ComCcsds::Allocation::memAllocator, hubBins);

    (void)hubComDriver.configureSend(state.hubRemoteAddress, state.hubRemotePort);
    (void)hubComDriver.configureRecv("0.0.0.0", state.hubLocalPort, HUB_LARGE_BUFFER_SIZE);

    cmdSplitter.configure(REMOTE_BASE_OPCODE);
    // The hub slot runs hubComDriver's synchronous UDP send in the DOOM rate group thread; with the coprocessor
    // unreachable (no ARP reply for hubRemoteAddress) that send blocked the engine for good on the bench
    // (FrameCount froze at 11 while the 1 Hz group kept running). Start with the slot off; enable it from the
    // ground with frameRepeater SET_OUTPUT(HUB, ENABLED) once the coprocessor answers on the link.
    frameRepeater.setOutputEnabled(static_cast<FwIndexType>(DoomFlight::Ports_FrameRepeater::HUB), false);

    // The native-frame stream shares the coprocessor's address with the hub; its sends run on frameStreamer's own
    // thread (below the engine), so it can start enabled: an unreachable peer costs the engine only the frame copy.
    frameStreamer.configure(stream_slot(0), stream_slot(1), state.hubRemoteAddress, STREAM_PORT, STREAM_DIVISOR, true,
                            &ethernetCarrierUp);

    fileUplink.configure((state.fileUplinkDirectory != nullptr) ? state.fileUplinkDirectory : "/");

    // All engine heap allocation (PSRAM, see PsramHeap.c) happens here, before any task runs. An unreadable WAD
    // leaves the engine uncreated and doom.Start is rejected until the next boot.
    Doom::InitStatus initStatus = DoomSubtopology::doom.setWadPath((state.wadPath != nullptr) ? state.wadPath : "");
    if (initStatus == Doom::InitStatus::OK) {
        initStatus = DoomSubtopology::doom.initEngine();
    }
    if (initStatus != Doom::InitStatus::OK) {
        Fw::String text;
        initStatus.toString(text);
        printk("DoomFlight: DOOM engine init failed (%s): Start will be rejected\n", text.toChar());
    }
}

// Public functions for use in main program are namespaced with deployment name DoomFlight

namespace DoomFlight {
void setupTopology(const TopologyState& state) {
    // Autocoded initialization. Function provided by autocoder.
    initComponents(state);
    // Autocoded id setup. Function provided by autocoder.
    setBaseIds();
    // Autocoded connection wiring. Function provided by autocoder.
    connectComponents();
    // Autocoded command registration. Function provided by autocoder.
    regCommands();
    // Autocoded configuration. Function provided by autocoder.
    configComponents(state);
    // Project-specific component configuration. Function provided above. May be inlined, if desired.
    configureTopology(state);
    // Autocoded parameter loading. Function provided by autocoder.
    loadParameters();
    // Autocoded task kick-off (active components). Function provided by autocoder.
    startTasks(state);

    // UDP receive (and reconnect) tasks for the GenericHub link
    Os::TaskString hubName("hub");
    hubComDriver.start(hubName, HUB_RECV_PRIORITY, Default::STACK_SIZE, Os::Task::TASK_DEFAULT, HUB_RECONNECT_PRIORITY,
                       Default::STACK_SIZE);

    if (state.doomAutoStart) {
        Fw::String status;
        DoomSubtopology::doom.forceStart().toString(status);
        printk("DoomFlight: auto-start: doom.forceStart() returned %s\n", status.toChar());
    }

    comDriver.configure(state.uartDevice, state.baudRate);
#if defined(CONFIG_USBD_CDC_ACM_CLASS)
    cdcUartDevice = state.uartDevice;
    k_work_init_delayable(&cdcFlowControlWork, updateCdcFlowControl);
    (void)k_work_schedule(&cdcFlowControlWork, K_NO_WAIT);
#endif
}

void startRateGroups() {
    timer.configure(BASE_RATEGROUP_PERIOD_MS);
    timer.start();
    while (1) {
        timer.cycle();
    }
}

void stopRateGroups() {
    timer.stop();
}

void teardownTopology(const TopologyState& state) {
    // Autocoded (active component) task clean-up. Functions provided by topology autocoder.
    stopTasks(state);
    freeThreads(state);
    hubComDriver.stop();
    (void)hubComDriver.join();
    hubBufferManager.cleanup();
    tearDownComponents(state);
}
};  // namespace DoomFlight
