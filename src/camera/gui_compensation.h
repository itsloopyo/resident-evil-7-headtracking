#pragma once

namespace RE7HT {

// on_pre_gui_draw_element callback: compensates world-anchored markers and
// feeds the main-menu suppression signal. Returns true to keep drawing.
bool OnPreGuiDrawElement(void* element, void* context);

} // namespace RE7HT
