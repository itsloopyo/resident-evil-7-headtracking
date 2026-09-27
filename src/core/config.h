#pragma once

#include <cameraunlock/reframework/plugin_config.h>

namespace RE7HT {

using Config = cameraunlock::reframework::PluginConfig;

// The game's name as cameraunlock-core's data/games.json spells it, written at
// the top of CameraUnlock.ini.
inline constexpr const char* kGameName = "Resident Evil 7 biohazard";

// RE7's INI schema: the [Position] Invert keys it has always exposed, no
// [Flashlight] section and no diagnostic marker key. The sensitivities and
// inversions are not settings any more: the canonical file has no row for
// them, so PluginMod applies SetDefaults' values, 1:1 and no inversion, whatever
// the file holds. The Invert keys are still named here because the legacy
// import reads them out of an old HeadTracking.ini to report a changed one as
// dropped.
//
// canonicalConfig: settings live in reframework\plugins\CameraUnlock.ini, and
// HeadTracking.ini, the file every earlier build read, is imported once while
// CameraUnlock.ini is absent and never written.
inline constexpr cameraunlock::reframework::PluginConfigSchema kConfigSchema{
    /*title*/ "RE7 Head Tracking",
    /*positionInvertKeys*/ true,
    /*flashlight*/ false,
    /*diagnosticMarkerKey*/ false,
    /*positionSensitivity*/ 1.0f,
    /*modId*/ "re7",
    /*canonicalConfig*/ true,
};

} // namespace RE7HT
