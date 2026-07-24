#include <windows_pointer/engine.hpp>

#include <charconv>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

auto parseNumber(std::string_view value, std::string_view name, int minimum, int maximum) -> int {
    int result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size() || result < minimum || result > maximum)
        throw std::runtime_error(std::string(name) + " must be between " + std::to_string(minimum) + " and " + std::to_string(maximum));
    return result;
}

auto parseMotion(std::string_view line) -> windows_pointer::Motion {
    const auto comma = line.find(',');
    if (comma == std::string_view::npos)
        throw std::runtime_error("input rows must look like 12,-4");

    return {
        .x = parseNumber(line.substr(0, comma), "x", -32768, 32767),
        .y = parseNumber(line.substr(comma + 1), "y", -32768, 32767),
    };
}

} // namespace

int main(int argc, char** argv) {
    try {
        windows_pointer::Settings settings;
        std::uint16_t              displayDpi = 96;

        for (int index = 1; index < argc; ++index) {
            const std::string_view argument = argv[index];
            if (argument == "--speed" && index + 1 < argc) {
                const auto speed = windows_pointer::parsePointerSpeed(argv[++index]);
                if (!speed)
                    throw std::runtime_error(speed.error());
                settings.pointerSpeed = *speed;
            } else if (argument == "--epp" && index + 1 < argc) {
                const std::string_view value = argv[++index];
                if (value == "on")
                    settings.enhancePointerPrecision = true;
                else if (value == "off")
                    settings.enhancePointerPrecision = false;
                else
                    throw std::runtime_error("--epp must be on or off");
            } else if (argument == "--dpi" && index + 1 < argc)
                displayDpi = static_cast<std::uint16_t>(parseNumber(argv[++index], "dpi", 96, 480));
            else
                throw std::runtime_error("usage: windows-pointer-replay [--speed 10/20] [--epp on|off] [--dpi 96..480] < trace.csv");
        }

        windows_pointer::Engine engine(settings);
        std::string             line;
        std::size_t             report = 0;

        std::cout << "# windows-pointer-linux replay v1\n"
                  << "# pointer_speed=" << static_cast<int>(settings.pointerSpeed) << "/20\n"
                  << "# enhance_pointer_precision=" << (settings.enhancePointerPrecision ? "true" : "false") << '\n'
                  << "# system_dpi=" << displayDpi << '\n'
                  << "report,raw_x,raw_y,output_x,output_y\n";

        while (std::getline(std::cin, line)) {
            if (line.empty() || line.starts_with('#'))
                continue;

            const auto raw    = parseMotion(line);
            const auto output = engine.apply(raw, displayDpi);
            std::cout << report++ << ',' << raw.x << ',' << raw.y << ',' << output.x << ',' << output.y << '\n';
        }

        if (report == 0)
            throw std::runtime_error("no input reports arrived on stdin");
    } catch (const std::exception& error) {
        std::cerr << "windows-pointer-replay: " << error.what() << '\n';
        return 1;
    }
}
