#pragma once

namespace RE7HT {

// Called from plugin_main's pre-BeginRendering callback
void OnPreBeginRendering();

// Called from plugin_main's post-BeginRendering callback — restores clean matrix
// so game logic (aim, raycasts, physics) never sees head-tracked state.
void OnPostBeginRendering();

// Called from plugin_main's on_pre_gui_draw_element callback. Discovery build:
// logs unique GUI element GameObject names via managed invokes only (never
// touches the raw context arg that crashed the RE9 GUI layer on RE7).
bool OnPreGuiDrawElement(void* element, void* context);

} // namespace RE7HT
