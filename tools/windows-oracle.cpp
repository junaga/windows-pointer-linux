#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct Motion {
    LONG x = 0;
    LONG y = 0;
};

[[noreturn]] void fail(std::string_view operation) {
    throw std::runtime_error(std::string(operation) + " failed with Win32 error " + std::to_string(GetLastError()));
}

void systemParameters(UINT action, void* value, UINT flags = 0) {
    if (!SystemParametersInfoW(action, 0, value, flags))
        fail("SystemParametersInfoW");
}

class DesktopSettings {
  public:
    DesktopSettings() {
        systemParameters(SPI_GETMOUSE, m_mouse.data());
        systemParameters(SPI_GETMOUSESPEED, &m_speed);
        if (!GetCursorPos(&m_cursor))
            fail("GetCursorPos");
    }

    DesktopSettings(const DesktopSettings&)            = delete;
    DesktopSettings& operator=(const DesktopSettings&) = delete;

    ~DesktopSettings() {
        SystemParametersInfoW(SPI_SETMOUSE, 0, m_mouse.data(), SPIF_SENDCHANGE);
        SystemParametersInfoW(
            SPI_SETMOUSESPEED,
            0,
            reinterpret_cast<void*>(static_cast<std::intptr_t>(m_speed)),
            SPIF_SENDCHANGE);
        SetCursorPos(m_cursor.x, m_cursor.y);
    }

  private:
    std::array<int, 3> m_mouse{};
    int                m_speed = 10;
    POINT              m_cursor{};
};

auto parseNumber(std::string_view value, std::string_view name, int minimum, int maximum) -> int {
    int result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size() || result < minimum || result > maximum)
        throw std::runtime_error(std::string(name) + " must be between " + std::to_string(minimum) + " and " + std::to_string(maximum));
    return result;
}

auto parseMotion(std::string_view line) -> Motion {
    const auto comma = line.find(',');
    if (comma == std::string_view::npos)
        throw std::runtime_error("input rows must look like 12,-4");

    return {
        .x = static_cast<LONG>(parseNumber(line.substr(0, comma), "x", -32768, 32767)),
        .y = static_cast<LONG>(parseNumber(line.substr(comma + 1), "y", -32768, 32767)),
    };
}

auto readTrace() -> std::vector<Motion> {
    std::vector<Motion> trace;
    std::string         line;

    while (std::getline(std::cin, line)) {
        if (line.empty() || line.starts_with('#'))
            continue;
        trace.push_back(parseMotion(line));
    }

    if (trace.empty())
        throw std::runtime_error("no input reports arrived on stdin");
    return trace;
}

void configure(int speed, bool enhanced) {
    std::array mouse{
        enhanced ? 6 : 0,
        enhanced ? 10 : 0,
        enhanced ? 1 : 0,
    };

    systemParameters(
        SPI_SETMOUSESPEED,
        reinterpret_cast<void*>(static_cast<std::intptr_t>(speed)),
        SPIF_SENDCHANGE);
    systemParameters(SPI_SETMOUSE, mouse.data(), SPIF_SENDCHANGE);
}

void centerCursor() {
    const auto x = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) / 2;
    const auto y = GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) / 2;
    if (!SetCursorPos(x, y))
        fail("SetCursorPos");
}

auto send(Motion raw) -> Motion {
    POINT before{};
    POINT after{};
    if (!GetCursorPos(&before))
        fail("GetCursorPos");

    INPUT input{};
    input.type       = INPUT_MOUSE;
    input.mi.dx      = raw.x;
    input.mi.dy      = raw.y;
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_MOVE_NOCOALESCE;

    if (SendInput(1, &input, sizeof(input)) != 1)
        fail("SendInput");

    // SendInput serializes the report, but cursor publication happens on the
    // desktop input thread. Five milliseconds is boring and intentional.
    Sleep(5);
    if (!GetCursorPos(&after))
        fail("GetCursorPos");

    return {
        .x = after.x - before.x,
        .y = after.y - before.y,
    };
}

} // namespace

int main(int argc, char** argv) {
    try {
        int  speed    = 10;
        bool enhanced = true;

        for (int index = 1; index < argc; ++index) {
            const std::string_view argument = argv[index];
            if (argument == "--speed" && index + 1 < argc)
                speed = parseNumber(argv[++index], "speed", 1, 20);
            else if (argument == "--epp" && index + 1 < argc) {
                const std::string_view value = argv[++index];
                if (value == "on")
                    enhanced = true;
                else if (value == "off")
                    enhanced = false;
                else
                    throw std::runtime_error("--epp must be on or off");
            } else
                throw std::runtime_error("usage: windows-oracle [--speed 1..20] [--epp on|off] < trace.csv");
        }

        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        DesktopSettings restoreWhenDone;
        const auto      trace = readTrace();

        configure(speed, enhanced);
        centerCursor();

        std::cout << "# windows-pointer-linux oracle v1\n"
                  << "# pointer_speed=" << speed << "/20\n"
                  << "# enhance_pointer_precision=" << (enhanced ? "true" : "false") << '\n'
                  << "# system_dpi=" << GetDpiForSystem() << '\n'
                  << "report,raw_x,raw_y,output_x,output_y\n";

        for (std::size_t index = 0; index < trace.size(); ++index) {
            const auto output = send(trace[index]);
            std::cout << index << ',' << trace[index].x << ',' << trace[index].y << ',' << output.x << ',' << output.y << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "windows-oracle: " << error.what() << '\n';
        return 1;
    }
}
