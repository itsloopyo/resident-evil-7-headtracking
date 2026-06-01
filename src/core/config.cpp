#include "pch.h"
#include "config.h"
#include "logger.h"

#include <cameraunlock/config/ini_reader.h>

#include <algorithm>
#include <cmath>

namespace RE7HT {

namespace {
// std::clamp does NOT sanitize NaN/Inf: clamp(NaN, lo, hi) returns NaN because
// both `NaN < lo` and `hi < NaN` are false. strtod (used by ReadFloat) accepts
// "nan"/"inf" and overflows large literals like 1e400 to +inf, so a malformed
// or corrupted HeadTracking.ini value would otherwise pass straight through
// Validate() and poison the sin/cos/view-matrix math. Replace any non-finite
// value with the documented default before clamping into range.
inline float Sanitize(float v, float fallback, float lo, float hi) {
    return std::clamp(std::isfinite(v) ? v : fallback, lo, hi);
}
}  // namespace

void Config::SetDefaults() {
    *this = Config{};
}

void Config::Validate() {
    static const Config kDefaults{};

    yawMultiplier = Sanitize(yawMultiplier, kDefaults.yawMultiplier, 0.1f, 5.0f);
    pitchMultiplier = Sanitize(pitchMultiplier, kDefaults.pitchMultiplier, 0.1f, 5.0f);
    rollMultiplier = Sanitize(rollMultiplier, kDefaults.rollMultiplier, 0.0f, 2.0f);

    positionSensitivityX = Sanitize(positionSensitivityX, kDefaults.positionSensitivityX, 0.1f, 10.0f);
    positionSensitivityY = Sanitize(positionSensitivityY, kDefaults.positionSensitivityY, 0.1f, 10.0f);
    positionSensitivityZ = Sanitize(positionSensitivityZ, kDefaults.positionSensitivityZ, 0.1f, 10.0f);

    positionLimitX = Sanitize(positionLimitX, kDefaults.positionLimitX, 0.01f, 2.0f);
    positionLimitY = Sanitize(positionLimitY, kDefaults.positionLimitY, 0.01f, 2.0f);
    positionLimitZ = Sanitize(positionLimitZ, kDefaults.positionLimitZ, 0.01f, 2.0f);
    positionLimitZBack = Sanitize(positionLimitZBack, kDefaults.positionLimitZBack, 0.01f, 2.0f);
    positionSmoothing = Sanitize(positionSmoothing, kDefaults.positionSmoothing, 0.0f, 0.99f);

    if (udpPort < 1024) {
        Logger::Instance().Warning("UDP port %d is in reserved range, using default %d",
                                   udpPort, DEFAULT_UDP_PORT);
        udpPort = DEFAULT_UDP_PORT;
    }
}

bool Config::Load(const char* path) {
    SetDefaults();

    cameraunlock::IniReader reader;
    if (!reader.Open(path)) {
        Logger::Instance().Warning("Could not load config from %s, using defaults", path);
        return false;
    }

    // Validate the full int before narrowing: a value above 65535 would
    // otherwise wrap silently into a valid-looking but wrong port.
    int rawPort = reader.ReadInt("Network", "UDPPort", udpPort);
    if (rawPort < 1024 || rawPort > 65535) {
        Logger::Instance().Warning("UDP port %d out of range (1024-65535), using default %d",
                                   rawPort, DEFAULT_UDP_PORT);
        udpPort = DEFAULT_UDP_PORT;
    } else {
        udpPort = static_cast<uint16_t>(rawPort);
    }

    yawMultiplier = reader.ReadFloat("Sensitivity", "YawMultiplier", yawMultiplier);
    pitchMultiplier = reader.ReadFloat("Sensitivity", "PitchMultiplier", pitchMultiplier);
    rollMultiplier = reader.ReadFloat("Sensitivity", "RollMultiplier", rollMultiplier);

    toggleKey = reader.ReadHex("Hotkeys", "ToggleKey", toggleKey);
    recenterKey = reader.ReadHex("Hotkeys", "RecenterKey", recenterKey);
    positionToggleKey = reader.ReadHex("Hotkeys", "PositionToggleKey", positionToggleKey);
    yawModeKey = reader.ReadHex("Hotkeys", "YawModeKey", yawModeKey);

    positionSensitivityX = reader.ReadFloat("Position", "SensitivityX", positionSensitivityX);
    positionSensitivityY = reader.ReadFloat("Position", "SensitivityY", positionSensitivityY);
    positionSensitivityZ = reader.ReadFloat("Position", "SensitivityZ", positionSensitivityZ);
    positionLimitX = reader.ReadFloat("Position", "LimitX", positionLimitX);
    positionLimitY = reader.ReadFloat("Position", "LimitY", positionLimitY);
    positionLimitZ = reader.ReadFloat("Position", "LimitZ", positionLimitZ);
    positionLimitZBack = reader.ReadFloat("Position", "LimitZBack", positionLimitZBack);
    positionSmoothing = reader.ReadFloat("Position", "Smoothing", positionSmoothing);
    positionInvertX = reader.ReadBool("Position", "InvertX", positionInvertX);
    positionInvertY = reader.ReadBool("Position", "InvertY", positionInvertY);
    positionInvertZ = reader.ReadBool("Position", "InvertZ", positionInvertZ);
    positionEnabled = reader.ReadBool("Position", "Enabled", positionEnabled);

    autoEnable = reader.ReadBool("General", "AutoEnable", autoEnable);
    worldSpaceYaw = reader.ReadBool("General", "WorldSpaceYaw", worldSpaceYaw);

    Validate();
    Logger::Instance().Info("Config loaded from %s", path);
    return true;
}

bool Config::Save(const char* path) const {
    std::ofstream file(path);
    if (!file.is_open()) {
        Logger::Instance().Error("Failed to save config to %s", path);
        return false;
    }

    file << "; RE7 Head Tracking Configuration\n";
    file << "; Delete this file to reset to defaults\n\n";

    file << "[Network]\n";
    file << "; UDP port for OpenTrack data (default: 4242)\n";
    file << "UDPPort=" << udpPort << "\n\n";

    file << "[Sensitivity]\n";
    file << "; Rotation sensitivity multipliers (1.0 = 1:1)\n";
    file << "YawMultiplier=" << yawMultiplier << "\n";
    file << "PitchMultiplier=" << pitchMultiplier << "\n";
    file << "RollMultiplier=" << rollMultiplier << "\n\n";

    file << "[Position]\n";
    file << "; Position tracking sensitivity (0.1-10.0, higher = more movement)\n";
    file << "SensitivityX=" << positionSensitivityX << "\n";
    file << "SensitivityY=" << positionSensitivityY << "\n";
    file << "SensitivityZ=" << positionSensitivityZ << "\n";
    file << "; Position limits in meters\n";
    file << "LimitX=" << positionLimitX << "\n";
    file << "LimitY=" << positionLimitY << "\n";
    file << "LimitZ=" << positionLimitZ << "\n";
    file << "LimitZBack=" << positionLimitZBack << "\n";
    file << "Smoothing=" << positionSmoothing << "\n";
    file << "InvertX=" << (positionInvertX ? "true" : "false") << "\n";
    file << "InvertY=" << (positionInvertY ? "true" : "false") << "\n";
    file << "InvertZ=" << (positionInvertZ ? "true" : "false") << "\n";
    file << "Enabled=" << (positionEnabled ? "true" : "false") << "\n\n";

    file << "[Hotkeys]\n";
    file << "; Virtual key codes (hex)\n";
    file << "ToggleKey=0x" << std::hex << toggleKey << "    ; End\n";
    file << "RecenterKey=0x" << std::hex << recenterKey << "  ; Home\n";
    file << "PositionToggleKey=0x" << std::hex << positionToggleKey << " ; Page Up\n";
    file << "YawModeKey=0x" << std::hex << yawModeKey << "      ; Page Down - toggle world/local yaw\n\n";

    file << "[General]\n";
    file << "AutoEnable=" << (autoEnable ? "true" : "false") << "\n";
    file << "; Yaw mode: false = camera-local, true = horizon-locked (default)\n";
    file << "WorldSpaceYaw=" << (worldSpaceYaw ? "true" : "false") << "\n";

    file.close();
    Logger::Instance().Info("Config saved to %s", path);
    return true;
}

} // namespace RE7HT
