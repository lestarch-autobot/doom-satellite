// ======================================================================
// \title  FrameRepeaterTester.hpp
// \brief  hpp file for FrameRepeater component test harness implementation class
// ======================================================================

#ifndef Components_FrameRepeaterTester_HPP
#define Components_FrameRepeaterTester_HPP

#include "DoomSatellite/Components/FrameRepeater/FrameRepeater.hpp"
#include "DoomSatellite/Components/FrameRepeater/FrameRepeaterGTestBase.hpp"

namespace Components {

class FrameRepeaterTester final : public FrameRepeaterGTestBase {
  public:
    static constexpr FwSizeType MAX_HISTORY_SIZE = 16;
    static constexpr FwEnumStoreType TEST_INSTANCE_ID = 0;
    static constexpr FwSizeType PIXELS = 64;

    FrameRepeaterTester();
    ~FrameRepeaterTester();

    void testRepeatsFrames();
    void testRepeatsPalettes();
    void testSkipsUnconnectedOutputs();
    void testOutputGate();

  private:
    void connectPorts();
    void initComponents();

    //! Record the output port order and check each consumer sees the caller's pixels
    void from_frameOut_handler(FwIndexType portNum,
                               U32 frameNumber,
                               U16 width,
                               U16 height,
                               Fw::Buffer& pixels) override;

    //! Record the output port order
    void from_paletteOut_handler(FwIndexType portNum, const Doom::Palette& palette) override;

    FrameRepeater component;

    U8 m_pixels[PIXELS];                                                     //!< Frame offered on frameIn
    FwIndexType m_frameOrder[FrameRepeater::NUM_FRAMEOUT_OUTPUT_PORTS];      //!< frameOut ports in invocation order
    FwIndexType m_paletteOrder[FrameRepeater::NUM_PALETTEOUT_OUTPUT_PORTS];  //!< paletteOut ports in order
    FwSizeType m_frameCalls;                                                 //!< frameOut invocations seen
    FwSizeType m_paletteCalls;                                               //!< paletteOut invocations seen
};

}  // namespace Components

#endif
