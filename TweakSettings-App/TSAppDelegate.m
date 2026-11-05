//
//  AppDelegate.m
//  TweakSettings
//
//  Created by Dana Buehre on 5/16/21.
//
//

#import "TSAppDelegate.h"
#import <CoreSpotlight/CoreSpotlight.h>
#import <dlfcn.h>
#import "TSRootListController.h"
#import "Localizable.h"
#import "TSRootNavigationManager.h"
#import "TSUserDefaults.h"
#import "rootless.h"
#include <unistd.h>


static void HandleExceptions(NSException *exception) {
    NSLog(@"TweakSettings unhandled exception: %@", exception.debugDescription);
}

@implementation TSAppDelegate

#pragma mark - UIApplicationDelegate

- (BOOL)application:(UIApplication *)application willFinishLaunchingWithOptions:(NSDictionary<UIApplicationLaunchOptionsKey, id> *)launchOptions {

    NSSetUncaughtExceptionHandler(&HandleExceptions);

    application.shortcutItems = @[
            [[UIApplicationShortcutItem alloc] initWithType:TSActionTypeTweakInject localizedTitle:NSLocalizedString(TWEAKINJECT_TITLE_KEY, nil) localizedSubtitle:NSLocalizedString(TWEAKINJECT_SUBTITLE_KEY, nil) icon:nil userInfo:nil],
            [[UIApplicationShortcutItem alloc] initWithType:TSActionTypeUICache localizedTitle:NSLocalizedString(UICACHE_TITLE_KEY, nil) localizedSubtitle:NSLocalizedString(UICACHE_SUBTITLE_KEY, nil) icon:nil userInfo:nil],
            [[UIApplicationShortcutItem alloc] initWithType:TSActionTypeSafemode localizedTitle:NSLocalizedString(SAFEMODE_TITLE_KEY, nil) localizedSubtitle:NSLocalizedString(SAFEMODE_SUBTITLE_KEY, nil) icon:nil userInfo:nil],
            [[UIApplicationShortcutItem alloc] initWithType:TSActionTypeRespring localizedTitle:NSLocalizedString(RESPRING_TITLE_KEY, nil) localizedSubtitle:NSLocalizedString(RESPRING_SUBTITLE_KEY, nil) icon:nil userInfo:nil]
    ];

    _navigationManager = [TSRootNavigationManager new];

    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = (id)_navigationManager.splitController;
    [self.window makeKeyAndVisible];

    return YES;
}

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {

    if (launchOptions[UIApplicationLaunchOptionsShortcutItemKey]) {

        [self handleActionForType:[(UIApplicationShortcutItem *)launchOptions[UIApplicationLaunchOptionsShortcutItemKey] type]];
        return NO;
    }

    if (launchOptions[UIApplicationLaunchOptionsURLKey]) {

        [self.navigationManager.rootListController setShowOnLoad:NO];
        [self.navigationManager setDeferredLoadURL:launchOptions[UIApplicationLaunchOptionsURLKey]];
        return NO;
    }

    return YES;
}

- (BOOL)application:(UIApplication *)application openURL:(NSURL *)url options:(NSDictionary<UIApplicationOpenURLOptionsKey, id> *)options {

    if ([url.scheme isEqualToString:@"tweaks"]) {

        if (self.navigationManager.rootListController.rootListLoaded) {

            [self.navigationManager processURL:url animated:YES];
        }

        return YES;
    }

    return NO;
}

- (void)application:(UIApplication *)application performActionForShortcutItem:(UIApplicationShortcutItem *)shortcutItem completionHandler:(void (^)(BOOL))completionHandler {

    [self handleActionForType:shortcutItem.type];
    completionHandler(YES);
}

- (BOOL)application:(UIApplication *)application continueUserActivity:(NSUserActivity *)userActivity restorationHandler:(void (^)(NSArray<id<UIUserActivityRestoring>> *))restorationHandler {

    if ([userActivity.activityType isEqualToString:CSSearchableItemActionType]) {
        NSURL *launchURL = [NSURL URLWithString:userActivity.userInfo[CSSearchableItemActivityIdentifier]];
        [self.navigationManager processURL:launchURL animated:NO];
    }

    return YES;
}

#pragma mark - Public Methods

- (void)presentAsPopover:(UIViewController *)controller withSender:(id)sender {

    if (sender == nil) sender = _navigationManager.rootListController.navigationItem.rightBarButtonItem;
    self.popoverSender = sender;

    controller.modalPresentationStyle = UIModalPresentationPopover;

    if (sender && [sender isKindOfClass:UIBarButtonItem.class]) {
        controller.popoverPresentationController.barButtonItem = sender;
    }
    else if (sender && [sender isKindOfClass:UIView.class]) {
        controller.popoverPresentationController.sourceRect = [sender bounds];
        controller.popoverPresentationController.sourceView = sender;
        controller.popoverPresentationController.permittedArrowDirections = (UIPopoverArrowDirection)0;
    }

    [_navigationManager.rootListController presentViewController:controller animated:YES completion:nil];
}

- (void)presentViewController:(UIViewController *)controller {
    [_navigationManager.topNavigationController presentViewController:controller animated:YES completion:nil];
}

- (void)handleActionForType:(NSString *)actionType {

    // Relaxin 0.5.7 does not expose a verified public injection toggle.
    // Explain this instead of pretending that a marker-file change toggles injection.
    if ([actionType isEqualToString:TSActionTypeTweakInject] &&
        access(ROOT_PATH("/basebin/jbctl"), F_OK) == 0) {
        UIAlertController *alert = [UIAlertController
            alertControllerWithTitle:@"插件注入切换暂不可用"
            message:@"Relaxin 0.5.7 目前没有可安全调用的公开插件注入开关接口。本版本不会修改注入状态，避免造成越狱环境异常。"
            preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"知道了" style:UIAlertActionStyleDefault handler:nil]];
        [self presentViewController:alert];
        return;
    }

    if (CanRunWithoutConfirmation(actionType) && !TSUserDefaults.sharedDefaults.requireActionConfirmation) {

        HandleActionForType(actionType);

    } else {

        [self presentAsPopover:ActionAlertForType(actionType) withSender:_popoverSender];
    }
}

- (void)openApplicationURL:(NSURL *)url {

    void (*SBSOpenSensitiveURLAndUnlock)(NSURL *, BOOL);
    if ((SBSOpenSensitiveURLAndUnlock = (void (*)(NSURL *, BOOL)) dlsym(RTLD_DEFAULT, "SBSOpenSensitiveURLAndUnlock"))) {
        (*SBSOpenSensitiveURLAndUnlock)(url, YES);
    } else if (@available(iOS 10,*)) {
        [self openURL:url options:@{} completionHandler:nil];
    }
}

- (void)generateURL {
    NSURL *url = [self.navigationManager urlForCurrentNavStack];
    [NSUserDefaults.standardUserDefaults setObject:url.absoluteString forKey:@"kPreferencePositionKey"];
}

@end
