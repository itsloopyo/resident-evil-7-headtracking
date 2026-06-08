#include "pch.h"
#include "mod.h"
#include "logger.h"

#include <algorithm>
#include <cameraunlock/time/qpc_clock.h>

namespace RE7HT {

using cameraunlock::TrackingMode;

// Skip noisy initial frames before auto-recentering (~0.5s at 60fps)
constexpr int STABILIZATION_FRAME_COUNT = 30;

constexpr float kMicrosPerSecond = 1000000.0f;

// Frame-timing bounds for the per-frame interpolation/smoothing tick.
constexpr float kSeedFrameDeltaSeconds = 0.016f;  // ~60fps seed until a real interval is measured
constexpr float kMinFrameDeltaSeconds = 0.0001f;  // floor: avoid div-by-zero / runaway extrapolation
constexpr float kMaxFrameDeltaSeconds = 0.1f;     // ceiling: a hitch must not snap the view

Mod& Mod::Instance() {
    static Mod instance;
    return instance;
}

bool Mod::Initialize() {
    if (m_initialized.load()) {
        Logger::Instance().Warning("Mod already initialized");
        return true;
    }

    Logger::Instance().Info("%s v%s initializing...", RE7HT_PLUGIN_NAME, RE7HT_VERSION);

    // Determine plugin directory (used for config)
    HMODULE hModule = nullptr;
    char dllPath[MAX_PATH] = {};
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&Mod::Instance, &hModule)) {
        GetModuleFileNameA(hModule, dllPath, MAX_PATH);
    }
    m_pluginDir.assign(dllPath);
    auto lastSlash = m_pluginDir.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        m_pluginDir = m_pluginDir.substr(0, lastSlash + 1);
    }

    if (!LoadConfig()) {
        Logger::Instance().Warning("Using default configuration");
    }

    cameraunlock::SensitivitySettings sensitivity;
    sensitivity.yaw = m_config.yawMultiplier;
    sensitivity.pitch = m_config.pitchMultiplier;
    sensitivity.roll = m_config.rollMultiplier;
    m_session.GetProcessor().SetSensitivity(sensitivity);

    Logger::Instance().Info("Sensitivity: yaw=%.2f pitch=%.2f roll=%.2f",
                            sensitivity.yaw, sensitivity.pitch, sensitivity.roll);

    m_session.SetMode(m_config.positionEnabled ? TrackingMode::RotationAndPosition
                                               : TrackingMode::RotationOnly);
    m_session.SetStabilizationFrames(STABILIZATION_FRAME_COUNT);
    m_worldSpaceYaw.store(m_config.worldSpaceYaw);

    cameraunlock::PositionSettings posSettings(
        m_config.positionSensitivityX, m_config.positionSensitivityY, m_config.positionSensitivityZ,
        m_config.positionLimitX, m_config.positionLimitY, m_config.positionLimitZ, m_config.positionLimitZBack,
        m_config.positionSmoothing,
        m_config.positionInvertX, m_config.positionInvertY, m_config.positionInvertZ
    );
    m_session.GetPositionProcessor().SetSettings(posSettings);
    // The previous per-mod pipeline never engaged tracker pivot compensation
    // (it passed radians to a degrees API, zeroing the artifact). Keep that
    // tuning until pivot compensation is verified in game.
    m_session.GetPositionProcessor().SetTrackerPivotForward(0.0f);

    Logger::Instance().Info("Position: %s, sens=%.1f/%.1f/%.1f",
                            m_session.IsPositionActive() ? "6DOF" : "3DOF",
                            posSettings.sensitivity_x, posSettings.sensitivity_y, posSettings.sensitivity_z);

    // Start UDP receiver. Wire logging before Start so bind/retry messages surface.
    m_udpReceiver.SetLog([](const std::string& msg) {
        Logger::Instance().Info("%s", msg.c_str());
    });

    // A busy port is non-fatal: the receiver schedules a background retry every
    // 5s and recovers on its own. The rest of the mod must keep running so
    // tracking resumes the moment the port frees -- do not tear down here.
    if (!m_udpReceiver.Start(m_config.udpPort)) {
        Logger::Instance().Warning("UDP port %d busy; retrying in background, tracking will start when it frees", m_config.udpPort);
    } else {
        Logger::Instance().Info("UDP receiver started on port %d", m_config.udpPort);
    }

    if (m_config.autoEnable) {
        m_enabled.store(true);
        Logger::Instance().Info("Head tracking auto-enabled");
    }

    m_initialized.store(true);
    Logger::Instance().Info("Initialization complete");
    return true;
}

void Mod::Shutdown() {
    if (!m_initialized.load()) return;

    Logger::Instance().Info("Shutting down...");
    m_udpReceiver.Stop();
    m_initialized.store(false);
    Logger::Instance().Info("Shutdown complete");
}

bool Mod::LoadConfig() {
    std::string configPath = m_pluginDir + "HeadTracking.ini";

    if (!m_config.Load(configPath.c_str())) {
        m_config.SetDefaults();
        m_config.Save(configPath.c_str());
        return false;
    }
    return true;
}

void Mod::SetEnabled(bool enabled) {
    bool wasEnabled = m_enabled.exchange(enabled);
    if (wasEnabled != enabled) {
        Logger::Instance().Info("Head tracking %s", enabled ? "enabled" : "disabled");
    }
}

void Mod::Toggle() {
    SetEnabled(!m_enabled.load());
}

void Mod::Recenter() {
    m_session.Recenter();
    m_lastFrameTickTime = 0;
    Logger::Instance().Info("View recentered");
}

void Mod::CycleTrackingMode() {
    switch (m_session.CycleMode()) {
        case TrackingMode::RotationAndPosition:
            Logger::Instance().Info("Tracking mode: full (rotation + position)");
            break;
        case TrackingMode::RotationOnly:
            Logger::Instance().Info("Tracking mode: rotation only (position disabled)");
            break;
        case TrackingMode::PositionOnly:
            Logger::Instance().Info("Tracking mode: position only (rotation disabled)");
            break;
    }
}

void Mod::ProcessDeferredActions() {
    if (!m_initialized.load()) return;
    if (m_recenterRequested.Consume()) Recenter();
    if (m_cycleModeRequested.Consume()) CycleTrackingMode();
}

void Mod::TickFrame() {
    if (!m_initialized.load()) return;

    uint64_t now = cameraunlock::time::QpcNowMicros();
    float deltaTime = kSeedFrameDeltaSeconds;
    if (m_lastFrameTickTime > 0) {
        deltaTime = (now - m_lastFrameTickTime) / kMicrosPerSecond;
        deltaTime = std::clamp(deltaTime, kMinFrameDeltaSeconds, kMaxFrameDeltaSeconds);
    }
    m_lastFrameTickTime = now;
    m_lastDeltaTime = deltaTime;

    m_session.Update(deltaTime);
}

bool Mod::GetProcessedRotation(float& yaw, float& pitch, float& roll) {
    return m_session.GetRotation(yaw, pitch, roll);
}

bool Mod::GetPositionOffset(float& x, float& y, float& z) {
    return m_session.GetPositionOffset(x, y, z);
}

void Mod::ToggleYawMode() {
    bool next = !m_worldSpaceYaw.load();
    m_worldSpaceYaw.store(next);
    Logger::Instance().Info("Yaw mode: %s", next ? "world-space (horizon-locked)" : "camera-local");
}

} // namespace RE7HT
