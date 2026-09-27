#pragma once

#include <cstdint>

namespace RE7HT {

inline constexpr const char* RE7HT_VERSION = "0.0.0";
inline constexpr const char* RE7HT_PLUGIN_NAME = "RE7 Head Tracking";

inline constexpr uint16_t DEFAULT_UDP_PORT = 4242;

inline constexpr int DEFAULT_TOGGLE_KEY = 0x23;           // VK_END
inline constexpr int DEFAULT_POSITION_TOGGLE_KEY = 0x21;   // VK_PRIOR (Page Up)
inline constexpr int DEFAULT_YAW_MODE_KEY = 0x22;          // VK_NEXT (Page Down)

} // namespace RE7HT
