#include <windows_pointer/engine.hpp>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/devices/IPointer.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using windows_pointer::Engine;
using windows_pointer::Motion;
using windows_pointer::Settings;

using OriginalOnMouseMoved = void (*)(CInputManager*, IPointer::SMotionEvent);

HANDLE                           g_pluginHandle = nullptr;
CFunctionHook*                   g_motionHook    = nullptr;
SP<SHyprCtlCommand>              g_hyprctlCommand;
SP<Config::Values::CStringValue> g_pointerSpeed;
SP<Config::Values::CBoolValue>   g_enhancePointerPrecision;
CHyprSignalListener              g_configReloaded;
Settings                         g_settings;
Engine                           g_engine;

struct DeviceStats {
    std::uint64_t processed = 0;
    Motion        lastRaw;
    Motion        lastOutput;
    std::uint16_t lastDpi = 96;
};

struct RuntimeStats {
    std::uint64_t processed          = 0;
    std::uint64_t touchpads          = 0;
    std::uint64_t virtualPointers    = 0;
    std::uint64_t otherPointers      = 0;
    std::uint64_t invalidCoordinates = 0;
    std::uint64_t rotatedCoordinates = 0;
    Motion        lastRaw;
    Motion        lastOutput;
    std::uint16_t lastDpi     = 96;
    double        lastScale   = 1.0;
    std::string   lastMonitor = "(none yet)";
    std::string   lastDevice  = "(none yet)";
    std::map<std::string, DeviceStats> devices;
};

RuntimeStats g_stats;

[[noreturn]] void fail(const std::string& message) {
    HyprlandAPI::addNotification(
        g_pluginHandle,
        "[windows-pointer-linux] " + message,
        CHyprColor{1.0F, 0.2F, 0.2F, 1.0F},
        5000.0F);
    throw std::runtime_error(message);
}

void readSettings() {
    const auto parsedSpeed = windows_pointer::parsePointerSpeed(g_pointerSpeed->value());
    if (!parsedSpeed) {
        HyprlandAPI::addNotification(
            g_pluginHandle,
            "[windows-pointer-linux] ignoring pointer-speed: " + parsedSpeed.error(),
            CHyprColor{1.0F, 0.7F, 0.2F, 1.0F},
            5000.0F);
        return;
    }

    const Settings next{
        .pointerSpeed            = *parsedSpeed,
        .enhancePointerPrecision = g_enhancePointerPrecision->value(),
    };

    if (next == g_settings)
        return;

    g_settings = next;
    g_engine.configure(g_settings);
}

[[nodiscard]] auto rawCoordinate(double value) -> std::optional<std::int32_t> {
    if (!std::isfinite(value))
        return std::nullopt;

    const auto lower = static_cast<double>(std::numeric_limits<std::int32_t>::min());
    const auto upper = static_cast<double>(std::numeric_limits<std::int32_t>::max());
    if (value < lower || value > upper)
        return std::nullopt;

    const auto rounded = std::round(value);
    if (std::abs(value - rounded) > 1e-9)
        return std::nullopt;

    return static_cast<std::int32_t>(rounded);
}

struct Display {
    std::uint16_t dpi   = 96;
    double        scale = 1.0;
    std::string   name  = "(no monitor)";
};

[[nodiscard]] auto currentDisplay() -> Display {
    const auto monitor = g_pCompositor->getMonitorFromCursor();
    if (!monitor)
        return {};

    return {
        .dpi   = windows_pointer::displayDpiFromScale(monitor->m_scale),
        .scale = monitor->m_scale,
        .name  = monitor->m_name,
    };
}

