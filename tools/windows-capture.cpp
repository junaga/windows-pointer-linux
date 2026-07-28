#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct RawSample {
    std::uint64_t sequence;
    std::int64_t  timeNs;
    std::uintptr_t device;
    std::uint16_t flags;
    std::int32_t  x;
    std::int32_t  y;
    POINT         cursor;
};

struct PointerSample {
    std::uint64_t sequence;
    std::int64_t  timeNs;
    std::uint32_t message;
    POINT         cursor;
    std::uint32_t flags;
};

std::vector<RawSample>     g_raw;
std::vector<PointerSample> g_pointer;
LARGE_INTEGER              g_frequency;
LARGE_INTEGER              g_started;
std::filesystem::path      g_output;
std::filesystem::path      g_ready;
HHOOK                      g_hook = nullptr;
POINT                      g_initialCursor{};
int                        g_screenWidth  = 0;
int                        g_screenHeight = 0;
bool                       g_cursorApiAvailable = false;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message + " (Windows error " + std::to_string(GetLastError()) + ")");
}

auto nowNs() -> std::int64_t {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const auto ticks = now.QuadPart - g_started.QuadPart;
    const auto seconds   = ticks / g_frequency.QuadPart;
    const auto remainder = ticks % g_frequency.QuadPart;
    return static_cast<std::int64_t>(
        seconds * 1'000'000'000LL + (remainder * 1'000'000'000LL) / g_frequency.QuadPart);
}

LRESULT CALLBACK lowLevelMouse(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION) {
        const auto* event = reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
        g_pointer.push_back({
            .sequence = g_pointer.size(),
            .timeNs   = nowNs(),
            .message  = static_cast<std::uint32_t>(message),
            .cursor   = event->pt,
            .flags    = event->flags,
        });
    }
    return CallNextHookEx(g_hook, code, message, data);
}

void recordRaw(LPARAM data) {
    UINT size = 0;
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(data), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0)
        fail("GetRawInputData size");

    std::vector<std::byte> storage(size);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(data), RID_INPUT, storage.data(), &size, sizeof(RAWINPUTHEADER)) != size)
        fail("GetRawInputData payload");

    const auto* input = reinterpret_cast<const RAWINPUT*>(storage.data());
    if (input->header.dwType != RIM_TYPEMOUSE)
        return;

    POINT cursor{
        .x = std::numeric_limits<LONG>::min(),
        .y = std::numeric_limits<LONG>::min(),
    };
    GetCursorPos(&cursor);

    g_raw.push_back({
        .sequence = g_raw.size(),
        .timeNs   = nowNs(),
        .device   = reinterpret_cast<std::uintptr_t>(input->header.hDevice),
        .flags    = input->data.mouse.usFlags,
        .x        = input->data.mouse.lLastX,
        .y        = input->data.mouse.lLastY,
        .cursor   = cursor,
    });
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_INPUT:
            recordRaw(lparam);
            return DefWindowProcW(window, message, wparam, lparam);
        case WM_TIMER:
            PostQuitMessage(0);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wparam, lparam);
    }
}

auto parseSeconds(std::string_view value) -> UINT {
    std::size_t consumed = 0;
    const auto seconds = std::stoul(std::string{value}, &consumed);
    if (consumed != value.size() || seconds < 1 || seconds > 3600)
        throw std::runtime_error("--seconds must be between 1 and 3600");
    return static_cast<UINT>(seconds);
}

