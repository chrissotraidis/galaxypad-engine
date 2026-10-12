// GalaxyPad engine framework entry (iOS). GalaxyPad links GalaxyPadEngine.framework
// next to its Original engine and calls galaxypad_engine_main() from its own main()
// when the New engine is selected; it is the framework's only exported symbol.
//
// SDL's Objective-C classes are renamed with a GPE_ prefix in this build
// (native/app/CMakeLists.txt), because the Original engine links its own SDL and
// Objective-C class names are process-wide. SDL looks two of them up by name;
// this category returns the renamed names, as SDL's subclassing notes describe.

#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL_main.h>

#import <Foundation/Foundation.h>

extern "C" int aurora_main(int argc, char* argv[]);  // app_main.cpp (aurora/main.h renames main)

@interface SDLUIKitDelegate : NSObject
@end
@interface SDLUIKitSceneDelegate : NSObject
@end

@implementation SDLUIKitDelegate (GalaxyPadEngine)
+ (NSString*)getAppDelegateClassName {
    return NSStringFromClass(self);
}
@end

@implementation SDLUIKitSceneDelegate (GalaxyPadEngine)
+ (NSString*)getSceneDelegateClassName {
    return NSStringFromClass(self);
}
@end

extern "C" __attribute__((visibility("default"))) int galaxypad_engine_main(int argc, char* argv[]) {
    return SDL_RunApp(argc, argv, aurora_main, nullptr);
}
