// ======================================================================
// \title  FrameStreamer.hpp
// \brief  Streams the native DOOM frame to a UDP peer from its own thread, latest wins
// ======================================================================

#ifndef Components_FrameStreamer_HPP
#define Components_FrameStreamer_HPP

#include "Doom/DoomConfig/FppConstantsAc.hpp"
#include "DoomSatellite/Components/FrameStreamer/FrameStreamerComponentAc.hpp"
#include "Drv/Ip/UdpSocket.hpp"
#include "Os/Mutex.hpp"

namespace Components {

class FrameStreamer final : public FrameStreamerComponentBase {
  public:
    static constexpr U16 FRAME_WIDTH = Doom::FRAME_WIDTH;
    static constexpr U16 FRAME_HEIGHT = Doom::FRAME_HEIGHT;
    static constexpr U32 FRAME_BYTES = static_cast<U32>(FRAME_WIDTH) * static_cast<U32>(FRAME_HEIGHT);
    static constexpr U32 PALETTE_BYTES = Doom::PALETTE_BYTES;
    //! Chunk wire format (big-endian): magic "DSF1" U32, frameSeq U32, width U16, height U16, offset U32,
    //! length U16, kind U8 (0 pixels, 1 palette), flags U8 (bit 0: last chunk of the frame); then length bytes.
    static constexpr U32 CHUNK_HEADER_BYTES = 20;
    //! Largest UDP payload that fits one Ethernet frame without IPv4 fragmentation (1500 - 20 IP - 8 UDP)
    static constexpr U32 DATAGRAM_MAX_BYTES = 1472;
    static constexpr U32 CHUNK_PAYLOAD_BYTES = DATAGRAM_MAX_BYTES - CHUNK_HEADER_BYTES;
    static constexpr U32 ADDRESS_SIZE = 16;
    static constexpr FwIndexType NUM_SLOTS = 2;
    static constexpr FwIndexType NO_SLOT = -1;

    explicit FrameStreamer(const char* const compName);
    ~FrameStreamer();

    //! Deployment-provided slot storage (each FRAME_BYTES), peer and boot state. Call before the tasks start.
    //! Deployment hook answering whether the link carrier is up; nullptr = always send
    using LinkUpFn = bool (*)();

    void configure(U8* slot0, U8* slot1, const char* address, U16 port, U8 divisor, bool enabled,
                   LinkUpFn linkUp = nullptr);

    //! Capture hook, called on the engine thread with the native frame and the engine palette (256 x BGRA bytes)
    void capture(const U8* pixels, const U8* bgra);

    //! The instance the C capture hook forwards to (nullptr before construction)
    static FrameStreamer* instance();

  private:
    struct Slot {
        U32 seq;
        U8* pixels;
        U8 palette[PALETTE_BYTES];
    };

    void deliver_internalInterfaceHandler() override;
    void ENABLE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const Fw::Enabled& enable) override;
    void SET_RATE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U8 divisor) override;
    void SET_PEER_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const Fw::CmdStringArg& address, U16 port) override;

    bool ensureSocket();
    void closeSocket();
    bool sendChunk(U32 seq, U8 kind, U32 offset, const U8* data, U32 length, bool last);
    void sendFrame(const Slot& slot);
    void reportState();
    void writeTelemetry();

    static FrameStreamer* s_instance;

    Os::Mutex m_lock;
    Slot m_slots[NUM_SLOTS];
    FwIndexType m_published;
    FwIndexType m_delivering;

    Drv::UdpSocket m_socket;
    Drv::SocketDescriptor m_descriptor;
    bool m_socketOpen;
    bool m_peerChanged;
    char m_address[ADDRESS_SIZE];
    U16 m_port;
    U8 m_divisor;
    bool m_enabled;
    LinkUpFn m_linkUp;
    U32 m_framesLinkDown;

    U32 m_captured;
    U32 m_seq;
    U32 m_framesSent;
    U32 m_framesSkipped;
    U32 m_chunksSent;
    U32 m_sendErrors;
    bool m_sendFailing;  //!< a send failed since the last successful chunk (event rate limit)
    U32 m_txTimeMaxUs;
    U32 m_copyTimeMaxUs;
    U32 m_lastTlmSeconds;
    U8 m_datagram[DATAGRAM_MAX_BYTES];
};

}  // namespace Components

#endif
