#include "pch.h"

#include <reframework/API.hpp>

#include "camera/game_state_detector.h"
#include "camera/gui_compensation.h"
#include "core/config.h"

#include <cameraunlock/reframework/gameplay_gate.h>
#include <cameraunlock/reframework/gui_elements.h>
#include <cameraunlock/reframework/plugin_bootstrap.h>

namespace ref = cameraunlock::reframework;

namespace {

// Fast-path candidates; app.PlayerCamera.lateUpdate is the confirmed hook point
// on RE7. The hooker's parent-chain walk discovers the real controller
// dynamically and logs the component tree if none of these match.
const char* const kControllerTypeCandidates[] = {
    "app.PlayerCamera",
    "app.camera.PlayerCamera",
    "app.PlayerCameraController",
    "app.camera.PlayerCameraController",
    "app.CameraManager",
    "app.camera.CameraManager",
};

// RE7 compensates world-anchored markers only - it has no reticle to place - so
// the pipeline computes the rotation-only tangents and skips the aim
// projection entirely.
const ref::PluginBootstrapDescriptor kPlugin = [] {
    ref::PluginBootstrapDescriptor d;
    d.logTag = "RE7HT";
    d.mod.displayName = RE7HT::RE7HT_PLUGIN_NAME;
    d.mod.version = RE7HT::RE7HT_VERSION;
    d.mod.config = RE7HT::kConfigSchema;
    d.camera.controllerCandidateTypes = kControllerTypeCandidates;
    d.camera.controllerCandidateCount =
        static_cast<int>(std::size(kControllerTypeCandidates));
    d.camera.gate = RE7HT::GameplayGateInstance();
    d.centerGameWindow = true;
    d.camera.onInit = []() { ref::InitGuiMethods(); };
    d.preGuiDrawElement = &RE7HT::OnPreGuiDrawElement;
    return d;
}();

} // namespace

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
    return ref::InitializePlugin(param, kPlugin);
}