void onMouseMoved(CInputManager* inputManager, IPointer::SMotionEvent event) {
    if (!event.device || !event.mouse) {
        ++g_stats.otherPointers;
    } else if (event.device->m_isTouchpad) {
        ++g_stats.touchpads;
    } else if (event.device->isVirtual()) {
        ++g_stats.virtualPointers;
    } else {
        const auto rawX = rawCoordinate(event.unaccel.x);
        const auto rawY = rawCoordinate(event.unaccel.y);

        if (!rawX || !rawY) {
            if (std::isfinite(event.unaccel.x) && std::isfinite(event.unaccel.y))
                ++g_stats.rotatedCoordinates;
            else
                ++g_stats.invalidCoordinates;
        } else {
            const auto display = currentDisplay();
            const Motion raw{.x = *rawX, .y = *rawY};
            const auto   moved = g_engine.apply(raw, display.dpi);

            event.delta = Vector2D{static_cast<double>(moved.x), static_cast<double>(moved.y)};

            ++g_stats.processed;
            g_stats.lastRaw     = raw;
            g_stats.lastOutput  = moved;
            g_stats.lastDpi     = display.dpi;
            g_stats.lastScale   = display.scale;
            g_stats.lastMonitor = display.name;
            g_stats.lastDevice  = event.device->m_deviceName;

            auto& deviceStats      = g_stats.devices[g_stats.lastDevice];
            ++deviceStats.processed;
            deviceStats.lastRaw    = raw;
            deviceStats.lastOutput = moved;
            deviceStats.lastDpi    = display.dpi;
        }
    }

    reinterpret_cast<OriginalOnMouseMoved>(g_motionHook->m_original)(inputManager, event);
}

[[nodiscard]] auto jsonEscape(std::string_view value) -> std::string {
    std::string escaped;
    escaped.reserve(value.size());

    for (const char character : value) {
        switch (character) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped += character;
        }
    }

    return escaped;
}

[[nodiscard]] auto status(eHyprCtlOutputFormat format) -> std::string {
    const auto epp = g_settings.enhancePointerPrecision ? "true" : "false";
    std::ostringstream output;

    if (format == FORMAT_JSON) {
        output << '{'
               << "\"version\":\"" << WINDOWS_POINTER_VERSION << "\","
               << "\"pointer_speed\":\"" << static_cast<int>(g_settings.pointerSpeed) << "/20\","
               << "\"enhance_pointer_precision\":" << epp << ','
               << "\"processed\":" << g_stats.processed << ','
               << "\"passthrough\":{"
               << "\"touchpad\":" << g_stats.touchpads << ','
               << "\"virtual\":" << g_stats.virtualPointers << ','
               << "\"other\":" << g_stats.otherPointers << ','
               << "\"invalid\":" << g_stats.invalidCoordinates << ','
               << "\"rotated\":" << g_stats.rotatedCoordinates << "},"
               << "\"last\":{"
               << "\"device\":\"" << jsonEscape(g_stats.lastDevice) << "\","
               << "\"monitor\":\"" << jsonEscape(g_stats.lastMonitor) << "\","
               << "\"scale\":" << g_stats.lastScale << ','
               << "\"dpi\":" << g_stats.lastDpi << ','
               << "\"raw\":[" << g_stats.lastRaw.x << ',' << g_stats.lastRaw.y << "],"
               << "\"output\":[" << g_stats.lastOutput.x << ',' << g_stats.lastOutput.y << ']'
               << "},\"devices\":[";

        bool first = true;
        for (const auto& [name, device] : g_stats.devices) {
            if (!first)
                output << ',';
            first = false;
            output << '{'
                   << "\"name\":\"" << jsonEscape(name) << "\","
                   << "\"processed\":" << device.processed << ','
                   << "\"dpi\":" << device.lastDpi << ','
                   << "\"raw\":[" << device.lastRaw.x << ',' << device.lastRaw.y << "],"
                   << "\"output\":[" << device.lastOutput.x << ',' << device.lastOutput.y << ']'
                   << '}';
        }
        output << "]}\n";
    } else {
        output << "windows-pointer-linux " << WINDOWS_POINTER_VERSION << '\n'
               << "settings: " << static_cast<int>(g_settings.pointerSpeed) << "/20, epp " << (g_settings.enhancePointerPrecision ? "on" : "off") << '\n'
               << "processed: " << g_stats.processed << '\n'
               << "passed through: " << g_stats.touchpads << " touchpad, " << g_stats.virtualPointers << " virtual, " << g_stats.otherPointers << " other, "
               << g_stats.invalidCoordinates << " invalid, " << g_stats.rotatedCoordinates << " rotated\n"
               << "last: " << g_stats.lastDevice << " on " << g_stats.lastMonitor << " at " << g_stats.lastScale << "x (" << g_stats.lastDpi << " dpi), raw "
               << g_stats.lastRaw.x << ',' << g_stats.lastRaw.y << " -> " << g_stats.lastOutput.x << ',' << g_stats.lastOutput.y << '\n';
    }

    return output.str();
}

