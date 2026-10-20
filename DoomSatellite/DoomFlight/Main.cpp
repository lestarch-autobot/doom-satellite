// ======================================================================
// \title  Main.cpp
// \brief main program for the F' application. Intended for CLI-based systems (Linux, macOS)
//
// ======================================================================
// Used to access topology functions
#include <DoomSatellite/Components/EngineProfiler/EngineProfiler.hpp>
#include <DoomSatellite/DoomFlight/Top/DoomFlightTopology.hpp>
#include <Fw/Types/Assert.hpp>
#include <Os/Os.hpp>
#include <fprime-zephyr/Svc/ZephyrTouchReset/BootloaderEntry.hpp>

// Zephyr headers follow F Prime headers: Zephyr's EMPTY macro collides with Os::Queue::Status::EMPTY
#include <cmsis_core.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/fatal.h>
#include <zephyr/kernel.h>
#if defined(CONFIG_BOARD_TEENSY41)
#include <zephyr/net/phy.h>
#endif
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>

#include <cstdio>
#include <cstring>

const struct device* serial = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));
#if defined(CONFIG_BOARD_TEENSY41)
extern "C" void psram_heap_report(void);     // PsramHeap.c
extern "C" void screen_buffers_report(void);  // ScreenBuffers.c
#endif

//! Seconds the CDC ACM port is watched for a touch reset before the topology starts
static constexpr U32 BOOT_WINDOW_SECONDS = 10;
static constexpr U32 CRASH_RECORD_MAGIC = 0xDEADD00Du;

//! Kernel fatal error state, kept across the warm reboot that follows the fatal error
struct CrashRecord {
    U32 magic;
    U32 reason;
    U32 pc;
    U32 lr;
    U32 xpsr;
    U32 cfsr;
    U32 hfsr;
    U32 mmfar;
    U32 bfar;
    U32 stackStart;
    char thread[32];
};
static __noinit CrashRecord crashRecord;

//! Enters the board bootloader when the host has set the CDC ACM port to the board's touch baud rate
static void touchResetCheck() {
    U32 baud = 0;
    if ((uart_line_ctrl_get(serial, UART_LINE_CTRL_BAUD_RATE, &baud) == 0) &&
        (baud == Zephyr::Bootloader::TOUCH_BAUD)) {
        Zephyr::Bootloader::enter();
    }
}

extern "C" void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf* esf) {
    crashRecord.reason = reason;
    crashRecord.pc = (esf != nullptr) ? esf->basic.pc : 0;
    crashRecord.lr = (esf != nullptr) ? esf->basic.lr : 0;
    crashRecord.xpsr = (esf != nullptr) ? esf->basic.xpsr : 0;
#if defined(CONFIG_ARMV7_M_ARMV8_M_MAINLINE)
    crashRecord.cfsr = SCB->CFSR;
    crashRecord.hfsr = SCB->HFSR;
    crashRecord.mmfar = SCB->MMFAR;
    crashRecord.bfar = SCB->BFAR;
#else
    crashRecord.cfsr = 0;
    crashRecord.hfsr = 0;
    crashRecord.mmfar = 0;
    crashRecord.bfar = 0;
#endif
    struct k_thread* thread = k_current_get();
    crashRecord.stackStart = (thread != nullptr) ? static_cast<U32>(thread->stack_info.start) : 0;
    const char* name = (thread != nullptr) ? k_thread_name_get(thread) : nullptr;
    (void)strncpy(crashRecord.thread, (name != nullptr) ? name : "", sizeof(crashRecord.thread) - 1);
    crashRecord.thread[sizeof(crashRecord.thread) - 1] = '\0';
    crashRecord.magic = CRASH_RECORD_MAGIC;
    sys_reboot(SYS_REBOOT_WARM);
}

static void printCrashRecord(const CrashRecord& record, U32 resetCause) {
    printk("DoomFlight: reset cause 0x%08x\n", resetCause);
    printk("DoomFlight: FATAL reason %u thread '%s' stack 0x%08x\n", record.reason, record.thread, record.stackStart);
    printk("DoomFlight: pc 0x%08x lr 0x%08x xpsr 0x%08x\n", record.pc, record.lr, record.xpsr);
    printk("DoomFlight: cfsr 0x%08x hfsr 0x%08x mmfar 0x%08x bfar 0x%08x\n", record.cfsr, record.hfsr, record.mmfar,
           record.bfar);
}

#if defined(CONFIG_BOARD_TEENSY41)
static const struct device* const ethernetPhy = DEVICE_DT_GET(DT_NODELABEL(phy));

//! Reapplies the DP83825 configuration (RMII reference clock, advertised link modes) once the ENET MAC is running
static void configureEthernetPhy() {
    const int status = phy_configure_link(
        ethernetPhy,
        static_cast<enum phy_link_speed>(LINK_HALF_10BASE | LINK_FULL_10BASE | LINK_HALF_100BASE | LINK_FULL_100BASE),
        static_cast<enum phy_cfg_link_flag>(0));
    if (status != 0) {
        printk("DoomFlight: PHY configuration failed %d\n", status);
    }
}
#endif

//! Last FW_ASSERT kept across the warm reboot: the AssertFatalAdapter hook that owns asserts once the topology is up
//! ends in assert(false) -> k_panic, and its console message is lost when the CDC console is saturated by the downlink
struct LastAssertRecord {
    U32 magic;
    U32 line;
    U32 numArgs;
    U32 args[6];
    char file[120];
};
static constexpr U32 LAST_ASSERT_MAGIC = 0x4C415354;  // "LAST"
static __noinit LastAssertRecord lastAssertRecord;

