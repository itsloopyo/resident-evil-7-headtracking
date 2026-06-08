#pragma once

#include "config.h"
#include <atomic>
#include <string>
#include <cameraunlock/input/deferred_actions.h>
#include <cameraunlock/protocol/udp_receiver.h>
#include <cameraunlock/tracking/head_tracking_session.h>

namespace RE7HT {

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

    // Hotkey callbacks fire on the HotkeyPoller's background thread, but
    // Recenter and CycleTrackingMode mutate the session's non-atomic
    // processor/interpolator smoothing state owned by the render thread. The
    // hotkey thread only requests the action; ProcessDeferredActions() runs it
    // on the render thread at the start of each frame.
    void RequestRecenter() { m_recenterRequested.Request(); }
    void RequestCycleTrackingMode() { m_cycleModeRequested.Request(); }
    void ProcessDeferredActions();

    Config& GetConfig() { return m_config; }
    const Config& GetConfig() const { return m_config; }

    // Advance interpolation + smoothing pipelines once per render frame.
    // Caches the smoothed rotation and position so every in-frame consumer
    // reads an identical value rather than re-ticking the pipeline with a
    // fragmented dt.
    void TickFrame();

    // Wall-clock seconds of the last TickFrame step. GUI marker compensation
    // smooths its projection at the same dt the tracking pipeline used.
    float GetLastDeltaTime() const { return m_lastDeltaTime; }

    bool GetProcessedRotation(float& yaw, float& pitch, float& roll);
    bool GetPositionOffset(float& x, float& y, float& z);

    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw.load(); }

    Mod(const Mod&) = delete;
    Mod& operator=(const Mod&) = delete;

private:
    Mod() = default;
    ~Mod() = default;

    bool LoadConfig();

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_initialized{false};

    Config m_config;
    cameraunlock::UdpReceiver m_udpReceiver;
    cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver> m_session{m_udpReceiver};

    std::atomic<bool> m_worldSpaceYaw{false};

    cameraunlock::input::DeferredAction m_recenterRequested;
    cameraunlock::input::DeferredAction m_cycleModeRequested;

    uint64_t m_lastFrameTickTime = 0;
    float m_lastDeltaTime = 0.016f;

    std::string m_pluginDir;
};

} // namespace RE7HT