[[nodiscard]] auto hyprctl(eHyprCtlOutputFormat format, std::string request) -> std::string {
    constexpr std::string_view name = "windows-pointer-linux";
    auto                       argument = std::string_view{request}.substr(name.size());
    while (!argument.empty() && argument.front() == ' ')
        argument.remove_prefix(1);

    if (argument.empty() || argument == "status")
        return status(format);

    if (argument == "reset") {
        g_engine.reset();
        g_stats = {};
        return format == FORMAT_JSON ? "{\"ok\":true}\n" : "ok\n";
    }

    return format == FORMAT_JSON ? "{\"error\":\"usage: windows-pointer-linux [status|reset]\"}\n" : "usage: windows-pointer-linux [status|reset]\n";
}

void registerConfig() {
    Config::Values::SStringValueOptions speedOptions{
        .validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            const auto parsed = windows_pointer::parsePointerSpeed(value);
            if (!parsed)
                return std::unexpected(parsed.error());
            return {};
        },
    };

    g_pointerSpeed = makeShared<Config::Values::CStringValue>(
        "plugin:windows-pointer-linux:pointer-speed",
        "Windows 11 pointer speed, written exactly like the Settings app.",
        "10/20",
        std::move(speedOptions));
    g_enhancePointerPrecision = makeShared<Config::Values::CBoolValue>(
        "plugin:windows-pointer-linux:enhance-pointer-precision",
        "Use Windows Enhance pointer precision.",
        true);

    if (!HyprlandAPI::addConfigValueV2(g_pluginHandle, g_pointerSpeed) ||
        !HyprlandAPI::addConfigValueV2(g_pluginHandle, g_enhancePointerPrecision))
        fail("could not register config values");

    readSettings();
    g_configReloaded = Event::bus()->m_events.config.reloaded.listen(readSettings);
}

void registerHyprctl() {
    g_hyprctlCommand = HyprlandAPI::registerHyprCtlCommand(
        g_pluginHandle,
        SHyprCtlCommand{
            .name  = "windows-pointer-linux",
            .exact = false,
            .fn    = hyprctl,
        });

    if (!g_hyprctlCommand)
        fail("could not register the hyprctl status command");
}

int luaLoaded(lua_State*) {
    return 0;
}

void registerLuaMarker() {
    if (Config::mgr()->type() == Config::CONFIG_LUA &&
        !HyprlandAPI::addLuaFunction(g_pluginHandle, "windows_pointer_linux", "loaded", luaLoaded))
        fail("could not register the Lua plugin namespace");
}

void installMotionHook() {
    const auto functions = HyprlandAPI::findFunctionsByName(g_pluginHandle, "onMouseMoved");
    const auto function  = std::ranges::find_if(functions, [](const SFunctionMatch& candidate) {
        return candidate.demangled.contains("CInputManager::onMouseMoved(IPointer::SMotionEvent)");
    });

    if (function == functions.end())
        fail("could not find CInputManager::onMouseMoved");

    g_motionHook = HyprlandAPI::createFunctionHook(g_pluginHandle, function->address, reinterpret_cast<void*>(onMouseMoved));
    if (!g_motionHook || !g_motionHook->hook())
        fail("could not install the mouse motion hook");
}

} // namespace

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    g_pluginHandle = handle;

    if (std::string{__hyprland_api_get_hash()} != __hyprland_api_get_client_hash())
        fail("Hyprland changed underneath the plugin; rebuild it");

    registerConfig();
    registerHyprctl();
    registerLuaMarker();
    installMotionHook();

    return {
        "windows-pointer-linux",
        "Windows 11 pointer motion for physical mice",
        "windows-pointer-linux contributors",
        WINDOWS_POINTER_VERSION,
    };
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_configReloaded.reset();

    if (g_hyprctlCommand) {
        HyprlandAPI::unregisterHyprCtlCommand(g_pluginHandle, g_hyprctlCommand);
        g_hyprctlCommand.reset();
    }

    if (g_motionHook) {
        HyprlandAPI::removeFunctionHook(g_pluginHandle, g_motionHook);
        g_motionHook = nullptr;
    }

    g_pointerSpeed.reset();
    g_enhancePointerPrecision.reset();
    g_pluginHandle = nullptr;
}
