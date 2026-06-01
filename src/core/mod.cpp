#include "pch.h"
#include "mod.h"
#include "logger.h"
#include "camera/game_state_detector.h"

#include <algorithm>
#include <cameraunlock/math/smoothing_utils.h>

namespace RE7HT {

// Skip noisy initial frames before auto-recentering (~0.5s at 60fps)
constexpr int STABILIZATION_FRAME_COUNT = 30;

constexpr float kMicrosPerSecond = 1000000.0f;

// Frame-timing bounds for the per-frame interpolation/smoothing tick.
constexpr float kSeedFrameDeltaSeconds = 0.016f;  // ~60fps seed until a real interval is measured
constexpr float kMinFrameDeltaSeconds = 0.0001f;  // floor: avoid div-by-zero / runaway extrapolation
constexpr float kMaxFrameDeltaSeconds = 0.1f;     // ceiling: a hitch must not snap the view

static uint64_t GetTimeMicros() {
    static double microsPerTick = 0.0;
    if (microsPerTick == 0.0) {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        microsPerTick = kMicrosPerSecond / static_cast<double>(freq.QuadPart);
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<uint64_t>(static_cast<double>(now.QuadPart) * microsPerTick);
}

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

    // Initialize TrackingProcessor
    cameraunlock::SensitivitySettings sensitivity;
    sensitivity.yaw = m_config.yawMultiplier;
    sensitivity.pitch = m_config.pitchMultiplier;
    sensitivity.roll = m_config.rollMultiplier;
    m_processor.SetSensitivity(sensitivity);

    Logger::Instance().Info("Sensitivity: yaw=%.2f pitch=%.2f roll=%.2f",
                            sensitivity.yaw, sensitivity.pitch, sensitivity.roll);

    // Initialize position processor
    m_trackingMode.store(static_cast<int>(
        m_config.positionEnabled ? TrackingMode::Full : TrackingMode::RotationOnly));
    m_worldSpaceYaw.store(m_config.worldSpaceYaw);

    cameraunlock::PositionSettings posSettings(
        m_config.positionSensitivityX, m_config.positionSensitivityY, m_config.positionSensitivityZ,
        m_config.positionLimitX, m_config.positionLimitY, m_config.positionLimitZ, m_config.positionLimitZBack,
        m_config.positionSmoothing,
        m_config.positionInvertX, m_config.positionInvertY, m_config.positionInvertZ
    );
    m_positionProcessor.SetSettings(posSettings);

    Logger::Instance().Info("Position: %s, sens=%.1f/%.1f/%.1f",
                            IsPositionEnabled() ? "6DOF" : "3DOF",
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
    std::lock_guard<std::mutex> lock(m_pipelineMutex);
    RecenterLocked();
}

void Mod::RecenterLocked() {
    m_udpReceiver.Recenter();
    m_processor.Reset();
    m_poseInterpolator.Reset();
    m_lastFrameTickTime = 0;

    float px, py, pz;
    if (m_udpReceiver.GetPosition(px, py, pz)) {
        cameraunlock::PositionData posCenter(px, py, pz);
        m_positionProcessor.SetCenter(posCenter);
    }
    m_positionInterpolator.Reset();

    Logger::Instance().Info("View recentered");
}

void Mod::CycleTrackingMode() {
    std::lock_guard<std::mutex> lock(m_pipelineMutex);
    int next = (m_trackingMode.load() + 1) % 3;
    m_trackingMode.store(next);
    switch (static_cast<TrackingMode>(next)) {
        case TrackingMode::Full:
            Logger::Instance().Info("Tracking mode: full (rotation + position)");
            break;
        case TrackingMode::RotationOnly:
            m_positionProcessor.Reset();
            m_positionInterpolator.Reset();
            Logger::Instance().Info("Tracking mode: rotation only (position disabled)");
            break;
        case TrackingMode::PositionOnly:
            Logger::Instance().Info("Tracking mode: position only (rotation disabled)");
            break;
    }
}

void Mod::TickFrame() {
    if (!m_initialized.load()) return;

    std::lock_guard<std::mutex> lock(m_pipelineMutex);

    uint64_t now = GetTimeMicros();
    float deltaTime = kSeedFrameDeltaSeconds;
    if (m_lastFrameTickTime > 0) {
        deltaTime = (now - m_lastFrameTickTime) / kMicrosPerSecond;
        deltaTime = std::clamp(deltaTime, kMinFrameDeltaSeconds, kMaxFrameDeltaSeconds);
    }
    m_lastFrameTickTime = now;

    float rawYaw, rawPitch, rawRoll;
    if (!m_udpReceiver.GetRotation(rawYaw, rawPitch, rawRoll)) {
        m_cachedRotationValid = false;
        m_cachedPositionValid = false;
        return;
    }

    if (!m_hasCentered) {
        m_stabilizationFrames++;
        if (m_stabilizationFrames >= STABILIZATION_FRAME_COUNT) {
            m_hasCentered = true;
            RecenterLocked();
            Logger::Instance().Info("Auto-recentered after %d frames", m_stabilizationFrames);
        }
    }

    int64_t receiveTs = m_udpReceiver.GetLastReceiveTimestamp();
    bool isNewPacket = (receiveTs != m_lastReceiveTimestamp);
    m_lastReceiveTimestamp = receiveTs;

    // Detect new DATA, not just new packets. If a tracker sends at a higher rate
    // than its sensor updates (e.g. phone app at 60Hz with 30Hz IMU), duplicate
    // packets would fool the interpolator into thinking samples arrive at 60Hz,
    // preventing it from generating smooth inter-sample frames.
    bool isNewSample = isNewPacket &&
        (rawYaw != m_lastRawYaw || rawPitch != m_lastRawPitch || rawRoll != m_lastRawRoll);
    if (isNewPacket) {
        m_lastRawYaw = rawYaw;
        m_lastRawPitch = rawPitch;
        m_lastRawRoll = rawRoll;
    }

    cameraunlock::InterpolatedPose interpolated = m_poseInterpolator.Update(
        rawYaw, rawPitch, rawRoll, isNewSample, deltaTime);

    cameraunlock::TrackingPose processed = m_processor.Process(
        interpolated.yaw, interpolated.pitch, interpolated.roll, deltaTime);

    if (IsRotationEnabled()) {
        m_cachedYaw = processed.yaw;
        m_cachedPitch = processed.pitch;
        m_cachedRoll = processed.roll;
    } else {
        m_cachedYaw = m_cachedPitch = m_cachedRoll = 0.0f;
    }
    m_cachedRotationValid = true;

    if (IsPositionEnabled()) {
        float rawX, rawY, rawZ;
        if (m_udpReceiver.GetPosition(rawX, rawY, rawZ)) {
            cameraunlock::PositionData rawPos(rawX, rawY, rawZ, receiveTs);
            cameraunlock::PositionData interpolatedPos =
                m_positionInterpolator.Update(rawPos, deltaTime);

            cameraunlock::math::Quat4 headRotQ = cameraunlock::math::Quat4::FromYawPitchRoll(
                m_cachedYaw * static_cast<float>(cameraunlock::math::kDegToRad),
                m_cachedPitch * static_cast<float>(cameraunlock::math::kDegToRad),
                m_cachedRoll * static_cast<float>(cameraunlock::math::kDegToRad));

            cameraunlock::math::Vec3 offset =
                m_positionProcessor.Process(interpolatedPos, headRotQ, deltaTime);
            m_cachedPositionX = offset.x;
            m_cachedPositionY = offset.y;
            m_cachedPositionZ = offset.z;
            m_cachedPositionValid = true;
        } else {
            m_cachedPositionValid = false;
        }
    } else {
        m_cachedPositionX = m_cachedPositionY = m_cachedPositionZ = 0.0f;
        m_cachedPositionValid = false;
    }
}

bool Mod::GetProcessedRotation(float& yaw, float& pitch, float& roll) {
    if (!m_cachedRotationValid) {
        yaw = pitch = roll = 0.0f;
        return false;
    }
    yaw = m_cachedYaw;
    pitch = m_cachedPitch;
    roll = m_cachedRoll;
    return true;
}

bool Mod::GetPositionOffset(float& x, float& y, float& z) {
    if (!m_cachedPositionValid) {
        x = y = z = 0.0f;
        return false;
    }
    x = m_cachedPositionX;
    y = m_cachedPositionY;
    z = m_cachedPositionZ;
    return true;
}

void Mod::ToggleYawMode() {
    bool next = !m_worldSpaceYaw.load();
    m_worldSpaceYaw.store(next);
    Logger::Instance().Info("Yaw mode: %s", next ? "world-space (horizon-locked)" : "camera-local");
}

} // namespace RE7HT
