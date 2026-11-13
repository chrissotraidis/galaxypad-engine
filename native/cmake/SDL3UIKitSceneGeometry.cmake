# SDL3 UIKit: take window geometry and orientation from the application's own
# window scene, and run the application only in the scene it started in.
#
#  - SDLUIKitSceneDelegate records the application scene SDL starts in. Other
#    scenes (an external display's, or a second window) no longer show a launch
#    screen, call the application's main function again, or forward app
#    lifecycle events.
#  - Window creation and the "active scene" fallback prefer that scene.
#  - Display orientation comes from that scene's effectiveGeometry (iOS 16+)
#    instead of UIApplication.statusBarOrientation, which reads as unknown in a
#    scene-based app on iOS 27.
#  - A window in a scene keeps its own bounds; the legacy status-bar
#    width/height swap remains only for windows without a scene.
#  - A geometry change of that scene refreshes the display orientation, the
#    Metal layer scale and layout, so SDL sends its usual resize and pixel-size
#    events. The game's render resolution is untouched.
# tvOS, visionOS and non-scene paths are unchanged. The same edit applies to SDL
# 3.4.4, 3.4.10 and 3.4.18, whose UIKit sources match apart from offsets.
#
# Each replacement is exact and idempotent: an already patched tree is left
# alone, and a tree without the expected text stops the configure.
#
# Use: include() this file and call sdl3_uikit_scene_geometry_patch(<SDL source
# dir>), or run cmake -DSDL_SOURCE_DIR=<dir> -P <this file>.

function(_sdl3_uikit_scene_replace file old new)
  file(READ "${file}" _content)
  string(FIND "${_content}" "${new}" _already)
  if (NOT _already EQUAL -1)
    return()
  endif ()
  string(FIND "${_content}" "${old}" _anchor)
  if (_anchor EQUAL -1)
    message(FATAL_ERROR "SDL3 UIKit scene geometry patch does not apply to ${file}; review it for this SDL version")
  endif ()
  string(REPLACE "${old}" "${new}" _content "${_content}")
  file(WRITE "${file}" "${_content}")
endfunction()

