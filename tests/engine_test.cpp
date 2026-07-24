#include <windows_pointer/engine.hpp>

#include <gtest/gtest.h>

#include <array>

namespace windows_pointer {
namespace {

TEST(PointerSpeed, ParsesTheWindows11Notation) {
    EXPECT_EQ(parsePointerSpeed("1/20"), 1);
    EXPECT_EQ(parsePointerSpeed("10/20"), 10);
    EXPECT_EQ(parsePointerSpeed("20/20"), 20);
}

TEST(PointerSpeed, RejectsEverythingElse) {
    for (const auto value : {"", "10", "0/20", "21/20", "6/11", " 10/20", "10/20 "})
        EXPECT_FALSE(parsePointerSpeed(value).has_value()) << value;
}

TEST(LinearMotion, DefaultSpeedIsOneToOne) {
    Engine engine({.pointerSpeed = 10, .enhancePointerPrecision = false});

    EXPECT_EQ(engine.apply({1, -1}), (Motion{1, -1}));
    EXPECT_EQ(engine.apply({19, 7}), (Motion{19, 7}));
}

TEST(LinearMotion, CarriesSubpixelRemainders) {
    Engine engine({.pointerSpeed = 8, .enhancePointerPrecision = false});

    EXPECT_EQ(engine.apply({1, 0}), (Motion{0, 0}));
    EXPECT_EQ(engine.apply({1, 0}), (Motion{1, 0}));
    EXPECT_EQ(engine.apply({1, 0}), (Motion{1, 0}));
    EXPECT_EQ(engine.apply({1, 0}), (Motion{1, 0}));
}

TEST(LinearMotion, MatchesTheFastestWindowsSetting) {
    Engine engine({.pointerSpeed = 20, .enhancePointerPrecision = false});

    EXPECT_EQ(engine.apply({2, -2}), (Motion{7, -7}));
}

TEST(EnhancedMotion, PreservesIndependentStatePerEngine) {
    Engine first;
    Engine second;

    EXPECT_EQ(first.apply({1, 0}), second.apply({1, 0}));
    (void)first.apply({30, 12});
    EXPECT_NE(first.apply({30, 12}), second.apply({30, 12}));
}

TEST(MotionState, ResetsOnlyWhenAsked) {
    Engine dirty;
    (void)dirty.apply({1, 1});
    (void)dirty.apply({20, 4});
    dirty.configure({.pointerSpeed = 11, .enhancePointerPrecision = true});
    dirty.reset();

    Engine clean({.pointerSpeed = 11, .enhancePointerPrecision = true});
    EXPECT_EQ(dirty.apply({6, -3}), clean.apply({6, -3}));
}

TEST(MotionState, KeepsLinearAndEnhancedRemaindersSeparate) {
    Engine engine({.pointerSpeed = 8, .enhancePointerPrecision = false});

    EXPECT_EQ(engine.apply({1, 0}), (Motion{0, 0}));

    engine.configure({.pointerSpeed = 10, .enhancePointerPrecision = true});
    (void)engine.apply({30, 12});

    engine.configure({.pointerSpeed = 8, .enhancePointerPrecision = false});
    EXPECT_EQ(engine.apply({1, 0}), (Motion{1, 0}));
}

TEST(DisplayDpi, ConvertsHyprlandScaleToWindowsDpi) {
    EXPECT_EQ(displayDpiFromScale(0.75), 96);
    EXPECT_EQ(displayDpiFromScale(1.0), 96);
    EXPECT_EQ(displayDpiFromScale(1.25), 120);
    EXPECT_EQ(displayDpiFromScale(1.5), 144);
    EXPECT_EQ(displayDpiFromScale(2.0), 192);
    EXPECT_EQ(displayDpiFromScale(99.0), 480);
}

TEST(DisplayDpi, SelectsTheCurveForTheCurrentDisplay) {
    Engine at100Percent;
    Engine at150Percent;

    EXPECT_NE(at100Percent.apply({12, 5}, 96), at150Percent.apply({12, 5}, 144));
}

TEST(EnhancedMotion, ZeroReportsDoNotForgetThePreviousSegment) {
    Engine withZero;
    Engine withoutZero;

    (void)withZero.apply({30, 12});
    (void)withoutZero.apply({30, 12});
    EXPECT_EQ(withZero.apply({0, 0}), (Motion{}));

    EXPECT_EQ(withZero.apply({30, 12}), withoutZero.apply({30, 12}));
}

TEST(EnhancedMotion, MatchesReferenceTraceAtWindowsDefaults) {
    Engine engine;

    constexpr std::array raw{
        Motion{1, 0},
        Motion{1, 0},
        Motion{2, 1},
        Motion{5, 2},
        Motion{12, 5},
        Motion{30, 12},
        Motion{-1, 0},
        Motion{-4, -2},
        Motion{-20, -7},
        Motion{3, -9},
    };

    // Generated once from the reverse-engineered win32k fixed-point reference.
    constexpr std::array expected{
        Motion{0, 0},
        Motion{1, 0},
        Motion{1, 0},
        Motion{4, 2},
        Motion{14, 6},
        Motion{63, 25},
        Motion{0, 0},
        Motion{-2, 0},
        Motion{-30, -11},
        Motion{2, -10},
    };

    for (std::size_t index = 0; index < raw.size(); ++index)
        EXPECT_EQ(engine.apply(raw[index]), expected[index]) << "sample " << index;
}

} // namespace
} // namespace windows_pointer
