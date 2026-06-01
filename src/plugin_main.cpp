#include "pch.h"

#include <reframework/API.hpp>

#include "core/mod.h"
#include "core/logger.h"
#include "core/window.h"
#include "camera/camera_hook.h"

#include <cameraunlock/input/hotkey_poller.h>
#include <cameraunlock/reframework/log_callback.h>

static cameraunlock::input::HotkeyPoller g_hotkeyPoller;

static bool IsChordHeld() {
    return ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0)
        && ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
}

// Nav-cluster keys fire only when the Ctrl+Shift chord is NOT held, so the
// chord bindings below are the sole trigger for Ctrl+Shift+<nav> combos.
template <typename F>
static cameraunlock::input::HotkeyCallback NavGuarded(F action) {
    return [action]() { if (!IsChordHeld()) action(); };
}

// Chord-cluster letters fire only while the Ctrl+Shift chord IS held.
template <typename F>
static cameraunlock::input::HotkeyCallback ChordGuarded(F action) {
    return [action]() { if (IsChordHeld()) action(); };
}

static void OnPreBeginRendering() {
    RE7HT::CenterGameWindowOnce();
    RE7HT::OnPreBeginRendering();
}

static void OnPostBeginRendering() {
    RE7HT::OnPostBeginRendering();
}

// --- REFramework plugin exports ---

extern "C" __declspec(dllexport)
void reframework_plugin_required_version(REFrameworkPluginVersion* version) {
    version->major = REFRAMEWORK_PLUGIN_VERSION_MAJOR;
    version->minor = REFRAMEWORK_PLUGIN_VERSION_MINOR;
    version->patch = REFRAMEWORK_PLUGIN_VERSION_PATCH;
    version->game_name = nullptr;
}

extern "C" __declspec(dllexport)
bool reframework_plugin_initialize(const REFrameworkPluginInitializeParam* param) {
    if (!param) return false;

    // Initialize REFramework SDK wrapper
    reframework::API::initialize(param);

    // Set up logging via REFramework's log functions
    RE7HT::Logger::Instance().SetREFunctions(
        param->functions->log_info,
        param->functions->log_warn,
        param->functions->log_error
    );

    // Bridge shared library logging to REFramework's log functions
    cameraunlock::reframework::SetLogCallback([](cameraunlock::reframework::LogLevel level, const char* msg) {
        switch (level) {
            case cameraunlock::reframework::LogLevel::Warning:
                RE7HT::Logger::Instance().Warning("%s", msg); break;
            case cameraunlock::reframework::LogLevel::Error:
                RE7HT::Logger::Instance().Error("%s", msg); break;
            default:
                RE7HT::Logger::Instance().Info("%s", msg); break;
        }
    });

    RE7HT::Logger::Instance().Info("%s v%s - Plugin loaded", RE7HT::RE7HT_PLUGIN_NAME, RE7HT::RE7HT_VERSION);

    // Initialize mod (tracking pipeline, UDP receiver)
    if (!RE7HT::Mod::Instance().Initialize()) {
        RE7HT::Logger::Instance().Error("Mod initialization failed");
        return false;
    }

    param->functions->on_pre_application_entry("BeginRendering", OnPreBeginRendering);
    param->functions->on_post_application_entry("BeginRendering", OnPostBeginRendering);

    // Set up hotkeys
    auto& config = RE7HT::Mod::Instance().GetConfig();

    // Nav-cluster bindings (End / Home / Page Up / Page Down).
    g_hotkeyPoller.SetToggleKey(config.toggleKey, NavGuarded([] { RE7HT::Mod::Instance().Toggle(); }));
    g_hotkeyPoller.SetRecenterKey(config.recenterKey, NavGuarded([] { RE7HT::Mod::Instance().Recenter(); }));
    g_hotkeyPoller.AddHotkey(config.positionToggleKey, NavGuarded([] { RE7HT::Mod::Instance().CycleTrackingMode(); }));
    g_hotkeyPoller.AddHotkey(config.yawModeKey, NavGuarded([] { RE7HT::Mod::Instance().ToggleYawMode(); }));

    // Ctrl+Shift+<letter> chord bindings (CLAUDE.md T/Y/U/G/H/J cluster).
    g_hotkeyPoller.AddHotkey('T', ChordGuarded([] { RE7HT::Mod::Instance().Recenter(); }));
    g_hotkeyPoller.AddHotkey('Y', ChordGuarded([] { RE7HT::Mod::Instance().Toggle(); }));
    g_hotkeyPoller.AddHotkey('G', ChordGuarded([] { RE7HT::Mod::Instance().CycleTrackingMode(); }));
    g_hotkeyPoller.AddHotkey('H', ChordGuarded([] { RE7HT::Mod::Instance().ToggleYawMode(); }));

    g_hotkeyPoller.Start();

    RE7HT::Logger::Instance().Info("Plugin initialization complete");
    return true;
}
