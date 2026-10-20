// ======================================================================
// \title  FrameRepeaterTestMain.cpp
// \brief  cpp file for FrameRepeater component test main function
// ======================================================================

#include "FrameRepeaterTester.hpp"

TEST(Repeat, Frames) {
    Components::FrameRepeaterTester tester;
    tester.testRepeatsFrames();
}

TEST(Repeat, Palettes) {
    Components::FrameRepeaterTester tester;
    tester.testRepeatsPalettes();
}

TEST(Repeat, SkipsUnconnectedOutputs) {
    Components::FrameRepeaterTester tester;
    tester.testSkipsUnconnectedOutputs();
}

TEST(Repeat, OutputGate) {
    Components::FrameRepeaterTester tester;
    tester.testOutputGate();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