static void printLastAssert(const LastAssertRecord& record) {
    printk("DoomFlight: last assert %s:%u args(%u) %u %u %u %u %u %u\n", record.file, record.line, record.numArgs,
           record.args[0], record.args[1], record.args[2], record.args[3], record.args[4], record.args[5]);
}

//! Records the assert, then hands it to the hook that was registered before (AssertFatalAdapter once the topology
//! is set up), which reports it and takes the fatal action
class RecordingAssertHook : public Fw::AssertHook {
  public:
    void install() {
        this->m_previous = Fw::AssertHook::getRegisteredHook();
        this->registerHook();
    }

    void reportAssert(FILE_NAME_ARG file,
                      FwSizeType lineNo,
                      FwSizeType numArgs,
                      FwAssertArgType arg1,
                      FwAssertArgType arg2,
                      FwAssertArgType arg3,
                      FwAssertArgType arg4,
                      FwAssertArgType arg5,
                      FwAssertArgType arg6) override {
        const FwAssertArgType args[6] = {arg1, arg2, arg3, arg4, arg5, arg6};
        lastAssertRecord.line = static_cast<U32>(lineNo);
        lastAssertRecord.numArgs = static_cast<U32>(numArgs);
        for (U32 i = 0; i < 6; i++) {
            lastAssertRecord.args[i] = static_cast<U32>(args[i]);
        }
#if FW_ASSERT_LEVEL == FW_FILENAME_ASSERT
        (void)strncpy(lastAssertRecord.file, file, sizeof(lastAssertRecord.file) - 1);
        lastAssertRecord.file[sizeof(lastAssertRecord.file) - 1] = '\0';
#else
        (void)snprintf(lastAssertRecord.file, sizeof(lastAssertRecord.file), "0x%08x", static_cast<unsigned>(file));
#endif
        lastAssertRecord.magic = LAST_ASSERT_MAGIC;
        if (this->m_previous != nullptr) {
            this->m_previous->reportAssert(file, lineNo, numArgs, arg1, arg2, arg3, arg4, arg5, arg6);
        } else {
            Fw::AssertHook::reportAssert(file, lineNo, numArgs, arg1, arg2, arg3, arg4, arg5, arg6);
        }
    }

    void doAssert() override {
        if (this->m_previous != nullptr) {
            this->m_previous->doAssert();
        } else {
            Fw::AssertHook::doAssert();
        }
    }

  private:
    Fw::AssertHook* m_previous = nullptr;
};

static RecordingAssertHook recordingAssertHook;

//! Watches for the touch reset before the topology starts. After a fatal error, stays here reporting it.
static void bootWindow() {
    U32 resetCause = 0;
    (void)hwinfo_get_reset_cause(&resetCause);
    Components::EngineProfiler::setResetCause(resetCause);
    (void)hwinfo_clear_reset_cause();
#if defined(CONFIG_BOARD_TEENSY41)
    configureEthernetPhy();
#endif
    const bool crashed = (crashRecord.magic == CRASH_RECORD_MAGIC);
    const CrashRecord record = crashRecord;
    crashRecord.magic = 0;
    const bool asserted = (lastAssertRecord.magic == LAST_ASSERT_MAGIC);
    const LastAssertRecord assertRecord = lastAssertRecord;
    lastAssertRecord.magic = 0;
    for (U32 tick = 0; crashed || asserted || (tick < (BOOT_WINDOW_SECONDS * 10)); tick++) {
        touchResetCheck();
        if ((tick % 10) == 0) {
            if (crashed || asserted) {
                if (crashed) {
                    printCrashRecord(record, resetCause);
                }
                if (asserted) {
                    printLastAssert(assertRecord);
                }
            } else {
                printk("DoomFlight: boot window %us, reset cause 0x%08x\n", BOOT_WINDOW_SECONDS - (tick / 10),
                       resetCause);
            }
        }
        k_sleep(K_MSEC(100));
    }
}

//! Holds the asserting thread instead of rebooting so the assert message reaches the console and the
//! touch reset can still reach the bootloader
class ParkingAssertHook : public Fw::AssertHook {
  public:
    void printAssert(const CHAR* msg) override { printk("%s\n", msg); }

    void doAssert() override {
        while (true) {
            touchResetCheck();
            k_sleep(K_MSEC(100));
        }
    }
};

ParkingAssertHook assertHook;

int main(int argc, char* argv[]) {
    // ** DO NOT REMOVE **//
    //
    // This wait is necessary to allow the USB CDC ACM interface to initialize before
    // the application starts writing to it.
    bootWindow();

    assertHook.registerHook();
    Os::init();
    // Object for communicating state to the topology
    DoomFlight::TopologyState inputs;
    inputs.uartDevice = serial;
    inputs.baudRate = 115200;
    inputs.hubRemoteAddress = "192.168.11.1";
    inputs.hubRemotePort = 50555;
    inputs.hubLocalPort = 50556;
    // The WAD lives on the LittleFS mount (boards/<board>.overlay fstab) and is uploaded there with fileUplink
    inputs.fileUplinkDirectory = "/lfs";
    inputs.wadPath = "/lfs/doom1.wad";
    inputs.doomAutoStart = true;

    // Setup, cycle, and teardown topology
#if defined(CONFIG_BOARD_TEENSY41)
    psram_heap_report();
#endif
    printk("DoomFlight: setting up topology\n");
    DoomFlight::setupTopology(inputs);
#if defined(CONFIG_BOARD_TEENSY41)
    screen_buffers_report();  // after setup: the engine allocated its buffers in setupTopology
#endif
    recordingAssertHook.install();  // after setup: wraps the AssertFatalAdapter hook registered during init
    printk("DoomFlight: starting rate groups\n");
    DoomFlight::startRateGroups();  // Program loop
    DoomFlight::teardownTopology(inputs);
    return 0;
}
