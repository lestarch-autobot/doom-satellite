// ======================================================================
// \title  FrameHandoff.cpp
// \brief  cpp file for FrameHandoff component implementation class
// ======================================================================

#include "DoomSatellite/Components/FrameHandoff/FrameHandoff.hpp"

#include <cstring>

#include "Fw/Types/Assert.hpp"
#include "Os/RawTime.hpp"

namespace Components {

FrameHandoff::FrameHandoff(const char* const compName)
    : FrameHandoffComponentBase(compName),
      m_slots(),
      m_published(NO_SLOT),
      m_delivering(NO_SLOT),
      m_palettePending(false),
      m_framesIn(0),
      m_framesOut(0),
      m_framesOverwritten(0),
      m_deliverMaxUs(0),
      m_lastTlmSeconds(0) {}

FrameHandoff::~FrameHandoff() {}

void FrameHandoff::paletteIn_handler(FwIndexType portNum, const Doom::Palette& palette) {
    this->m_pendingPalette = palette;
    this->m_palettePending = true;
}

void FrameHandoff::frameIn_handler(FwIndexType portNum, U32 frameNumber, U16 width, U16 height, Fw::Buffer& pixels) {
    FW_ASSERT(width == FRAME_WIDTH, static_cast<FwAssertArgType>(width));
    FW_ASSERT(height == FRAME_HEIGHT, static_cast<FwAssertArgType>(height));
    FW_ASSERT(pixels.getSize() >= FRAME_BYTES, static_cast<FwAssertArgType>(pixels.getSize()));
    FW_ASSERT(pixels.getData() != nullptr);

    // The delivery thread owns at most one slot at a time, so the other one is always free to write
    this->m_lock.lock();
    const FwIndexType slot = (this->m_delivering == 0) ? 1 : 0;
    this->m_lock.unlock();

    Slot& target = this->m_slots[slot];
    target.frameNumber = frameNumber;
    if (this->m_palettePending) {
        target.palette = this->m_pendingPalette;
    }
    (void)::memcpy(target.pixels, pixels.getData(), FRAME_BYTES);

    this->m_lock.lock();
    if (this->m_published != NO_SLOT) {
        this->m_framesOverwritten++;
    }
    this->m_published = slot;
    this->m_lock.unlock();
    this->m_framesIn++;

    this->deliver_internalInterfaceInvoke();
}

void FrameHandoff::deliver_internalInterfaceHandler() {
    this->m_lock.lock();
    const FwIndexType slot = this->m_published;
    this->m_published = NO_SLOT;
    this->m_delivering = slot;
    this->m_lock.unlock();

    if (slot != NO_SLOT) {
        Os::RawTime start;
        (void)start.now();
        const Slot& source = this->m_slots[slot];
        if (this->isConnected_paletteOut_OutputPort(0)) {
            this->paletteOut_out(0, source.palette);
        }
        if (this->isConnected_frameOut_OutputPort(0)) {
            // The port offers mutable pixels for the duration of the call; the slot is this thread's until released
            Fw::Buffer view(const_cast<U8*>(source.pixels), FRAME_BYTES);
            this->frameOut_out(0, source.frameNumber, FRAME_WIDTH, FRAME_HEIGHT, view);
        }
        this->m_framesOut++;
        Os::RawTime end;
        (void)end.now();
        U32 elapsedUs = 0;
        if (end.getDiffUsec(start, elapsedUs) == Os::RawTime::OP_OK && elapsedUs > this->m_deliverMaxUs) {
            this->m_deliverMaxUs = elapsedUs;
        }

        this->m_lock.lock();
        this->m_delivering = NO_SLOT;
        this->m_lock.unlock();
    }

    const U32 seconds = this->getTime().getSeconds();
    if (seconds != this->m_lastTlmSeconds) {
        this->m_lastTlmSeconds = seconds;
        this->tlmWrite_FramesIn(this->m_framesIn);
        this->tlmWrite_FramesOut(this->m_framesOut);
        this->tlmWrite_FramesOverwritten(this->m_framesOverwritten);
        this->tlmWrite_DeliverMaxUs(this->m_deliverMaxUs);
        this->m_deliverMaxUs = 0;
    }
}

}  // namespace Components
