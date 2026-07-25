#include <windows_pointer/engine.hpp>

#include <bit>
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

    for (std::size_t offset = 2; offset + 8 < size; offset += 9) {
        const auto xBits =
            static_cast<std::uint32_t>(data[offset]) |
            static_cast<std::uint32_t>(data[offset + 1]) << 8 |
            static_cast<std::uint32_t>(data[offset + 2]) << 16 |
            static_cast<std::uint32_t>(data[offset + 3]) << 24;
        const auto yBits =
            static_cast<std::uint32_t>(data[offset + 4]) |
            static_cast<std::uint32_t>(data[offset + 5]) << 8 |
            static_cast<std::uint32_t>(data[offset + 6]) << 16 |
            static_cast<std::uint32_t>(data[offset + 7]) << 24;
        const auto dpi = static_cast<std::uint16_t>(96 + data[offset + 8] * 384 / 255);

        const auto output = engine.apply(
            {
                .x = std::bit_cast<std::int32_t>(xBits),
                .y = std::bit_cast<std::int32_t>(yBits),
            },
            dpi);
        (void)output;
    }

    return 0;
}
