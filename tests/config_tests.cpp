// Config loading/validation tests.
//
// HeadTracking.ini is a boundary the user controls. The port is read as a
// signed int and then narrowed to uint16_t; a value above 65535 used to wrap
// silently into a valid-looking but wrong port. These tests pin the
// range-validation and the clamping done by Validate().

#include "pch.h"
#include "core/config.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

namespace {

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

std::string TempIniPath() {
    char dir[MAX_PATH] = {};
    DWORD n = GetTempPathA(MAX_PATH, dir);
    std::string base = (n > 0 && n < MAX_PATH) ? std::string(dir) : std::string(".\\");
    return base + "re7ht_config_test.ini";
}

// Write a minimal INI containing a single Network/UDPPort entry.
void WritePortIni(const std::string& path, const char* portValue) {
    std::ofstream f(path, std::ios::trunc);
    f << "[Network]\n";
    f << "UDPPort=" << portValue << "\n";
    f.close();
}

}  // namespace

int RunConfigTests() {
    using RE7HT::Config;

    std::cout << "Config tests\n";

    const std::string path = TempIniPath();

    // Valid in-range port is honoured.
    {
        WritePortIni(path, "5555");
        Config cfg;
        Check(cfg.Load(path.c_str()), "valid config loads");
        Check(cfg.udpPort == 5555, "in-range port honoured");
    }

    // Port above uint16_t range must fall back to default, not wrap.
    {
        WritePortIni(path, "70000");
        Config cfg;
        cfg.Load(path.c_str());
        Check(cfg.udpPort == RE7HT::DEFAULT_UDP_PORT, "out-of-range high port -> default (no silent wrap)");
    }

    // Reserved low port falls back to default.
    {
        WritePortIni(path, "500");
        Config cfg;
        cfg.Load(path.c_str());
        Check(cfg.udpPort == RE7HT::DEFAULT_UDP_PORT, "reserved low port -> default");
    }

    // Validate() clamps sensitivities into their documented ranges.
    {
        Config cfg;
        cfg.yawMultiplier = 99.0f;     // above max 5.0
        cfg.pitchMultiplier = -1.0f;   // below min 0.1
        cfg.rollMultiplier = 50.0f;    // above max 2.0
        cfg.positionSmoothing = 5.0f;  // above max 0.99
        cfg.Validate();
        Check(cfg.yawMultiplier == 5.0f, "yaw multiplier clamped to max");
        Check(cfg.pitchMultiplier == 0.1f, "pitch multiplier clamped to min");
        Check(cfg.rollMultiplier == 2.0f, "roll multiplier clamped to max");
        Check(cfg.positionSmoothing == 0.99f, "position smoothing clamped to max");
    }

    // Validate() must sanitize non-finite values. std::clamp passes NaN through
    // unchanged, so without an explicit isfinite guard a NaN/Inf from a corrupt
    // INI poisons the sin/cos/view-matrix math. Every float field must come out
    // finite and in-range, falling back to its default.
    {
        const Config defaults;
        Config cfg;
        const float kNaN = std::nanf("");
        const float kInf = std::numeric_limits<float>::infinity();
        cfg.yawMultiplier = kNaN;
        cfg.pitchMultiplier = kInf;
        cfg.rollMultiplier = -kInf;
        cfg.positionSensitivityX = kNaN;
        cfg.positionSensitivityY = kInf;
        cfg.positionSensitivityZ = kNaN;
        cfg.positionLimitX = kNaN;
        cfg.positionLimitY = kInf;
        cfg.positionLimitZ = kNaN;
        cfg.positionLimitZBack = kInf;
        cfg.positionSmoothing = kNaN;
        cfg.Validate();

        Check(std::isfinite(cfg.yawMultiplier), "NaN yaw multiplier sanitized to finite");
        Check(std::isfinite(cfg.pitchMultiplier), "Inf pitch multiplier sanitized to finite");
        Check(std::isfinite(cfg.rollMultiplier), "-Inf roll multiplier sanitized to finite");
        Check(std::isfinite(cfg.positionSensitivityX), "NaN posSensX sanitized to finite");
        Check(std::isfinite(cfg.positionSensitivityY), "Inf posSensY sanitized to finite");
        Check(std::isfinite(cfg.positionSensitivityZ), "NaN posSensZ sanitized to finite");
        Check(std::isfinite(cfg.positionLimitX), "NaN limitX sanitized to finite");
        Check(std::isfinite(cfg.positionLimitY), "Inf limitY sanitized to finite");
        Check(std::isfinite(cfg.positionLimitZ), "NaN limitZ sanitized to finite");
        Check(std::isfinite(cfg.positionLimitZBack), "Inf limitZBack sanitized to finite");
        Check(std::isfinite(cfg.positionSmoothing), "NaN smoothing sanitized to finite");
        // Non-finite fields fall back to their documented default.
        Check(cfg.yawMultiplier == defaults.yawMultiplier, "NaN yaw multiplier falls back to default");
        Check(cfg.positionSmoothing == defaults.positionSmoothing, "NaN smoothing falls back to default");
    }

    // A non-finite value read from an actual INI file must also be sanitized,
    // proving the strtod -> ReadFloat -> Validate path is covered end to end.
    {
        std::ofstream f(path, std::ios::trunc);
        f << "[Sensitivity]\n";
        f << "YawMultiplier=nan\n";
        f << "PitchMultiplier=1e400\n";  // overflows to +inf
        f.close();

        Config cfg;
        cfg.Load(path.c_str());
        Check(std::isfinite(cfg.yawMultiplier), "INI 'nan' yaw multiplier sanitized");
        Check(std::isfinite(cfg.pitchMultiplier), "INI overflow pitch multiplier sanitized");
    }

    std::remove(path.c_str());
    return g_failures;
}
