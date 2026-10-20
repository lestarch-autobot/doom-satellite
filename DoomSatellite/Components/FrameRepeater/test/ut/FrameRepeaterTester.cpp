// ======================================================================
// \title  FrameRepeaterTester.cpp
// \brief  cpp file for FrameRepeater component test harness implementation class
// ======================================================================

#include "FrameRepeaterTester.hpp"

#include "Doom/DoomConfig/FppConstantsAc.hpp"

namespace Components {

constexpr FwSizeType FrameRepeaterTester::PIXELS;

FrameRepeaterTester::FrameRepeaterTester()
    : FrameRepeaterGTestBase("FrameRepeaterTester", FrameRepeaterTester::MAX_HISTORY_SIZE),
      component("FrameRepeater"),
      m_pixels{},
      m_frameOrder{},
      m_paletteOrder{},
      m_frameCalls(0),
      m_paletteCalls(0) {
    this->initComponents();
    this->connectPorts();
    for (FwSizeType i = 0; i < PIXELS; i++) {
        this->m_pixels[i] = static_cast<U8>(i * 3U);
    }
}

FrameRepeaterTester::~FrameRepeaterTester() {}

// ----------------------------------------------------------------------
// Handlers
// ----------------------------------------------------------------------

void FrameRepeaterTester::from_frameOut_handler(FwIndexType portNum,
                                                U32 frameNumber,
                                                U16 width,
                                                U16 height,
                                                Fw::Buffer& pixels) {
    this->pushFromPortEntry_frameOut(frameNumber, width, height, pixels);
    ASSERT_LT(this->m_frameCalls, FW_NUM_ARRAY_ELEMENTS(this->m_frameOrder));
    this->m_frameOrder[this->m_frameCalls++] = portNum;
    ASSERT_EQ(pixels.getData(), this->m_pixels);
    ASSERT_EQ(pixels.getSize(), PIXELS);
    // A consumer shrinking its view must not affect the next consumer
    pixels.setSize(0);
}

void FrameRepeaterTester::from_paletteOut_handler(FwIndexType portNum, const Doom::Palette& palette) {
    this->pushFromPortEntry_paletteOut(palette);
    ASSERT_LT(this->m_paletteCalls, FW_NUM_ARRAY_ELEMENTS(this->m_paletteOrder));
    this->m_paletteOrder[this->m_paletteCalls++] = portNum;
}

// ----------------------------------------------------------------------
// Tests
// ----------------------------------------------------------------------

void FrameRepeaterTester::testRepeatsFrames() {
    Fw::Buffer pixels(this->m_pixels, PIXELS);
    this->invoke_to_frameIn(0, 7, 8, 8, pixels);
    ASSERT_from_frameOut_SIZE(FrameRepeater::NUM_FRAMEOUT_OUTPUT_PORTS);
    ASSERT_EQ(this->m_frameCalls, static_cast<FwSizeType>(FrameRepeater::NUM_FRAMEOUT_OUTPUT_PORTS));
    for (FwIndexType i = 0; i < FrameRepeater::NUM_FRAMEOUT_OUTPUT_PORTS; i++) {
        ASSERT_EQ(this->m_frameOrder[i], i);
        const FromPortEntry_frameOut& entry = this->fromPortHistory_frameOut->at(static_cast<FwSizeType>(i));
        ASSERT_EQ(entry.frameNumber, 7U);
        ASSERT_EQ(entry.width, 8U);
        ASSERT_EQ(entry.height, 8U);
    }
    // The caller's buffer is untouched by consumers redirecting their own views
    ASSERT_EQ(pixels.getData(), this->m_pixels);
    ASSERT_EQ(pixels.getSize(), PIXELS);
    ASSERT_TLM_SIZE(1);
    ASSERT_TLM_FramesRepeated_SIZE(1);
    ASSERT_TLM_FramesRepeated(0, 1U);
    ASSERT_from_paletteOut_SIZE(0);

    this->m_frameCalls = 0;
    this->invoke_to_frameIn(0, 8, 8, 8, pixels);
    ASSERT_TLM_FramesRepeated(1, 2U);
}

void FrameRepeaterTester::testRepeatsPalettes() {
    Doom::Palette palette;
    palette.set_generation(3);
    for (FwSizeType i = 0; i < Doom::PALETTE_BYTES; i++) {
        palette.get_rgb()[i] = static_cast<U8>(i);
    }
    this->invoke_to_paletteIn(0, palette);
    ASSERT_from_paletteOut_SIZE(FrameRepeater::NUM_PALETTEOUT_OUTPUT_PORTS);
    for (FwIndexType i = 0; i < FrameRepeater::NUM_PALETTEOUT_OUTPUT_PORTS; i++) {
        ASSERT_EQ(this->m_paletteOrder[i], i);
        ASSERT_from_paletteOut(static_cast<FwSizeType>(i), palette);
    }
    ASSERT_TLM_SIZE(1);
    ASSERT_TLM_PalettesRepeated(0, 1U);
    ASSERT_from_frameOut_SIZE(0);
}

void FrameRepeaterTester::testSkipsUnconnectedOutputs() {
    // Only the last output of each array is connected
    FrameRepeater partial("PartialFrameRepeater");
    partial.init(TEST_INSTANCE_ID);
    const FwIndexType last = FrameRepeater::NUM_FRAMEOUT_OUTPUT_PORTS - 1;
    partial.set_frameOut_OutputPort(last, this->get_from_frameOut(last));
    partial.set_paletteOut_OutputPort(last, this->get_from_paletteOut(last));
    partial.set_tlmOut_OutputPort(0, this->get_from_tlmOut(0));
    partial.set_timeCaller_OutputPort(0, this->get_from_timeCaller(0));

    Fw::Buffer pixels(this->m_pixels, PIXELS);
    partial.get_frameIn_InputPort(0)->invoke(1, 8, 8, pixels);
    ASSERT_from_frameOut_SIZE(1);
    ASSERT_EQ(this->m_frameOrder[0], last);

    Doom::Palette palette;
    partial.get_paletteIn_InputPort(0)->invoke(palette);
    ASSERT_from_paletteOut_SIZE(1);
    ASSERT_EQ(this->m_paletteOrder[0], last);

    ASSERT_TLM_FramesRepeated(0, 1U);
    ASSERT_TLM_PalettesRepeated(0, 1U);
}

void FrameRepeaterTester::testOutputGate() {
    const FwIndexType last = FrameRepeater::NUM_FRAMEOUT_OUTPUT_PORTS - 1;
    Fw::Buffer pixels(this->m_pixels, PIXELS);
    Doom::Palette palette;

    // Disabled at setup: the slot is skipped for frames and palettes, the others still repeat
    this->component.setOutputEnabled(last, false);
    this->invoke_to_frameIn(0, 1, 8, 8, pixels);
    this->invoke_to_paletteIn(0, palette);
    ASSERT_from_frameOut_SIZE(FrameRepeater::NUM_FRAMEOUT_OUTPUT_PORTS - 1);
    ASSERT_from_paletteOut_SIZE(FrameRepeater::NUM_PALETTEOUT_OUTPUT_PORTS - 1);
    for (FwIndexType i = 0; i < FrameRepeater::NUM_FRAMEOUT_OUTPUT_PORTS - 1; i++) {
        ASSERT_EQ(this->m_frameOrder[static_cast<FwSizeType>(i)], i);
        ASSERT_EQ(this->m_paletteOrder[static_cast<FwSizeType>(i)], i);
    }
    ASSERT_TLM_FramesRepeated_SIZE(1);
    ASSERT_TLM_FramesRepeated(0, 1);

    // Re-enabled from the ground: the slot repeats again
    this->clearHistory();
    this->m_frameCalls = 0;
    this->m_paletteCalls = 0;
    this->sendCmd_SET_OUTPUT(0, 10, static_cast<U8>(last), Fw::Enabled::ENABLED);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, FrameRepeater::OPCODE_SET_OUTPUT, 10, Fw::CmdResponse::OK);
    ASSERT_EVENTS_OutputStateChanged_SIZE(1);
    ASSERT_EVENTS_OutputStateChanged(0, static_cast<U8>(last), Fw::Enabled::ENABLED);
    this->invoke_to_frameIn(0, 2, 8, 8, pixels);
    ASSERT_from_frameOut_SIZE(FrameRepeater::NUM_FRAMEOUT_OUTPUT_PORTS);

