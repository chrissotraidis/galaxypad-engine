// Touch controls (native/app/touch_controls.hpp) switched off, as on a desktop
// without a touch screen, for tests that link the input layer without ImGui.
#include "../app/touch_controls.hpp"

namespace PetariNative::App::TouchControls {
bool enabled() { return false; }
void setWindow(float, float) {}
bool handle(const SDL_Event&) { return false; }
void draw() {}
void releaseAll() {}
bool smokeViaTouch() { return false; }
void synthetic(Synthetic, bool) {}
void syntheticPointer(float, float) {}
}  // namespace PetariNative::App::TouchControls
