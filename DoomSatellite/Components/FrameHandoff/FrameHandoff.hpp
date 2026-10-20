// ======================================================================
// \title  FrameHandoff.hpp
// \brief  hpp file for FrameHandoff component implementation class
// ======================================================================

#ifndef Components_FrameHandoff_HPP
#define Components_FrameHandoff_HPP

#include "Doom/DoomConfig/FppConstantsAc.hpp"
#include "DoomSatellite/Components/FrameHandoff/FrameHandoffComponentAc.hpp"
#include "Os/Mutex.hpp"

namespace Components {

class FrameHandoff final : public FrameHandoffComponentBase {
  public:
    //! Construct FrameHandoff object
    explicit FrameHandoff(const char* const compName  //!< The component name
    );

    //! Destroy FrameHandoff object
    ~FrameHandoff();

  private:
    static constexpr U16 FRAME_WIDTH = Doom::DOWNSAMPLED_WIDTH;
    static constexpr U16 FRAME_HEIGHT = Doom::DOWNSAMPLED_HEIGHT;
    static constexpr U32 FRAME_BYTES = static_cast<U32>(FRAME_WIDTH) * static_cast<U32>(FRAME_HEIGHT);
    static constexpr FwIndexType NUM_SLOTS = 2;
    static constexpr FwIndexType NO_SLOT = -1;

    //! One copied frame with the palette that preceded it (Doom::Palette is a serializable with a vtable: never memset)
    struct Slot {
        U32 frameNumber;
        Doom::Palette palette;
        U8 pixels[FRAME_BYTES];
    };

    //! Copies the frame (and the pending palette) into a free slot and publishes it; engine thread
    void frameIn_handler(FwIndexType portNum, U32 frameNumber, U16 width, U16 height, Fw::Buffer& pixels) override;

    //! Stores the palette for the next frame; engine thread
    void paletteIn_handler(FwIndexType portNum, const Doom::Palette& palette) override;

    //! Delivers the published slot downstream; this component's thread
    void deliver_internalInterfaceHandler() override;

    Os::Mutex m_lock;                 //!< Guards the slot bookkeeping below
    Slot m_slots[NUM_SLOTS];          //!< Frame copies
    FwIndexType m_published;          //!< Slot holding the latest complete frame not yet delivered, or NO_SLOT
    FwIndexType m_delivering;         //!< Slot being read by the delivery thread, or NO_SLOT
    Doom::Palette m_pendingPalette;   //!< Last palette received, copied into the slot with the next frame
    bool m_palettePending;            //!< m_pendingPalette holds a palette

    U32 m_framesIn;                   //!< Frames accepted
    U32 m_framesOut;                  //!< Frames delivered
    U32 m_framesOverwritten;          //!< Frames overwritten before delivery
    U32 m_deliverMaxUs;               //!< Longest delivery since the last DeliverMaxUs write
    U32 m_lastTlmSeconds;             //!< Time of the last telemetry write (whole seconds)
};

}  // namespace Components

#endif
