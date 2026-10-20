// ======================================================================
// \title  FrameStreamer.cpp
// \brief  Streams the native DOOM frame to a UDP peer from its own thread, latest wins
// ======================================================================

#include "DoomSatellite/Components/FrameStreamer/FrameStreamer.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <zephyr/kernel.h>

#include "Fw/Types/Assert.hpp"
#include "Os/RawTime.hpp"
#include "Os/Task.hpp"

namespace Components {

static constexpr U8 KIND_PIXELS = 0;
static constexpr U8 KIND_PALETTE = 1;
static constexpr U8 FLAG_LAST = 0x01;
static constexpr U32 CHUNK_MAGIC = 0x44534631;  // "DSF1"
static constexpr U32 SEND_TIMEOUT_US = 100000;  // one chunk waits at most this long for TX buffers
static constexpr I32 NONBLOCK_FAILED_BASE = 1000;  // SocketFailed status = 1000 + errno from fcntl
static constexpr U32 SEND_BACKOFF_US = 2000;  // let the net TX thread drain after a dropped frame

FrameStreamer* FrameStreamer::s_instance = nullptr;

FrameStreamer::FrameStreamer(const char* const compName)
    : FrameStreamerComponentBase(compName),
      m_slots(),
      m_published(NO_SLOT),
      m_delivering(NO_SLOT),
      m_socket(),
      m_descriptor(),
      m_socketOpen(false),
      m_peerChanged(false),
      m_port(0),
      m_divisor(1),
      m_enabled(false),
      m_linkUp(nullptr),
      m_framesLinkDown(0),
      m_captured(0),
      m_seq(0),
      m_framesSent(0),
      m_framesSkipped(0),
      m_chunksSent(0),
      m_sendErrors(0),
      m_sendFailing(false),
      m_sendSumUs(0),
      m_sendCount(0),
      m_sendMaxUs(0),
      m_txSumUs(0),
      m_txCount(0),
      m_cpuPrevSelf(0),
      m_cpuPrevTotal(0),
      m_sendAgain(0),
      m_txTimeMaxUs(0),
      m_copyTimeMaxUs(0),
      m_lastTlmSeconds(0) {
    (void)::memset(this->m_address, 0, sizeof(this->m_address));
    (void)::memset(this->m_datagram, 0, sizeof(this->m_datagram));
    s_instance = this;
}

FrameStreamer::~FrameStreamer() {
    this->closeSocket();
    s_instance = nullptr;
}

FrameStreamer* FrameStreamer::instance() {
    return s_instance;
}

void FrameStreamer::configure(U8* slot0, U8* slot1, const char* address, U16 port, U8 divisor, bool enabled,
                              LinkUpFn linkUp) {
    FW_ASSERT(slot0 != nullptr);
    FW_ASSERT(slot1 != nullptr);
    FW_ASSERT(address != nullptr);
    FW_ASSERT(divisor > 0, divisor);
    this->m_slots[0].pixels = slot0;
    this->m_slots[1].pixels = slot1;
    (void)::strncpy(this->m_address, address, sizeof(this->m_address) - 1);
    this->m_address[sizeof(this->m_address) - 1] = '\0';
    this->m_port = port;
    this->m_divisor = divisor;
    this->m_enabled = enabled;
    this->m_linkUp = linkUp;
    this->m_peerChanged = true;
}

// ----------------------------------------------------------------------
// Engine thread
// ----------------------------------------------------------------------

void FrameStreamer::capture(const U8* pixels, const U8* bgra) {
    if (!this->m_enabled || (pixels == nullptr) || (bgra == nullptr) || (this->m_slots[0].pixels == nullptr)) {
        return;
    }
    this->m_captured++;
    const U8 divisor = (this->m_divisor == 0) ? 1 : this->m_divisor;
    if ((this->m_captured % divisor) != 0) {
        return;
    }
    Os::RawTime start;
    (void)start.now();

    FwIndexType target = NO_SLOT;
    this->m_lock.lock();
    target = (this->m_delivering == 0) ? 1 : 0;
    if (this->m_published != NO_SLOT) {
        // Published but not yet picked up: superseded by this frame
        this->m_framesSkipped++;
        this->m_published = NO_SLOT;
    }
    this->m_lock.unlock();

    Slot& slot = this->m_slots[target];
    (void)::memcpy(slot.pixels, pixels, FRAME_BYTES);
    for (U32 i = 0; i < 256; i++) {
        slot.palette[(3 * i) + 0] = bgra[(4 * i) + 2];
        slot.palette[(3 * i) + 1] = bgra[(4 * i) + 1];
        slot.palette[(3 * i) + 2] = bgra[(4 * i) + 0];
    }
    this->m_seq++;
    slot.seq = this->m_seq;

    this->m_lock.lock();
    this->m_published = target;
    this->m_lock.unlock();

    Os::RawTime end;
    (void)end.now();
    U32 elapsedUs = 0;
    if ((end.getDiffUsec(start, elapsedUs) == Os::RawTime::OP_OK) && (elapsedUs > this->m_copyTimeMaxUs)) {
        this->m_copyTimeMaxUs = elapsedUs;
    }
    this->deliver_internalInterfaceInvoke();
}

// ----------------------------------------------------------------------
// Streamer thread
// ----------------------------------------------------------------------

void FrameStreamer::deliver_internalInterfaceHandler() {
    this->m_lock.lock();
    const FwIndexType slot = this->m_published;
    this->m_published = NO_SLOT;
    this->m_delivering = slot;
    this->m_lock.unlock();

    if (slot != NO_SLOT) {
        // No carrier: drop the frame (counted) instead of handing it to a transmit path that cannot complete
        const bool linkUp = (this->m_linkUp == nullptr) || this->m_linkUp();
        if (!linkUp) {
            this->m_framesLinkDown++;
            this->m_framesSkipped++;
        } else if (this->m_enabled && this->ensureSocket()) {
            this->sendFrame(this->m_slots[slot]);
        }
        this->m_lock.lock();
        this->m_delivering = NO_SLOT;
        this->m_lock.unlock();
    }
    this->writeTelemetry();
}

bool FrameStreamer::ensureSocket() {
    char address[ADDRESS_SIZE];
    U16 port = 0;
    bool reopen = false;
    this->m_lock.lock();
    reopen = this->m_peerChanged;
    this->m_peerChanged = false;
    (void)::memcpy(address, this->m_address, sizeof(address));
    port = this->m_port;
    this->m_lock.unlock();

    if (reopen) {
        this->closeSocket();
        const Drv::SocketIpStatus configured = this->m_socket.configureSend(address, port, 0, SEND_TIMEOUT_US);
        if (configured != Drv::SOCK_SUCCESS) {
            this->log_WARNING_HI_SocketFailed(static_cast<I32>(configured));
            return false;
        }
    }
    if (!this->m_socketOpen) {
        const Drv::SocketIpStatus opened = this->m_socket.open(this->m_descriptor);
        if (opened != Drv::SOCK_SUCCESS) {
            this->log_WARNING_HI_SocketFailed(static_cast<I32>(opened));
            return false;
        }
        this->m_socketOpen = true;
        // Fail fast: a non-blocking socket makes Zephyr allocate packets/buffers with K_NO_WAIT, so a full TX pool
        // returns EAGAIN at once instead of sleeping for up to CONFIG_NET_SOCKET_MAX_SEND_WAIT (10 s). Latest-wins
        // then drops the frame (SendErrors) rather than stalling this thread.
        if (::fcntl(this->m_descriptor.fd, F_SETFL, O_NONBLOCK) != 0) {
            this->log_WARNING_HI_SocketFailed(static_cast<I32>(NONBLOCK_FAILED_BASE + errno));
        }
    }
    return true;
}

void FrameStreamer::closeSocket() {
    if (this->m_socketOpen) {
        this->m_socket.close(this->m_descriptor);
        this->m_socketOpen = false;
    }
}

bool FrameStreamer::sendChunk(U32 seq, U8 kind, U32 offset, const U8* data, U32 length, bool last) {
    FW_ASSERT(length <= CHUNK_PAYLOAD_BYTES, static_cast<FwAssertArgType>(length));
    U8* header = this->m_datagram;
    const U32 fields[] = {CHUNK_MAGIC, seq};
    U32 index = 0;
    for (U32 value : fields) {
        header[index++] = static_cast<U8>(value >> 24);
        header[index++] = static_cast<U8>(value >> 16);
        header[index++] = static_cast<U8>(value >> 8);
        header[index++] = static_cast<U8>(value);
    }
    header[index++] = static_cast<U8>(FRAME_WIDTH >> 8);
    header[index++] = static_cast<U8>(FRAME_WIDTH);
    header[index++] = static_cast<U8>(FRAME_HEIGHT >> 8);
    header[index++] = static_cast<U8>(FRAME_HEIGHT);
    header[index++] = static_cast<U8>(offset >> 24);
    header[index++] = static_cast<U8>(offset >> 16);
    header[index++] = static_cast<U8>(offset >> 8);
    header[index++] = static_cast<U8>(offset);
    header[index++] = static_cast<U8>(length >> 8);
    header[index++] = static_cast<U8>(length);
    header[index++] = kind;
    header[index++] = last ? FLAG_LAST : 0;
    FW_ASSERT(index == CHUNK_HEADER_BYTES, static_cast<FwAssertArgType>(index));
    (void)::memcpy(&this->m_datagram[CHUNK_HEADER_BYTES], data, length);

    Os::RawTime sendStart;
    (void)sendStart.now();
    const Drv::SocketIpStatus status =
        this->m_socket.send(this->m_descriptor, this->m_datagram, CHUNK_HEADER_BYTES + length);
    Os::RawTime sendEnd;
    (void)sendEnd.now();
    U32 sendUs = 0;
    if (sendEnd.getDiffUsec(sendStart, sendUs) == Os::RawTime::OP_OK) {
        this->m_sendSumUs += sendUs;
        this->m_sendCount++;
        if (sendUs > this->m_sendMaxUs) {
            this->m_sendMaxUs = sendUs;
        }
    }
    if (status != Drv::SOCK_SUCCESS) {
        this->m_sendErrors++;
        if (status == Drv::SOCK_INTERRUPTED_TRY_AGAIN) {
            this->m_sendAgain++;
        }
        if (!this->m_sendFailing) {
            // Only the first failure of a run is reported; the counter carries the rest (46 chunks per frame)
            this->m_sendFailing = true;
            this->log_WARNING_LO_SendFailed(static_cast<I32>(status));
        }
        if (status == Drv::SOCK_DISCONNECTED) {
            this->closeSocket();
        }
        return false;
    }
    this->m_sendFailing = false;
    this->m_chunksSent++;
    return true;
}

void FrameStreamer::sendFrame(const Slot& slot) {
    Os::RawTime start;
    (void)start.now();
    bool ok = this->sendChunk(slot.seq, KIND_PALETTE, 0, slot.palette, PALETTE_BYTES, false);
    for (U32 offset = 0; ok && (offset < FRAME_BYTES); offset += CHUNK_PAYLOAD_BYTES) {
        const U32 remaining = FRAME_BYTES - offset;
        const U32 length = (remaining < CHUNK_PAYLOAD_BYTES) ? remaining : CHUNK_PAYLOAD_BYTES;
        ok = this->sendChunk(slot.seq, KIND_PIXELS, offset, &slot.pixels[offset], length, (offset + length) >= FRAME_BYTES);
    }
    if (ok) {
        this->m_framesSent++;
    } else {
        // A failing link must not turn into a tight send/fail loop: hold this thread off for a while. Frames captured
        // meanwhile are simply replaced in the slots (FramesSkipped).
        Os::Task::delay(Fw::TimeInterval(0, SEND_BACKOFF_US));
    }
    Os::RawTime end;
    (void)end.now();
    U32 elapsedUs = 0;
    if ((end.getDiffUsec(start, elapsedUs) == Os::RawTime::OP_OK) && (elapsedUs > this->m_txTimeMaxUs)) {
        this->m_txTimeMaxUs = elapsedUs;
    }
    this->m_txSumUs += elapsedUs;
    this->m_txCount++;
}

void FrameStreamer::writeTelemetry() {
    const Fw::Time now = this->getTime();
    if (now.getSeconds() == this->m_lastTlmSeconds) {
        return;
    }
    this->m_lastTlmSeconds = now.getSeconds();
    this->tlmWrite_FramesSent(this->m_framesSent, now);
    this->tlmWrite_FramesSkipped(this->m_framesSkipped, now);
    this->tlmWrite_ChunksSent(this->m_chunksSent, now);
    this->tlmWrite_SendErrors(this->m_sendErrors, now);
    this->tlmWrite_FramesLinkDown(this->m_framesLinkDown, now);
    this->tlmWrite_SockFlags(this->m_socketOpen ? static_cast<I32>(::fcntl(this->m_descriptor.fd, F_GETFL, 0)) : -1, now);
    this->tlmWrite_SendAgain(this->m_sendAgain, now);
    this->tlmWrite_SendAvgUs((this->m_sendCount > 0) ? static_cast<U32>(this->m_sendSumUs / this->m_sendCount) : 0, now);
    this->tlmWrite_SendMaxUs(this->m_sendMaxUs, now);
    this->tlmWrite_TxAvgUs((this->m_txCount > 0) ? static_cast<U32>(this->m_txSumUs / this->m_txCount) : 0, now);
    this->m_sendSumUs = 0;
    this->m_sendCount = 0;
    this->m_sendMaxUs = 0;
    this->m_txSumUs = 0;
    this->m_txCount = 0;
    k_thread_runtime_stats_t self;
    k_thread_runtime_stats_t all;
    if ((k_thread_runtime_stats_get(k_current_get(), &self) == 0) && (k_thread_runtime_stats_all_get(&all) == 0)) {
        const U64 totalDelta = all.total_cycles - this->m_cpuPrevTotal;
        const U64 selfDelta = self.execution_cycles - this->m_cpuPrevSelf;
        this->m_cpuPrevTotal = all.total_cycles;
        this->m_cpuPrevSelf = self.execution_cycles;
        this->tlmWrite_CpuPct((totalDelta > 0) ? static_cast<U8>((selfDelta * 100U) / totalDelta) : 0, now);
    }
    this->tlmWrite_TxTimeMaxUs(this->m_txTimeMaxUs, now);
    this->tlmWrite_CopyTimeMaxUs(this->m_copyTimeMaxUs, now);
}

void FrameStreamer::reportState() {
    this->m_lock.lock();
    Fw::LogStringArg address(this->m_address);
    const U16 port = this->m_port;
    this->m_lock.unlock();
    this->log_ACTIVITY_HI_StreamState(this->m_enabled ? Fw::Enabled::ENABLED : Fw::Enabled::DISABLED,
                                      this->m_divisor, address, port);
}

// ----------------------------------------------------------------------
// Commands (command dispatcher thread)
// ----------------------------------------------------------------------

void FrameStreamer::ENABLE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const Fw::Enabled& enable) {
    this->m_enabled = (enable == Fw::Enabled::ENABLED);
    this->reportState();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void FrameStreamer::SET_RATE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U8 divisor) {
    if (divisor == 0) {
        this->log_WARNING_LO_RateRejected(divisor);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->m_divisor = divisor;
    this->reportState();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void FrameStreamer::SET_PEER_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const Fw::CmdStringArg& address, U16 port) {
    this->m_lock.lock();
    (void)::strncpy(this->m_address, address.toChar(), sizeof(this->m_address) - 1);
    this->m_address[sizeof(this->m_address) - 1] = '\0';
    this->m_port = port;
    this->m_peerChanged = true;
    this->m_lock.unlock();
    this->reportState();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace Components

// C entry for the link-time engine hook (DoomFlight/EngineHooks.c)
extern "C" void frame_streamer_capture(const uint8_t* pixels, const uint8_t* bgra) {
    Components::FrameStreamer* const streamer = Components::FrameStreamer::instance();
    if (streamer != nullptr) {
        streamer->capture(pixels, bgra);
    }
}
