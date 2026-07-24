#include <windows_pointer/engine.hpp>

#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/devices/IPointer.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {

using windows_pointer::Engine;
using windows_pointer::Motion;
using windows_pointer::Settings;

using OriginalOnMouseMoved = void (*)(CInputManager*, IPointer::SMotionEvent);

struct DeviceState {
    DeviceState(const SP<IPointer>& pointer, Settings settings) : device(pointer), engine(settings) {}

    WP<IPointer> device;
    Engine       engine;
};

HANDLE                                      g_pluginHandle = nullptr;
CFunctionHook*                              g_motionHook    = nullptr;
SP<Config::Values::CStringValue>            g_pointerSpeed;
SP<Config::Values::CBoolValue>              g_enhancePointerPrecision;
CHyprSignalListener                         g_configReloaded;
Settings                                    g_settings;
std::unordered_map<IPointer*, DeviceState> g_devices;

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
    std::erase_if(g_devices, [](const auto& item) { return item.second.device.expired(); });

    for (auto& [_, device] : g_devices)
        device.engine.configure(g_settings);
}

[[nodiscard]] auto rawCoordinate(double value) -> std::int32_t {
    const auto lower = static_cast<double>(std::numeric_limits<std::int32_t>::min());
    const auto upper = static_cast<double>(std::numeric_limits<std::int32_t>::max());
    return static_cast<std::int32_t>(std::clamp(value, lower, upper));
}

[[nodiscard]] auto stateFor(const SP<IPointer>& pointer) -> DeviceState& {
    if (const auto existing = g_devices.find(pointer.get()); existing != g_devices.end())
        return existing->second;

    std::erase_if(g_devices, [](const auto& item) { return item.second.device.expired(); });
    return g_devices.try_emplace(pointer.get(), pointer, g_settings).first->second;
}

void onMouseMoved(CInputManager* inputManager, IPointer::SMotionEvent event) {
    const bool canProcess = event.mouse && event.device && !event.device->m_isTouchpad && !event.device->isVirtual() && std::isfinite(event.unaccel.x) &&
        std::isfinite(event.unaccel.y);

    if (canProcess) {
        const auto moved = stateFor(event.device).engine.apply({
            .x = rawCoordinate(event.unaccel.x),
            .y = rawCoordinate(event.unaccel.y),
        });
        event.delta = Vector2D{static_cast<double>(moved.x), static_cast<double>(moved.y)};
    }

    reinterpret_cast<OriginalOnMouseMoved>(g_motionHook->m_original)(inputManager, event);
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
    installMotionHook();

    return {
        "windows-pointer-linux",
        "Windows 11 pointer motion for physical mice",
        "windows-pointer-linux contributors",
        "0.1.0",
    };
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_configReloaded.reset();

    if (g_motionHook) {
        HyprlandAPI::removeFunctionHook(g_pluginHandle, g_motionHook);
        g_motionHook = nullptr;
    }

    g_devices.clear();
    g_pointerSpeed.reset();
    g_enhancePointerPrecision.reset();
    g_pluginHandle = nullptr;
}
