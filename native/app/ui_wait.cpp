// The paused frame seam's wait (host.hpp, Seam::waitWhilePaused). Separate from
// frame_seam.cpp because CoreFoundation's headers declare a global Rect.

#include <TargetConditionals.h>

#if TARGET_OS_IPHONE
#include <CoreFoundation/CoreFoundation.h>
#else
#include <SDL3/SDL_timer.h>
#endif

#include "host.hpp"

namespace PetariNative::App::Seam {

void waitWhilePaused() {
#if TARGET_OS_IPHONE
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0 / 120.0, true);
#else
    SDL_DelayNS(8000000);
#endif
}

}  // namespace PetariNative::App::Seam
