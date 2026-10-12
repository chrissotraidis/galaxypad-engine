// GalaxyPad engine framework entry (iOS). GalaxyPad links GalaxyPadEngine.framework
// next to its Old engine and calls galaxypad_engine_main() from its own main()
// when the New engine is selected. GalaxyPad then draws its own touch controls and
// menu over the game (PETARI_TOUCH_CONTROLS=0), reads game controllers itself
// (PETARI_SDL_GAMEPADS=0), and drives the game through the galaxypad_engine_*
// functions below, the framework's only exports.
//
// SDL's Objective-C classes are renamed with a GPE_ prefix in this build
// (native/app/CMakeLists.txt), because the Original engine links its own SDL and
// Objective-C class names are process-wide. SDL looks two of them up by name;
// this category returns the renamed names, as SDL's subclassing notes describe.
//
// Scenes: UIKit reuses a saved scene session's configuration at the next launch,
// and GalaxyPad's Original engine needs its own scene delegate. So inside
// GalaxyPad every scene uses GalaxyPad's configuration ("GalaxyPad" in its
// Info.plist), whose delegate hands the scene to SDL's while this engine runs.

#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL_main.h>

#import <UIKit/UIKit.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

#include "host.hpp"
#include "petari/audio_sdl.hpp"
#include "petari/home_menu.hpp"
#include "petari/input.hpp"
#include "petari/mods.hpp"

extern "C" int aurora_main(int argc, char* argv[]);  // app_main.cpp (aurora/main.h renames main)

@interface SDLUIKitDelegate : NSObject
@end
@interface SDLUIKitSceneDelegate : SDLUIKitDelegate
@end

@interface GalaxyPadEngineAppDelegate : SDLUIKitSceneDelegate
@end

@implementation GalaxyPadEngineAppDelegate
- (UISceneConfiguration*)application:(UIApplication*)application
    configurationForConnectingSceneSession:(UISceneSession*)session
                                   options:(UISceneConnectionOptions*)options {
    (void)application;
    (void)options;
    return [[UISceneConfiguration alloc] initWithName:@"GalaxyPad" sessionRole:session.role];
}
@end

@implementation SDLUIKitDelegate (GalaxyPadEngine)
+ (NSString*)getAppDelegateClassName {
    return NSStringFromClass(self);
}
@end

@implementation SDLUIKitSceneDelegate (GalaxyPadEngine)
+ (NSString*)getSceneDelegateClassName {
    return NSStringFromClass(GalaxyPadEngineAppDelegate.class);
}
@end

extern "C" __attribute__((visibility("default"))) int galaxypad_engine_main(int argc, char* argv[]) {
    return SDL_RunApp(argc, argv, aurora_main, nullptr);
}

// --- GalaxyPad's controls ---

namespace {
namespace App = PetariNative::App;
namespace Input = PetariNative::Input;
using Input::PadButton;

// GalaxyPad's buttons (galaxypad::Button in its apple/shared/GalaxyPadInput.h)
// press the controller buttons the engine's own touch controls use
// (touch_controls.cpp), so remapped controls apply to both. A presses Start
// (North): A everywhere, with B while the title asks for both, so one tap starts.
struct Route {
    std::uint32_t bit;
    PadButton pad;          // PadButton::Count: a key instead
    Input::KeyCode key;
};
constexpr Route kRoutes[] = {
    {1u << 0, PadButton::North, 0},         // A (the Start action)
    {1u << 1, PadButton::East, 0},          // B
    {1u << 2, PadButton::LeftShoulder, 0},  // C
    {1u << 3, PadButton::LeftTrigger, 0},   // Z
    {1u << 4, PadButton::Start, 0},         // Plus
    {1u << 5, PadButton::Back, 0},          // Minus
    {1u << 6, PadButton::West, 0},          // Spin
    {1u << 7, PadButton::DpadUp, 0},
    {1u << 8, PadButton::DpadDown, 0},
    {1u << 9, PadButton::DpadLeft, 0},
    {1u << 10, PadButton::DpadRight, 0},
    {1u << 11, PadButton::Count, Input::Key::Num1},
    {1u << 12, PadButton::Count, Input::Key::Num2},
    {1u << 13, PadButton::Guide, 0},  // Home
};

std::mutex gMutex;
std::uint32_t gButtons = 0;
float gStickX = 0.0f, gStickY = 0.0f;
bool gPointer = false;
float gPointerX = -1.0f, gPointerY = -1.0f;
}  // namespace

