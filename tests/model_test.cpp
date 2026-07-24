#include "reference_model.hpp"

#include <gtest/gtest.h>

#include <array>
#include <random>

namespace windows_pointer {
namespace {

TEST(AuditedModel, MatchesEverySettingAcrossDisplayScales) {
    constexpr std::array<std::uint16_t, 5> displayDpis{96, 120, 144, 192, 480};

    for (std::uint8_t speed = 1; speed <= 20; ++speed) {
        for (const bool enhanced : {false, true}) {
            const Settings settings{
                .pointerSpeed            = speed,
                .enhancePointerPrecision = enhanced,
            };
            Engine               engine(settings);
            test::ReferenceModel reference(settings);
            std::mt19937         random(0x57494e + speed * 2 + enhanced);
            std::uniform_int_distribution<std::int32_t> delta(-256, 256);

            for (std::size_t sample = 0; sample < 2048; ++sample) {
                const auto raw = sample % 97 == 0 ? Motion{} : Motion{delta(random), delta(random)};
                const auto dpi = displayDpis.at(sample % displayDpis.size());

                EXPECT_EQ(engine.apply(raw, dpi), reference.apply(raw, dpi))
                    << "speed " << static_cast<int>(speed) << ", EPP " << enhanced << ", DPI " << dpi << ", sample " << sample;
            }
        }
    }
}

TEST(AuditedModel, MatchesAtSignedDeviceLimitsWithoutOverflowing) {
    constexpr std::array motions{
        Motion{32767, 32767},
        Motion{-32768, 32767},
        Motion{32767, -32768},
        Motion{-32768, -32768},
    };

    const Settings settings{
        .pointerSpeed            = 20,
        .enhancePointerPrecision = true,
    };
    Engine               engine(settings);
    test::ReferenceModel reference(settings);

    for (const auto motion : motions)
        EXPECT_EQ(engine.apply(motion, 480), reference.apply(motion, 480));
}

} // namespace
} // namespace windows_pointer
