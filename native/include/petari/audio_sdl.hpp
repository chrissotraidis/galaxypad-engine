#pragma once
#include <cstdint>

namespace PetariNative::AudioSDL {
// Install before AIStartDMA. shutdown() must precede SDL_Quit and game-heap teardown.
void install();
void shutdown();
bool active();
bool outputMuted();
// Output volume, 0 (silent) to 1 (full); GalaxyPad's Audio menu. Any time.
void setGain(float value);
std::uint64_t submittedFrames();
}
