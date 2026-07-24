#include <windows_pointer/engine.hpp>

#include <gtest/gtest.h>

#include <array>
#include <string_view>

namespace windows_pointer {
namespace {

template <std::size_t Size>
void expectTrace(
    std::string_view                name,
    const std::array<Motion, Size>& raw,
    const std::array<Motion, Size>& expected) {
    Engine engine;

    for (std::size_t sample = 0; sample < Size; ++sample)
        EXPECT_EQ(engine.apply(raw[sample]), expected[sample]) << name << ", report " << sample;
}

TEST(Scenario, PickingUpADiscordCallWithoutMissingTheTinyButton) {
    constexpr std::array raw{
        Motion{18, 6},
        Motion{9, 3},
        Motion{4, 1},
        Motion{2, 0},
        Motion{1, 0},
        Motion{0, 1},
        Motion{-1, 0},
        Motion{2, -1},
    };
    constexpr std::array expected{
        Motion{20, 6},
        Motion{9, 3},
        Motion{4, 1},
        Motion{1, 0},
        Motion{0, 0},
        Motion{0, 1},
        Motion{0, 0},
        Motion{1, 0},
    };

    expectTrace("discord call", raw, expected);
}

TEST(Scenario, PlayingHypixelPvpWithAbsolutelyNormalArmMovements) {
    constexpr std::array raw{
        Motion{22, -4},
        Motion{35, 12},
        Motion{-28, 18},
        Motion{45, -20},
        Motion{-42, -8},
        Motion{8, 30},
        Motion{-15, -36},
        Motion{51, 5},
        Motion{-3, 2},
        Motion{30, -24},
    };
    constexpr std::array expected{
        Motion{26, -4},
        Motion{76, 25},
        Motion{-59, 38},
        Motion{103, -45},
        Motion{-93, -18},
        Motion{16, 61},
        Motion{-32, -78},
        Motion{116, 10},
        Motion{-1, 2},
        Motion{44, -36},
    };

    expectTrace("hypixel pvp", raw, expected);
}

TEST(Scenario, DraggingAFileIntoAWebsiteThenActuallyDroppingItThere) {
    constexpr std::array raw{
        Motion{1, 0},
        Motion{2, 1},
        Motion{3, 1},
        Motion{6, 2},
        Motion{12, 4},
        Motion{18, 6},
        Motion{24, 8},
        Motion{20, 7},
        Motion{10, 3},
        Motion{5, 2},
        Motion{2, 1},
        Motion{1, 0},
    };
    constexpr std::array expected{
        Motion{0, 0},
        Motion{1, 0},
        Motion{3, 1},
        Motion{5, 2},
        Motion{14, 4},
        Motion{30, 10},
        Motion{46, 16},
        Motion{36, 12},
        Motion{11, 4},
        Motion{5, 1},
        Motion{1, 1},
        Motion{0, 0},
    };

    expectTrace("drag and drop", raw, expected);
}

} // namespace
} // namespace windows_pointer
