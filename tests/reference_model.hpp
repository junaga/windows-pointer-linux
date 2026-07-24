#pragma once

#include <windows_pointer/engine.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

namespace windows_pointer::test {

// Kept deliberately separate from Engine. This is the straight-line model
// transcribed from the audited win32kbase routines, not a helper used by them.
class ReferenceModel {
  public:
    explicit ReferenceModel(Settings settings = {}) : m_settings(settings) {}

    auto apply(Motion raw, std::uint16_t displayDpi = 96) -> Motion {
        return m_settings.enhancePointerPrecision ? applyEnhanced(raw, displayDpi) : applyLinear(raw);
    }

  private:
    static constexpr std::int64_t Q16 = 1LL << 16;

    struct Ballistics {
        std::array<std::int64_t, 5> x{};
        std::array<std::int64_t, 4> slope{};
        std::array<std::int64_t, 4> intercept{};
    };

    void build(std::uint16_t displayDpi) {
        constexpr std::array xCurve{0LL, 0x6e15LL, 0x14000LL, 0x3dc29LL, 0x280000LL};
        constexpr std::array yCurve{0LL, 0x111fdLL, 0x42400LL, 0x12fc00LL, 0x1bbc000LL};

        displayDpi           = std::clamp<std::uint16_t>(displayDpi, 96, 480);
        const auto dpiFactor = (static_cast<std::int64_t>(displayDpi) << 16) / 120;
        const auto speed     = (static_cast<std::int64_t>(m_settings.pointerSpeed) << 16) / 10;
        std::array<std::int64_t, 5> y{};

        for (std::size_t index = 0; index < xCurve.size(); ++index) {
            m_ballistics.x[index] = (xCurve[index] * 0x38000) >> 16;
            y[index] = (((dpiFactor * yCurve[index]) >> 16) * speed) >> 16;
        }

        for (std::size_t index = 0; index < m_ballistics.slope.size(); ++index) {
            const auto dx = m_ballistics.x[index + 1] - m_ballistics.x[index];
            const auto dy = y[index + 1] - y[index];

            m_ballistics.slope[index]     = (dy << 16) / dx;
            m_ballistics.intercept[index] = y[index] - ((m_ballistics.slope[index] * m_ballistics.x[index]) >> 16);
        }

        m_dpi = displayDpi;
    }

    auto applyEnhanced(Motion raw, std::uint16_t displayDpi) -> Motion {
        if (raw == Motion{})
            return {};

        displayDpi = std::clamp<std::uint16_t>(displayDpi, 96, 480);
        if (displayDpi != m_dpi)
            build(displayDpi);

        const auto x = static_cast<std::int64_t>(raw.x) << 16;
        const auto y = static_cast<std::int64_t>(raw.y) << 16;
        const auto ax = x < 0 ? -x : x;
        const auto ay = y < 0 ? -y : y;
        const auto distance = std::max(ax, ay) + std::min(ax, ay) / 2;

        std::size_t segment = 0;
        while (segment < 3 && distance > m_ballistics.x[segment + 1])
            ++segment;

        auto gain = m_ballistics.slope[segment] + (m_ballistics.intercept[segment] << 16) / distance;
        if (m_previousSegment < segment) {
            const auto oldGain =
                m_ballistics.slope[m_previousSegment] + (m_ballistics.intercept[m_previousSegment] << 16) / distance;
            gain = (gain + oldGain) >> 1;
        }
        m_previousSegment = segment;

        const auto xTotal = ((x * gain) >> 16) + m_enhancedX;
        const auto yTotal = ((y * gain) >> 16) + m_enhancedY;
        const auto output = Motion{
            .x = static_cast<std::int32_t>(xTotal / Q16),
            .y = static_cast<std::int32_t>(yTotal / Q16),
        };

        m_enhancedX = xTotal % Q16;
        m_enhancedY = yTotal % Q16;
        return output;
    }

    auto applyLinear(Motion raw) -> Motion {
        constexpr std::array<std::int64_t, 20> gains{
            2048, 4096, 8192, 16384, 24576, 32768, 40960, 49152, 57344, 65536,
            81920, 98304, 114688, 131072, 147456, 163840, 180224, 196608, 212992, 229376,
        };

        const auto gain   = gains.at(m_settings.pointerSpeed - 1);
        const auto xTotal = static_cast<std::int64_t>(raw.x) * gain + m_linearX;
        const auto yTotal = static_cast<std::int64_t>(raw.y) * gain + m_linearY;
        const auto output = Motion{
            .x = static_cast<std::int32_t>(xTotal / Q16),
            .y = static_cast<std::int32_t>(yTotal / Q16),
        };

        m_linearX = xTotal % Q16;
        m_linearY = yTotal % Q16;
        return output;
    }

    Settings      m_settings;
    Ballistics    m_ballistics;
    std::uint16_t m_dpi = 0;

    std::int64_t m_enhancedX     = 0;
    std::int64_t m_enhancedY     = 0;
    std::size_t  m_previousSegment = 0;
    std::int64_t m_linearX       = 0;
    std::int64_t m_linearY       = 0;
};

} // namespace windows_pointer::test