// The whole state of GalaxyPad's controls, whenever it changes (main thread).
// buttons: galaxypad::Button bits. Stick: -1..1, y up. Pointer: 0..1 across the
// game image, top-left origin; hidden when pointerVisible is 0.
extern "C" __attribute__((visibility("default"))) void galaxypad_engine_input(std::uint32_t buttons, float stickX,
                                                                             float stickY, int pointerVisible,
                                                                             float pointerX, float pointerY) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (buttons != 0 || stickX != 0.0f || stickY != 0.0f || pointerVisible) Input::focusChanged(true);
    const std::uint32_t changed = buttons ^ gButtons;
    for (const Route& route : kRoutes) {
        if ((changed & route.bit) == 0) continue;
        const bool down = (buttons & route.bit) != 0;
        if (route.pad == PadButton::Count) {
            Input::keyEvent(route.key, down, false);
        } else {
            Input::padButtonEvent(route.pad, down);
        }
    }
    gButtons = buttons;
    if (stickX != gStickX || stickY != gStickY) {
        gStickX = stickX;
        gStickY = stickY;
        Input::padAxisEvent(Input::PadAxis::LeftX, stickX);
        Input::padAxisEvent(Input::PadAxis::LeftY, -stickY);  // the input layer's y is down
    }
    App::Rect image;
    float width = 0.0f, height = 0.0f;
    if (pointerVisible && App::Events::image(&image, &width, &height)) {
        if (!gPointer || pointerX != gPointerX || pointerY != gPointerY) {
            Input::mouseMoved(image.x + pointerX * image.width, image.y + pointerY * image.height);
        }
        gPointer = true;
        gPointerX = pointerX;
        gPointerY = pointerY;
    } else if (gPointer) {
        gPointer = false;
        Input::mouseLeft();
    }
}

// Where the game image is, as fractions of the window (GalaxyPad's pointer
// area). Returns 0 before the first frame.
extern "C" __attribute__((visibility("default"))) int galaxypad_engine_image(float* x, float* y, float* width,
                                                                            float* height) {
    App::Rect image;
    float windowWidth = 0.0f, windowHeight = 0.0f;
    if (!App::Events::image(&image, &windowWidth, &windowHeight)) return 0;
    *x = image.x / windowWidth;
    *y = image.y / windowHeight;
    *width = image.width / windowWidth;
    *height = image.height / windowHeight;
    return 1;
}

// 1 once shader preparation has finished and the game runs. Before that a tap
// on the game's own screen starts early, so GalaxyPad's controls stay hidden.
extern "C" __attribute__((visibility("default"))) int galaxypad_engine_started(void) {
    return App::Events::gameStarted() ? 1 : 0;
}

// GalaxyPad's menu chose the Original engine: the game closes like the Home
// menu's Quit; once it has, its save is copied to the Wii save and GalaxyPad's
// Engine setting becomes Original, as the Home menu's "Original engine..." does.
extern "C" __attribute__((visibility("default"))) void galaxypad_engine_switch_to_original(void) {
    App::Host::setBeforeExit(PetariNative::HomeMenu::switchToOriginalEngine);
    App::Host::requestQuit();
}

// Stop Game: the game closes like the Home menu's Quit.
extern "C" __attribute__((visibility("default"))) void galaxypad_engine_quit(void) {
    App::Host::requestQuit();
}

namespace {
std::atomic<float> gVolume{1.0f};
std::atomic<bool> gPausedAudio{false};
}  // namespace

// GalaxyPad's menus and pause screen: holds the game on its current frame, silent.
extern "C" __attribute__((visibility("default"))) void galaxypad_engine_pause(int paused) {
    App::Seam::setPaused(paused != 0);
    gPausedAudio.store(paused != 0);
    PetariNative::AudioSDL::setGain(paused ? 0.0f : gVolume.load());
    if (paused) Input::focusChanged(false);  // nothing stays held while paused
}

// Audio menu: 0 (mute) to 1.
extern "C" __attribute__((visibility("default"))) void galaxypad_engine_set_volume(float volume) {
    gVolume.store(volume);
    if (!gPausedAudio.load()) PetariNative::AudioSDL::setGain(volume);
}

// Display menu, Aspect Ratio: 1 for 16:9, 0 for 4:3, saved for the next start;
// a negative value only reads the setting.
extern "C" __attribute__((visibility("default"))) int galaxypad_engine_widescreen(int value) {
    return App::Host::widescreen(value);
}

// Display menu, Smooth Motion (native/MODS.md): 1 on, 0 off, applied at once and
// saved in mods.txt; a negative value only reads it.
extern "C" __attribute__((visibility("default"))) int galaxypad_engine_smooth_motion(int value) {
    using PetariNative::Mods::Mod;
    if (value >= 0 && PetariNative::Mods::enabled(Mod::SmoothMotion) != (value != 0)) {
        PetariNative::Mods::setEnabled(Mod::SmoothMotion, value != 0);
        std::string error;
        if (!PetariNative::Mods::save(&error)) std::fprintf(stderr, "petari: mods: %s\n", error.c_str());
    }
    return PetariNative::Mods::enabled(Mod::SmoothMotion) ? 1 : 0;
}

// Frames presented so far (the FPS counter).
extern "C" __attribute__((visibility("default"))) std::uint64_t galaxypad_engine_presented_frames(void) {
    return App::Seam::presentedFrames();
}