function(sdl3_uikit_scene_geometry_patch sdl_source_dir)
  set(_uikit "${sdl_source_dir}/src/video/uikit")
  if (NOT EXISTS "${_uikit}/SDL_uikitvideo.m")
    message(FATAL_ERROR "SDL3 UIKit sources not found under ${sdl_source_dir}")
  endif ()
  _sdl3_uikit_scene_replace("${_uikit}/SDL_uikitappdelegate.m"
[==[@end // SDLLaunchScreenController


API_AVAILABLE(ios(13.0))
@implementation SDLUIKitSceneDelegate
{
]==]
[==[@end // SDLLaunchScreenController


// Set once the application scene has started the application's main function.
static bool scene_main_started;

API_AVAILABLE(ios(13.0))
@implementation SDLUIKitSceneDelegate
{
]==])
  _sdl3_uikit_scene_replace("${_uikit}/SDL_uikitappdelegate.m"
[==[    UIWindowScene *windowScene = (UIWindowScene *)scene;
    windowScene.delegate = self;

    NSBundle *bundle = [NSBundle mainBundle];

#ifdef SDL_IPHONE_LAUNCHSCREEN
]==]
[==[    UIWindowScene *windowScene = (UIWindowScene *)scene;
    windowScene.delegate = self;

#ifndef SDL_PLATFORM_VISIONOS
    /* The application runs in the first application scene. Any other scene, such
     * as an external display's, must not show a launch screen or start the
     * application a second time. */
    if (![session.role isEqualToString:UIWindowSceneSessionRoleApplication]) {
        return;
    }
    UIWindowScene *owner = UIKit_GetOwningWindowScene();
    if (owner && owner != windowScene) {
        return;
    }
    UIKit_SetOwningWindowScene(windowScene);
    if (scene_main_started) {
        return;
    }
    scene_main_started = true;
#endif

    NSBundle *bundle = [NSBundle mainBundle];

#ifdef SDL_IPHONE_LAUNCHSCREEN
]==])
  _sdl3_uikit_scene_replace("${_uikit}/SDL_uikitappdelegate.m"
[==[

- (void)sceneDidBecomeActive:(UIScene *)scene
{
    SDL_OnApplicationDidEnterForeground();
}

- (void)sceneWillResignActive:(UIScene *)scene
{
    SDL_OnApplicationWillEnterBackground();
}

- (void)sceneWillEnterForeground:(UIScene *)scene
{
    SDL_OnApplicationWillEnterForeground();
}

- (void)sceneDidEnterBackground:(UIScene *)scene
{
    SDL_OnApplicationDidEnterBackground();
}

- (void)handleURL:(NSURL *)url
{
    const char *sourceApplicationCString = NULL;
]==]
[==[

- (void)sceneDidBecomeActive:(UIScene *)scene
{
    if (UIKit_IsOwningScene(scene)) {
        SDL_OnApplicationDidEnterForeground();
    }
}

- (void)sceneWillResignActive:(UIScene *)scene
{
    if (UIKit_IsOwningScene(scene)) {
        SDL_OnApplicationWillEnterBackground();
    }
}

- (void)sceneWillEnterForeground:(UIScene *)scene
{
    if (UIKit_IsOwningScene(scene)) {
        SDL_OnApplicationWillEnterForeground();
    }
}

- (void)sceneDidEnterBackground:(UIScene *)scene
{
    if (UIKit_IsOwningScene(scene)) {
        SDL_OnApplicationDidEnterBackground();
    }
}

#if !defined(SDL_PLATFORM_TVOS) && !defined(SDL_PLATFORM_VISIONOS)
- (void)windowScene:(UIWindowScene *)windowScene didUpdateCoordinateSpace:(id<UICoordinateSpace>)previousCoordinateSpace interfaceOrientation:(UIInterfaceOrientation)previousInterfaceOrientation traitCollection:(UITraitCollection *)previousTraitCollection
{
    if (@available(iOS 26.0, *)) {
        return; // windowScene:didUpdateEffectiveGeometry: reports the change
    }
    UIKit_OnWindowSceneGeometryChanged(windowScene);
}

- (void)windowScene:(UIWindowScene *)windowScene didUpdateEffectiveGeometry:(UIWindowSceneGeometry *)previousEffectiveGeometry API_AVAILABLE(ios(26.0))
{
    UIKit_OnWindowSceneGeometryChanged(windowScene);
}
#endif

- (void)handleURL:(NSURL *)url
{
    const char *sourceApplicationCString = NULL;
]==])
  _sdl3_uikit_scene_replace("${_uikit}/SDL_uikitmodes.m"
[==[{
#ifndef SDL_PLATFORM_TVOS
    if (uiscreen == [UIScreen mainScreen]) {
        return UIInterfaceOrientationIsLandscape([UIApplication sharedApplication].statusBarOrientation);
    } else
#endif // !SDL_PLATFORM_TVOS
    {
]==]
[==[{
#ifndef SDL_PLATFORM_TVOS
    if (uiscreen == [UIScreen mainScreen]) {
        return UIInterfaceOrientationIsLandscape(UIKit_GetInterfaceOrientation());
    } else
#endif // !SDL_PLATFORM_TVOS
    {
]==])
  _sdl3_uikit_scene_replace("${_uikit}/SDL_uikitmodes.m"
[==[#if !defined(SDL_PLATFORM_TVOS) && !defined(SDL_PLATFORM_VISIONOS)
void SDL_OnApplicationDidChangeStatusBarOrientation(void)
{
    BOOL isLandscape = UIInterfaceOrientationIsLandscape([UIApplication sharedApplication].statusBarOrientation);
    SDL_VideoDisplay *display = SDL_GetVideoDisplay(SDL_GetPrimaryDisplay());

    if (display) {
]==]
[==[#if !defined(SDL_PLATFORM_TVOS) && !defined(SDL_PLATFORM_VISIONOS)
void SDL_OnApplicationDidChangeStatusBarOrientation(void)
{
    UIInterfaceOrientation interfaceOrientation = UIKit_GetInterfaceOrientation();
    BOOL isLandscape = UIInterfaceOrientationIsLandscape(interfaceOrientation);
    SDL_VideoDisplay *display = SDL_GetVideoDisplay(SDL_GetPrimaryDisplay());

    if (display) {
]==])
  _sdl3_uikit_scene_replace("${_uikit}/SDL_uikitmodes.m"
[==[            }
        }

        switch ([UIApplication sharedApplication].statusBarOrientation) {
        case UIInterfaceOrientationPortrait:
            orientation = SDL_ORIENTATION_PORTRAIT;
            break;
]==]
[==[            }
        }

        switch (interfaceOrientation) {
        case UIInterfaceOrientationPortrait:
            orientation = SDL_ORIENTATION_PORTRAIT;
            break;
]==])
  _sdl3_uikit_scene_replace("${_uikit}/SDL_uikitvideo.h"
[==[#endif

extern API_AVAILABLE(ios(13.0)) UIWindowScene *UIKit_GetActiveWindowScene(void);

extern void UIKit_SetGameControllerInteraction(bool enabled);
extern void UIKit_SetViewGameControllerInteraction(UIView *view, bool enabled);
]==]
[==[#endif

extern API_AVAILABLE(ios(13.0)) UIWindowScene *UIKit_GetActiveWindowScene(void);
extern API_AVAILABLE(ios(13.0)) UIWindowScene *UIKit_GetOwningWindowScene(void);
extern API_AVAILABLE(ios(13.0)) void UIKit_SetOwningWindowScene(UIWindowScene *scene);
extern bool UIKit_IsOwningScene(UIScene *scene);
#if !defined(SDL_PLATFORM_TVOS) && !defined(SDL_PLATFORM_VISIONOS)
extern UIInterfaceOrientation UIKit_GetInterfaceOrientation(void);
extern API_AVAILABLE(ios(13.0)) void UIKit_OnWindowSceneGeometryChanged(UIWindowScene *scene);
#endif

extern void UIKit_SetGameControllerInteraction(bool enabled);
extern void UIKit_SetViewGameControllerInteraction(UIView *view, bool enabled);
]==])
  _sdl3_uikit_scene_replace("${_uikit}/SDL_uikitvideo.m"
[==[

    /* The orientation flip doesn't make sense when the window is smaller
     * than the screen (iPad Split View, for example). */
    if (fullscreen && (landscape != (frame.size.width > frame.size.height))) {
        float height = frame.size.width;
        frame.size.width = frame.size.height;
        frame.size.height = height;
]==]
[==[

    /* The orientation flip doesn't make sense when the window is smaller
     * than the screen (iPad Split View, for example). */
    BOOL scene_window = NO;
    if (@available(iOS 13.0, *)) {
        /* A window in a scene is sized by that scene: its bounds already follow
         * the scene's interface orientation and screen (including an external
         * display), and the application-wide status bar orientation may belong
         * to another scene or be unknown. */
        scene_window = (data != nil && data.uiwindow.windowScene != nil);
    }
    if (!scene_window && fullscreen && (landscape != (frame.size.width > frame.size.height))) {
        float height = frame.size.width;
        frame.size.width = frame.size.height;
        frame.size.height = height;
]==])
  _sdl3_uikit_scene_replace("${_uikit}/SDL_uikitvideo.m"
[==[}
#endif // SDL_PLATFORM_VISIONOS

UIWindowScene *UIKit_GetActiveWindowScene(void)
{
    if (@available(iOS 13.0, tvOS 13.0, *)) {
        NSSet<UIScene *> *connectedScenes = [UIApplication sharedApplication].connectedScenes;

        // First, try to find an active foreground scene
        for (UIScene *scene in connectedScenes) {
            if ([scene isKindOfClass:[UIWindowScene class]]) {
]==]
[==[}
#endif // SDL_PLATFORM_VISIONOS

// The application scene SDL started in (see SDLUIKitSceneDelegate).
static __weak UIWindowScene *UIKit_owning_scene API_AVAILABLE(ios(13.0));

UIWindowScene *UIKit_GetOwningWindowScene(void)
{
    if (@available(iOS 13.0, tvOS 13.0, *)) {
        UIWindowScene *scene = UIKit_owning_scene;
        if (scene && scene.activationState != UISceneActivationStateUnattached) {
            return scene;
        }
    }
    return nil;
}

void UIKit_SetOwningWindowScene(UIWindowScene *scene)
{
    if (@available(iOS 13.0, tvOS 13.0, *)) {
        UIKit_owning_scene = scene;
    }
}

bool UIKit_IsOwningScene(UIScene *scene)
{
    if (@available(iOS 13.0, tvOS 13.0, *)) {
        UIWindowScene *owner = UIKit_GetOwningWindowScene();
        return owner == nil || owner == scene;
    }
    return true;
}

#if !defined(SDL_PLATFORM_TVOS) && !defined(SDL_PLATFORM_VISIONOS)
UIInterfaceOrientation UIKit_GetInterfaceOrientation(void)
{
    if (@available(iOS 13.0, *)) {
        UIWindowScene *scene = UIKit_GetActiveWindowScene();
        if (scene) {
            if (@available(iOS 16.0, *)) {
                return scene.effectiveGeometry.interfaceOrientation;
            }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            return scene.interfaceOrientation;
#pragma clang diagnostic pop
        }
    }
    return [UIApplication sharedApplication].statusBarOrientation;
}
#endif

UIWindowScene *UIKit_GetActiveWindowScene(void)
{
    if (@available(iOS 13.0, tvOS 13.0, *)) {
        NSSet<UIScene *> *connectedScenes = [UIApplication sharedApplication].connectedScenes;

        // Prefer the scene the application runs in over any other connected scene
        UIWindowScene *owner = UIKit_GetOwningWindowScene();
        if (owner) {
            return owner;
        }

        // First, try to find an active foreground scene
        for (UIScene *scene in connectedScenes) {
            if ([scene isKindOfClass:[UIWindowScene class]]) {
]==])
  _sdl3_uikit_scene_replace("${_uikit}/SDL_uikitwindow.m"
[==[    SDL_UIKitDisplayData *displaydata = (__bridge SDL_UIKitDisplayData *)display->internal;
    SDL_uikitview *view;

#ifdef SDL_PLATFORM_VISIONOS
    CGRect frame = UIKit_ComputeViewFrame(window);
#else
    CGRect frame = UIKit_ComputeViewFrame(window, displaydata.uiscreen);
#endif

    int width = (int)frame.size.width;
    int height = (int)frame.size.height;

    SDL_UIKitWindowData *data = [[SDL_UIKitWindowData alloc] init];
    if (!data) {
        return SDL_OutOfMemory();
]==]
[==[    SDL_UIKitDisplayData *displaydata = (__bridge SDL_UIKitDisplayData *)display->internal;
    SDL_uikitview *view;

    SDL_UIKitWindowData *data = [[SDL_UIKitWindowData alloc] init];
    if (!data) {
        return SDL_OutOfMemory();
]==])
  _sdl3_uikit_scene_replace("${_uikit}/SDL_uikitwindow.m"
[==[

    data.uiwindow = uiwindow;

#ifndef SDL_PLATFORM_VISIONOS
    if (displaydata.uiscreen != [UIScreen mainScreen]) {
        window->flags &= ~SDL_WINDOW_RESIZABLE;   // window is NEVER resizable
]==]
[==[

    data.uiwindow = uiwindow;

    // Compute the frame once the UIWindow is known, so a scene window uses its own bounds.
#ifdef SDL_PLATFORM_VISIONOS
    CGRect frame = UIKit_ComputeViewFrame(window);
#else
    CGRect frame = UIKit_ComputeViewFrame(window, displaydata.uiscreen);
#endif

    int width = (int)frame.size.width;
    int height = (int)frame.size.height;

#ifndef SDL_PLATFORM_VISIONOS
    if (displaydata.uiscreen != [UIScreen mainScreen]) {
        window->flags &= ~SDL_WINDOW_RESIZABLE;   // window is NEVER resizable
]==])
  _sdl3_uikit_scene_replace("${_uikit}/SDL_uikitwindow.m"
[==[    }
}

#ifndef SDL_PLATFORM_TVOS
NSUInteger
UIKit_GetSupportedOrientations(SDL_Window *window)
]==]
[==[    }
}

#if !defined(SDL_PLATFORM_TVOS) && !defined(SDL_PLATFORM_VISIONOS)
/* The application scene changed size, orientation or screen: bring the display
 * orientation and each of its windows' layout and Metal scale up to date, so the
 * usual resize and pixel-size events follow. */
void UIKit_OnWindowSceneGeometryChanged(UIWindowScene *scene)
{
    @autoreleasepool {
        SDL_VideoDevice *_this = SDL_GetVideoDevice();
        SDL_Window *window;

        if (!_this || scene != UIKit_GetOwningWindowScene()) {
            return;
        }

        SDL_OnApplicationDidChangeStatusBarOrientation();

        for (window = _this->windows; window; window = window->next) {
            SDL_UIKitWindowData *data = (__bridge SDL_UIKitWindowData *)window->internal;
            UIView *view = data.viewcontroller.view;
            if (!data || data.uiwindow.windowScene != scene || !view) {
                continue;
            }
            if ((window->flags & SDL_WINDOW_HIGH_PIXEL_DENSITY) && view.tag == SDL_METALVIEW_TAG) {
                view.layer.contentsScale = data.uiwindow.screen.nativeScale;
            }
            [view setNeedsLayout];
        }
    }
}
#endif

#ifndef SDL_PLATFORM_TVOS
NSUInteger
UIKit_GetSupportedOrientations(SDL_Window *window)
]==])
  message(STATUS "SDL3 UIKit scene geometry patch checked: ${sdl_source_dir}")
endfunction()

if (CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE AND DEFINED SDL_SOURCE_DIR)
  sdl3_uikit_scene_geometry_patch("${SDL_SOURCE_DIR}")
endif ()
