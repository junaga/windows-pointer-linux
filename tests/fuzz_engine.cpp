#include <windows_pointer/engine.hpp>

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size < 2)
        return 0;

    const windows_pointer::Settings settings{
        .pointerSpeed            = static_cast<std::uint8_t>(data[0] % 20 + 1),
        .enhancePointerPrecision = (data[1] & 1) != 0,
    };
    windows_pointer::Engine engine(settings);

    for (std::size_t offset = 2; offset + 4 < size; offset += 5) {
        const auto x = static_cast<std::int16_t>(
            static_cast<std::uint16_t>(data[offset]) |
            static_cast<std::uint16_t>(data[offset + 1]) << 8);
        const auto y = static_cast<std::int16_t>(
            static_cast<std::uint16_t>(data[offset + 2]) |
            static_cast<std::uint16_t>(data[offset + 3]) << 8);
        const auto dpi = static_cast<std::uint16_t>(96 + data[offset + 4] * 384 / 255);

        const auto output = engine.apply({x, y}, dpi);
        (void)output;
    }

    return 0;
}
