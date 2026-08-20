#pragma once

namespace RE7HT {

// Returns true if the player is in active gameplay (not paused, menu, loading, etc.)
bool IsInGameplay();


// Called from the GUI draw hook when a title / main-menu / loading element
// draws. Records a timestamp the gameplay gate uses to suppress tracking over
// the menu's live 3D backdrop (which otherwise passes every other tier).
void NotifyMainMenuDrawn();

} // namespace RE7HT
