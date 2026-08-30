#pragma once

#include <cameraunlock/reframework/plugin_config.h>

namespace RE7HT {

using Config = cameraunlock::reframework::PluginConfig;

// RE7's INI schema: the [Position] Invert keys it has always exposed, no
// [Flashlight] section and no diagnostic marker key.
inline constexpr cameraunlock::reframework::PluginConfigSchema kConfigSchema{
    /*title*/ "RE7 Head Tracking",
    /*positionInvertKeys*/ true,
    /*flashlight*/ false,
    /*diagnosticMarkerKey*/ false,
    /*positionSensitivity*/ 1.0f,
};

} // namespace RE7HT
