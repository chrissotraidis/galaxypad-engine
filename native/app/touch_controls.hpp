// Touch controls for phones and tablets: an on-screen Nunchuk stick, the
// Wii Remote and Nunchuk buttons, and a direct Star Pointer under the finger.
// They feed the same input layer as a game controller and the mouse, so the
// game's own KPAD processing is unchanged.
#pragma once

union SDL_Event;

namespace PetariNative::App::TouchControls {

// On by default on iOS; PETARI_TOUCH_CONTROLS=0/1 overrides.
bool enabled();
// Window size in the same units as SDL mouse coordinates.
void setWindow(float width, float height);
// Returns true when the event was a touch the controls consumed.
bool handle(const SDL_Event& event);
// Draws the controls with Dear ImGui (call between NewFrame and Render).
void draw();
// Releases every held control (focus loss, menus).
void releaseAll();

// Test route (PETARI_SMOKE_VIA_TOUCH=1): the smoke script's presses become
// synthetic finger events at the controls' on-screen positions, handled by the
// same code as real touches.
bool smokeViaTouch();
enum class Synthetic { A, B, Spin, Z, Plus, Minus, StickUp, StickDown, StickLeft, StickRight };
void synthetic(Synthetic which, bool down);
void syntheticPointer(float x, float y);
// A whole two-finger gesture: move both by (dx, dy) points and scale their spread
// by pinch (> 1 spreads: zoom in). Needs the Odyssey camera mod.
void syntheticCamera(float dx, float dy, float pinch);

}  // namespace PetariNative::App::TouchControls