void writeResults() {
    std::filesystem::create_directories(g_output);

    int speed = 0;
    int mouse[3]{};
    SystemParametersInfoW(SPI_GETMOUSESPEED, 0, &speed, 0);
    SystemParametersInfoW(SPI_GETMOUSE, 0, mouse, 0);

    {
        std::ofstream metadata(g_output / "metadata.txt");
        metadata << "pointer_speed=" << speed << '\n'
                 << "mouse_threshold_1=" << mouse[0] << '\n'
                 << "mouse_threshold_2=" << mouse[1] << '\n'
                 << "mouse_acceleration=" << mouse[2] << '\n'
                 << "screen_width=" << g_screenWidth << '\n'
                 << "screen_height=" << g_screenHeight << '\n'
                 << "system_dpi=" << GetDpiForSystem() << '\n'
                 << "session_id=" << WTSGetActiveConsoleSessionId() << '\n'
                 << "cursor_api_available=" << g_cursorApiAvailable << '\n'
                 << "initial_cursor_x=" << g_initialCursor.x << '\n'
                 << "initial_cursor_y=" << g_initialCursor.y << '\n'
                 << "raw_reports=" << g_raw.size() << '\n'
                 << "pointer_events=" << g_pointer.size() << '\n'
                 << "qpc_frequency=" << g_frequency.QuadPart << '\n';
    }

    {
        std::ofstream output(g_output / "raw.csv");
        output << "report,time_ns,device,flags,raw_x,raw_y,cursor_x,cursor_y\n";
        for (const auto& sample : g_raw)
            output << sample.sequence << ',' << sample.timeNs << ',' << sample.device << ',' << sample.flags << ',' << sample.x << ',' << sample.y << ','
                   << sample.cursor.x << ',' << sample.cursor.y << '\n';
    }

    {
        std::ofstream output(g_output / "pointer.csv");
        output << "event,time_ns,message,cursor_x,cursor_y,flags\n";
        for (const auto& sample : g_pointer)
            output << sample.sequence << ',' << sample.timeNs << ',' << sample.message << ',' << sample.cursor.x << ',' << sample.cursor.y << ','
                   << sample.flags << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        UINT seconds = 30;
        g_output = "pointer-lab";
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument = argv[index];
            if (argument == "--output" && index + 1 < argc)
                g_output = argv[++index];
            else if (argument == "--ready-file" && index + 1 < argc)
                g_ready = argv[++index];
            else if (argument == "--seconds" && index + 1 < argc)
                seconds = parseSeconds(argv[++index]);
            else
                throw std::runtime_error(
                    "usage: windows-pointer-windows-capture "
                    "[--output directory] [--ready-file path] "
                    "[--seconds 1..3600]");
        }

        if (!QueryPerformanceFrequency(&g_frequency) || !QueryPerformanceCounter(&g_started))
            fail("QueryPerformanceCounter");
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        g_raw.reserve(static_cast<std::size_t>(seconds) * 1200);
        g_pointer.reserve(static_cast<std::size_t>(seconds) * 1200);

        const wchar_t* className = L"WindowsPointerCapture";
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc   = windowProcedure;
        windowClass.hInstance     = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = className;
        if (!RegisterClassW(&windowClass))
            fail("RegisterClass");

        const auto window = CreateWindowExW(
            0,
            className,
            L"Windows pointer capture",
            0,
            0,
            0,
            0,
            0,
            HWND_MESSAGE,
            nullptr,
            windowClass.hInstance,
            nullptr);
        if (!window)
            fail("CreateWindowEx");

        RAWINPUTDEVICE mouse{
            .usUsagePage = 0x01,
            .usUsage     = 0x02,
            .dwFlags     = RIDEV_INPUTSINK,
            .hwndTarget  = window,
        };
        if (!RegisterRawInputDevices(&mouse, 1, sizeof(mouse)))
            fail("RegisterRawInputDevices");

        g_screenWidth  = GetSystemMetrics(SM_CXSCREEN);
        g_screenHeight = GetSystemMetrics(SM_CYSCREEN);
        g_initialCursor = {
            .x = g_screenWidth / 2,
            .y = g_screenHeight / 2,
        };
        if (SetCursorPos(g_initialCursor.x, g_initialCursor.y)) {
            POINT actual{};
            if (GetCursorPos(&actual)) {
                g_initialCursor      = actual;
                g_cursorApiAvailable = true;
            }
        }

        g_hook = SetWindowsHookExW(WH_MOUSE_LL, lowLevelMouse, GetModuleHandleW(nullptr), 0);
        if (!g_hook)
            fail("SetWindowsHookEx");

        if (!SetTimer(window, 1, seconds * 1000, nullptr))
            fail("SetTimer");

        if (!g_ready.empty()) {
            if (g_ready.has_parent_path())
                std::filesystem::create_directories(g_ready.parent_path());
            std::ofstream ready(g_ready);
            ready << "armed\n";
            if (!ready)
                throw std::runtime_error("could not write ready file");
        }

        std::cout << "capture armed for " << seconds << " seconds\n";
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
        DestroyWindow(window);
        writeResults();
        std::cout << "captured " << g_raw.size() << " raw reports and " << g_pointer.size() << " pointer events in " << g_output.string() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "windows-pointer-windows-capture: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
