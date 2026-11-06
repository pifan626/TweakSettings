#import <Foundation/Foundation.h>
#include <limits.h>

#ifdef THEOS_PACKAGE_INSTALL_PREFIX
static inline NSString *TSJailbreakInstallPrefix(void) {
    static NSString *installPrefix;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        NSString *bundlePath = NSBundle.mainBundle.bundlePath.stringByResolvingSymlinksInPath;
        NSString *appSuffix = @"/Applications/TweakSettings.app";
        if ([bundlePath hasSuffix:appSuffix]) {
            NSUInteger prefixLength = bundlePath.length - appSuffix.length;
            if (prefixLength > 0) {
                installPrefix = [bundlePath substringToIndex:prefixLength];
            } else {
                installPrefix = @"/";
            }
        }
        if (!installPrefix.length) {
            installPrefix = @THEOS_PACKAGE_INSTALL_PREFIX;
        }
    });
    return installPrefix;
}

static inline NSString *TSJailbreakPathString(NSString *path) {
    NSString *prefix = TSJailbreakInstallPrefix();
    if ([path hasPrefix:@"/"]) {
        return [prefix stringByAppendingString:path];
    }
    return [prefix stringByAppendingPathComponent:path];
}

static inline const char *TSJailbreakPathCString(const char *path) {
    NSString *pathString = [NSString stringWithUTF8String:path];
    return TSJailbreakPathString(pathString).fileSystemRepresentation;
}

#define ROOT_PATH(cPath) TSJailbreakPathCString(cPath)
#define ROOT_PATH_NS(path) TSJailbreakPathString(path)
#define ROOT_PATH_NS_VAR(path) TSJailbreakPathString(path)
#define ROOT_PATH_VAR(cPath) TSJailbreakPathCString(cPath)
#else
#define ROOT_PATH(cPath) cPath
#define ROOT_PATH_NS(path) path
#define ROOT_PATH_NS_VAR(path) path
#define ROOT_PATH_VAR(cPath) cPath
#endif
