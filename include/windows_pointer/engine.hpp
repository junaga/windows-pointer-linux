#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace windows_pointer {

struct Motion {
    std::int32_t x = 0;
    std::int32_t y = 0;

    auto operator==(const Motion&) const -> bool = default;
};

struct Settings {
    std::uint8_t pointerSpeed            = 10;
    bool         enhancePointerPrecision = true;

    auto operator==(const Settings&) const -> bool = default;
};

[[nodiscard]] auto parsePointerSpeed(std::string_view value) -> std::expected<std::uint8_t, std::string>;

class Engine {
  public:
    explicit Engine(Settings settings = {});

    [[nodiscard]] auto apply(Motion raw, std::uint16_t displayDpi = 96) -> Motion;
    void               configure(Settings settings);
    void               reset();

    [[nodiscard]] auto settings() const -> Settings;

  private:
    struct Ballistics {
        std::array<std::int64_t, 5> x{};
        std::array<std::int64_t, 4> slopes{};
        std::array<std::int64_t, 4> intercepts{};
    };

    void buildCurve(std::uint16_t displayDpi);

    [[nodiscard]] auto applyEnhanced(Motion raw, std::uint16_t displayDpi) -> Motion;
    [[nodiscard]] auto applyLinear(Motion raw) -> Motion;

    Settings      m_settings;
    Ballistics    m_ballistics;
    std::uint16_t m_ballisticsDpi = 0;

    std::int64_t m_enhancedXRemainder = 0;
    std::int64_t m_enhancedYRemainder = 0;
    std::size_t  m_previousSegment     = 0;

    std::int64_t m_linearXRemainder = 0;
    std::int64_t m_linearYRemainder = 0;
};

[[nodiscard]] auto displayDpiFromScale(double scale) -> std::uint16_t;

} // namespace windows_pointer
