#pragma once

namespace RE7HT {

// Returns true if the player is in active gameplay (not paused, menu, loading, etc.)
bool IsInGameplay();

// Returns true once after transitioning from non-gameplay to gameplay (for auto-recenter)
bool ShouldRecenter();

} // namespace RE7HT
