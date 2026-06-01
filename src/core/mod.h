#pragma once

#include "config.h"
#include <cameraunlock/protocol/udp_receiver.h>
#include <cameraunlock/processing/tracking_processor.h>
#include <cameraunlock/processing/pose_interpolator.h>
#include <cameraunlock/processing/position_processor.h>
#include <cameraunlock/processing/position_interpolator.h>
#include <cstdio>
#include <mutex>
#include <string>

namespace RE7HT {

enum class TrackingMode {
    Full = 0,          // both rotation and position
    RotationOnly = 1,  // position disabled
    PositionOnly = 2,  // rotation disabled
};

class Mod {
public:
    static Mod& Instance();

    bool Initialize();
    void Shutdown();

    bool IsEnabled() const { return m_enabled.load(); }
    void SetEnabled(bool enabled);
    void Toggle();

    void Recenter();
    void CycleTrackingMode();
    void ToggleYawMode();

    Config& GetConfig() { return m_config; }
    const Config& GetConfig() const { return m_config; }

    // Advance interpolation + smoothing pipelines once per render frame.
    // Caches the smoothed rotation and position so every in-frame consumer
    // reads an identical value rather than re-ticking the pipeline with a
    // fragmented dt.
    void TickFrame();

    bool GetProcessedRotation(float& yaw, float& pitch, float& roll);
    bool GetPositionOffset(float& x, float& y, float& z);
    bool IsPositionEnabled() const {
        return static_cast<TrackingMode>(m_trackingMode.load()) != TrackingMode::RotationOnly;
    }
    bool IsRotationEnabled() const {
        return static_cast<TrackingMode>(m_trackingMode.load()) != TrackingMode::PositionOnly;
    }
    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw.load(); }

    Mod(const Mod&) = delete;
    Mod& operator=(const Mod&) = delete;

private:
    Mod() = default;
    ~Mod() = default;

    bool LoadConfig();

    // Recenter the tracking pipeline assuming m_pipelineMutex is already held.
    void RecenterLocked();

    // Serializes mutation of the tracking pipeline (processors + interpolators +
    // frame-timing state) between the render thread (TickFrame, called from
    // OnPreBeginRendering) and the HotkeyPoller worker thread (Recenter /
    // CycleTrackingMode). Without it the two threads race on non-atomic
    // interpolator state, which can corrupt smoothing or crash the game.
    std::mutex m_pipelineMutex;

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_initialized{false};

    Config m_config;
    cameraunlock::UdpReceiver m_udpReceiver;
    cameraunlock::PoseInterpolator m_poseInterpolator;
    cameraunlock::TrackingProcessor m_processor;
    int64_t m_lastReceiveTimestamp = 0;

    cameraunlock::PositionProcessor m_positionProcessor;
    cameraunlock::PositionInterpolator m_positionInterpolator;
    std::atomic<int> m_trackingMode{static_cast<int>(TrackingMode::Full)};
    std::atomic<bool> m_worldSpaceYaw{false};

    uint64_t m_lastFrameTickTime = 0;

    float m_cachedYaw = 0.0f;
    float m_cachedPitch = 0.0f;
    float m_cachedRoll = 0.0f;
    bool m_cachedRotationValid = false;

    float m_cachedPositionX = 0.0f;
    float m_cachedPositionY = 0.0f;
    float m_cachedPositionZ = 0.0f;
    bool m_cachedPositionValid = false;

    bool m_hasCentered = false;
    int m_stabilizationFrames = 0;

    // Previous raw values for new-sample detection (data change, not just packet arrival)
    float m_lastRawYaw = 0.0f;
    float m_lastRawPitch = 0.0f;
    float m_lastRawRoll = 0.0f;

    std::string m_pluginDir;
};

} // namespace RE7HT
