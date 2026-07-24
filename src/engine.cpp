#include <windows_pointer/engine.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <stdexcept>

namespace windows_pointer {
namespace {

constexpr std::array<std::int64_t, 5> SMOOTH_MOUSE_X_CURVE{
    0x0,
    0x6e15,
    0x14000,
    0x3dc29,
    0x280000,
};

constexpr std::array<std::int64_t, 5> SMOOTH_MOUSE_Y_CURVE{
    0x0,
    0x111fd,
    0x42400,
    0x12fc00,
    0x1bbc000,
};

// Fixed-point gains for the Windows 11 1-20 speed setting when EPP is off.
constexpr std::array<std::int64_t, 20> LINEAR_GAINS_Q16{
    2048,   // 0.03125
    4096,   // 0.0625
    8192,   // 0.125
    16384,  // 0.25
    24576,  // 0.375
    32768,  // 0.5
    40960,  // 0.625
    49152,  // 0.75
    57344,  // 0.875
    65536,  // 1.0
    81920,  // 1.25
    98304,  // 1.5
    114688, // 1.75
    131072, // 2.0
    147456, // 2.25
    163840, // 2.5
    180224, // 2.75
    196608, // 3.0
    212992, // 3.25
    229376, // 3.5
};

constexpr std::int64_t DEFAULT_DISPLAY_DPI = 96;

[[nodiscard]] auto narrow(std::int64_t value) -> std::int32_t {
    return static_cast<std::int32_t>(std::clamp(
        value,
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()),
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())));
}

} // namespace

auto parsePointerSpeed(std::string_view value) -> std::expected<std::uint8_t, std::string> {
    constexpr std::string_view suffix = "/20";

    if (!value.ends_with(suffix))
        return std::unexpected("pointer speed must look like \"10/20\"");

    const auto number = value.substr(0, value.size() - suffix.size());
    if (number.empty())
        return std::unexpected("pointer speed must look like \"10/20\"");

    unsigned speed = 0;
    const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), speed);
    if (error != std::errc{} || end != number.data() + number.size() || speed < 1 || speed > 20)
        return std::unexpected("pointer speed must be between \"1/20\" and \"20/20\"");

    return static_cast<std::uint8_t>(speed);
}

Engine::Engine(Settings settings) : m_settings(settings) {
    if (m_settings.pointerSpeed < 1 || m_settings.pointerSpeed > 20)
        throw std::invalid_argument("pointer speed must be between 1 and 20");

    buildCurve();
}

auto Engine::apply(Motion raw) -> Motion {
    return m_settings.enhancePointerPrecision ? applyEnhanced(raw) : applyLinear(raw);
}

void Engine::configure(Settings settings) {
    if (settings.pointerSpeed < 1 || settings.pointerSpeed > 20)
        throw std::invalid_argument("pointer speed must be between 1 and 20");

    if (settings == m_settings)
        return;

    m_settings = settings;
    buildCurve();
    reset();
}

void Engine::reset() {
    m_xRemainder      = 0;
    m_yRemainder      = 0;
    m_previousSegment = 0;
}

auto Engine::settings() const -> Settings {
    return m_settings;
}

void Engine::buildCurve() {
    const auto dpiFactor   = (DEFAULT_DISPLAY_DPI << 16) / 0x78;
    const auto speedFactor = (static_cast<std::int64_t>(m_settings.pointerSpeed) << 16) / 10;

    for (std::size_t index = 0; index < SMOOTH_MOUSE_X_CURVE.size(); ++index) {
        m_xScaled[index] = (SMOOTH_MOUSE_X_CURVE[index] * 0x38000) >> 16;

        const auto scaledY = (dpiFactor * SMOOTH_MOUSE_Y_CURVE[index]) >> 16;
        m_yScaled[index]   = (scaledY * speedFactor) >> 16;
    }

    for (std::size_t index = 0; index < m_slopes.size(); ++index) {
        const auto dx = m_xScaled[index + 1] - m_xScaled[index];
        const auto dy = m_yScaled[index + 1] - m_yScaled[index];

        m_slopes[index]     = dx == 0 ? 0 : (dy << 16) / dx;
        m_intercepts[index] = m_yScaled[index] - ((m_slopes[index] * m_xScaled[index]) >> 16);
    }
}

auto Engine::applyEnhanced(Motion raw) -> Motion {
    const auto xFixed = static_cast<std::int64_t>(raw.x) * Q16;
    const auto yFixed = static_cast<std::int64_t>(raw.y) * Q16;
    const auto absX   = xFixed >= 0 ? xFixed : -xFixed;
    const auto absY   = yFixed >= 0 ? yFixed : -yFixed;
    const auto distance = std::max(absX, absY) + (std::min(absX, absY) >> 1);

    std::size_t segment = 3;
    for (std::size_t index = 0; index < 3; ++index) {
        if (distance <= m_xScaled[index + 1]) {
            segment = index;
            break;
        }
    }

    auto gain = m_slopes[segment];
    if (distance != 0)
        gain += (m_intercepts[segment] << 16) / distance;

    if (m_previousSegment < segment) {
        auto previousGain = m_slopes[m_previousSegment];
        if (distance != 0)
            previousGain += (m_intercepts[m_previousSegment] << 16) / distance;
        gain = (gain + previousGain) >> 1;
    }

    m_previousSegment = segment;

    const auto xTotal = ((xFixed * gain) >> 16) + m_xRemainder;
    const auto yTotal = ((yFixed * gain) >> 16) + m_yRemainder;
    const auto x      = xTotal / Q16;
    const auto y      = yTotal / Q16;

    m_xRemainder = xTotal % Q16;
    m_yRemainder = yTotal % Q16;

    return {narrow(x), narrow(y)};
}

auto Engine::applyLinear(Motion raw) -> Motion {
    const auto gain   = LINEAR_GAINS_Q16.at(m_settings.pointerSpeed - 1);
    const auto xTotal = static_cast<std::int64_t>(raw.x) * gain + m_xRemainder;
    const auto yTotal = static_cast<std::int64_t>(raw.y) * gain + m_yRemainder;
    const auto x      = xTotal / Q16;
    const auto y      = yTotal / Q16;

    m_xRemainder = xTotal % Q16;
    m_yRemainder = yTotal % Q16;

    return {narrow(x), narrow(y)};
}

} // namespace windows_pointer