    // Disabled from the ground
    this->clearHistory();
    this->m_frameCalls = 0;
    this->m_paletteCalls = 0;
    this->sendCmd_SET_OUTPUT(0, 11, static_cast<U8>(last), Fw::Enabled::DISABLED);
    ASSERT_CMD_RESPONSE(0, FrameRepeater::OPCODE_SET_OUTPUT, 11, Fw::CmdResponse::OK);
    this->invoke_to_paletteIn(0, palette);
    ASSERT_from_paletteOut_SIZE(FrameRepeater::NUM_PALETTEOUT_OUTPUT_PORTS - 1);

    // A slot outside the arrays is rejected without touching the others
    this->clearHistory();
    this->m_frameCalls = 0;
    this->m_paletteCalls = 0;
    this->sendCmd_SET_OUTPUT(0, 12, static_cast<U8>(FrameRepeater::NUM_FRAMEOUT_OUTPUT_PORTS), Fw::Enabled::DISABLED);
    ASSERT_CMD_RESPONSE(0, FrameRepeater::OPCODE_SET_OUTPUT, 12, Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_OutputIndexInvalid_SIZE(1);
    ASSERT_EVENTS_OutputIndexInvalid(0, static_cast<U8>(FrameRepeater::NUM_FRAMEOUT_OUTPUT_PORTS));
    this->invoke_to_frameIn(0, 3, 8, 8, pixels);
    ASSERT_from_frameOut_SIZE(FrameRepeater::NUM_FRAMEOUT_OUTPUT_PORTS - 1);
}

}  // namespace Components
