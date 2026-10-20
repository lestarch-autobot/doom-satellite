// ======================================================================
// \title  FrameRepeater.hpp
// \brief  hpp file for FrameRepeater component implementation class
// ======================================================================

#ifndef Components_FrameRepeater_HPP
#define Components_FrameRepeater_HPP

#include "DoomSatellite/Components/FrameRepeater/FrameRepeaterComponentAc.hpp"

namespace Components {

class FrameRepeater final : public FrameRepeaterComponentBase {
  public:
    //! Construct FrameRepeater object
    explicit FrameRepeater(const char* const compName  //!< The component name
    );

    //! Destroy FrameRepeater object
    ~FrameRepeater();

    //! Enable or disable one output slot (both frameOut[output] and paletteOut[output]); all slots start enabled
    void setOutputEnabled(FwIndexType output, bool enabled);

  private:
    void frameIn_handler(FwIndexType portNum, U32 frameNumber, U16 width, U16 height, Fw::Buffer& pixels) override;

    void paletteIn_handler(FwIndexType portNum, const Doom::Palette& palette) override;

    //! Handler for command SET_OUTPUT
    void SET_OUTPUT_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U8 slot, const Fw::Enabled& setting) override;

    bool m_outputEnabled[NUM_FRAMEOUT_OUTPUT_PORTS];  //!< Per-slot enable; disabled slots are skipped
    U32 m_framesRepeated;                             //!< Frames repeated
    U32 m_palettesRepeated;                           //!< Palettes repeated
};

}  // namespace Components

#endif
