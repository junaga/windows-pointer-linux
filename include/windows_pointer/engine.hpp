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

    [[nodiscard]] auto apply(Motion raw) -> Motion;
    void               configure(Settings settings);
    void               reset();

    [[nodiscard]] auto settings() const -> Settings;

  private:
    static constexpr std::int64_t Q16 = 1LL << 16;

    void buildCurve();

    [[nodiscard]] auto applyEnhanced(Motion raw) -> Motion;
    [[nodiscard]] auto applyLinear(Motion raw) -> Motion;

    Settings                    m_settings;
    std::array<std::int64_t, 5> m_xScaled{};
    std::array<std::int64_t, 5> m_yScaled{};
    std::array<std::int64_t, 4> m_slopes{};
    std::array<std::int64_t, 4> m_intercepts{};
    std::int64_t                m_xRemainder = 0;
    std::int64_t                m_yRemainder = 0;
    std::size_t                 m_previousSegment = 0;
};

} // namespace windows_pointer

