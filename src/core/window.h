#pragma once

namespace RE7HT {

// Center the game's top-level window on its current monitor's work area.
// Runs at most once per process; subsequent calls are no-ops so users can
// drag the window afterwards without us yanking it back. Skips windows
// already at/above the work-area size (true fullscreen, or borderless
// fullscreen the user is happy with).
//
// Reason for existing: on super-ultrawide setups (e.g. 5120x1440) RE Engine
// titles can launch their window in the top-left corner with the title bar
// refusing drag input until the first cinematic ends (first observed on
// RE:Requiem), so the head tracking experience starts off-axis with no easy
// way to fix it.
void CenterGameWindowOnce();

} // namespace RE7HT
