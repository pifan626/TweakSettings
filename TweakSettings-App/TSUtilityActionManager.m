//
//  TSActionType.m
//  TweakSettings
//
//  Created by Dana Buehre on 5/30/21.
//
//

#import <UIKit/UIKit.h>
#import "TSUtilityActionManager.h"
#import "Localizable.h"
#import "TSAppDelegate.h"
#import "rootless.h"
#include <spawn.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

NSString *const TSActionTypeRespring = @"respring";
NSString *const TSActionTypeSafemode = @"safemode";
NSString *const TSActionTypeUICache = @"uicache";
NSString *const TSActionTypeLDRestart = @"ldrestart";
NSString *const TSActionTypeReboot = @"reboot";
NSString *const TSActionTypeUserspaceReboot = @"usreboot";
NSString *const TSActionTypeTweakInject = @"tweakinject";

static NSString *TweakInjectNextActionTitle(void);

struct TaskResult ExecuteCommand(NSString *command) {
    struct TaskResult result = { .status = EXIT_FAILURE, .output = nil, .error = nil };

    // Launch the privileged utility directly. Do not use /bin/sh:
    // the utility validates that its immediate parent is TweakSettings.app.
    // A shell intermediary causes every action to be rejected.
    NSString *utilityPath = ROOT_PATH_NS(@"/usr/bin/tweaksettings-utility");
    NSString *actionArgument = command.lastPathComponent;
    if (![actionArgument hasPrefix:@"--"] || access(utilityPath.fileSystemRepresentation, X_OK) != 0) {
        result.error = [NSString stringWithFormat:@"Utility unavailable or invalid action: %@", utilityPath];
        NSLog(@"TweakSettings: %@", result.error);
        return result;
    }

    pid_t child = 0;
    char * const arguments[] = {
        (char *)utilityPath.fileSystemRepresentation,
        (char *)actionArgument.UTF8String,
        NULL
    };

    extern char **environ;
    int spawnError = posix_spawn(&child, utilityPath.fileSystemRepresentation, NULL, NULL, arguments, environ);
    if (spawnError != 0) {
        result.status = spawnError;
        result.error = [NSString stringWithUTF8String:strerror(spawnError)];
        NSLog(@"TweakSettings: failed to launch utility: %@", result.error);
        return result;
    }

    int waitStatus = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &waitStatus, 0);
    } while (waited == -1 && errno == EINTR);

    if (waited == -1) {
        result.status = errno;
        result.error = [NSString stringWithUTF8String:strerror(errno)];
        NSLog(@"TweakSettings: waitpid failed: %@", result.error);
        return result;
    }

    if (WIFEXITED(waitStatus)) {
        result.status = WEXITSTATUS(waitStatus);
    } else if (WIFSIGNALED(waitStatus)) {
        result.status = 128 + WTERMSIG(waitStatus);
    }

    if (result.status != 0) {
        NSLog(@"TweakSettings: utility action %@ failed with status %d", actionArgument, result.status);
    }

    return result;
}
NSString *TitleForActionType(NSString *type) {
    if ([type isEqualToString:TSActionTypeRespring]) return NSLocalizedString(RESPRING_TITLE_KEY, nil);
    if ([type isEqualToString:TSActionTypeSafemode]) return NSLocalizedString(SAFEMODE_TITLE_KEY, nil);
    if ([type isEqualToString:TSActionTypeUICache]) return NSLocalizedString(UICACHE_TITLE_KEY, nil);
    if ([type isEqualToString:TSActionTypeLDRestart]) return NSLocalizedString(LDRESTART_TITLE_KEY, nil);
    if ([type isEqualToString:TSActionTypeReboot]) return NSLocalizedString(REBOOT_TITLE_KEY, nil);
    if ([type isEqualToString:TSActionTypeUserspaceReboot]) return NSLocalizedString(USREBOOT_TITLE_KEY, nil);
    if ([type isEqualToString:TSActionTypeTweakInject]) return TweakInjectNextActionTitle();
    return nil;
}

// Returns 1 when injection is enabled, 0 when disabled, and -1 when the state cannot be detected.
static NSInteger TweakInjectEnabledState(void) {
    if (access("/var/jb/.installed_dopamine", F_OK) == 0) {
        return access("/var/jb/basebin/.safe_mode", F_OK) == 0 ? 0 : 1;
    }

    if (access(ROOT_PATH("/usr/lib/TweakInject.dylib"), F_OK) == 0) {
        return access(ROOT_PATH("/.disable_tweakinject"), F_OK) == 0 ? 0 : 1;
    }

    if (access(ROOT_PATH("/etc/rc.d/substrate"), F_OK) == 0) {
        return access(ROOT_PATH("/var/tmp/.substrated_disable_loader"), F_OK) == 0 ? 0 : 1;
    }

    return -1;
}

static NSString *TweakInjectCurrentStateText(void) {
    NSInteger state = TweakInjectEnabledState();
    if (state == 1) return NSLocalizedString(TWEAKINJECT_STATE_ENABLED_KEY, nil);
    if (state == 0) return NSLocalizedString(TWEAKINJECT_STATE_DISABLED_KEY, nil);
    return NSLocalizedString(TWEAKINJECT_STATE_UNKNOWN_KEY, nil);
}

static NSString *TweakInjectNextActionTitle(void) {
    NSInteger state = TweakInjectEnabledState();
    if (state == 1) return NSLocalizedString(TWEAKINJECT_ACTION_DISABLE_KEY, nil);
    if (state == 0) return NSLocalizedString(TWEAKINJECT_ACTION_ENABLE_KEY, nil);
    return NSLocalizedString(TWEAKINJECT_TITLE_KEY, nil);
}

NSString *SubtitleForActionType(NSString *type) {
    if ([type isEqualToString:TSActionTypeRespring]) return NSLocalizedString(RESPRING_SUBTITLE_KEY, nil);
    if ([type isEqualToString:TSActionTypeSafemode]) return NSLocalizedString(SAFEMODE_SUBTITLE_KEY, nil);
    if ([type isEqualToString:TSActionTypeUICache]) return NSLocalizedString(UICACHE_SUBTITLE_KEY, nil);
    if ([type isEqualToString:TSActionTypeLDRestart]) return NSLocalizedString(LDRESTART_SUBTITLE_KEY, nil);
    if ([type isEqualToString:TSActionTypeReboot]) return NSLocalizedString(REBOOT_SUBTITLE_KEY, nil);
    if ([type isEqualToString:TSActionTypeUserspaceReboot]) return NSLocalizedString(USREBOOT_SUBTITLE_KEY, nil);
    if ([type isEqualToString:TSActionTypeTweakInject]) return NSLocalizedString(TWEAKINJECT_SUBTITLE_KEY, nil);
    return nil;
}

BOOL CanRunWithoutConfirmation(NSString *actionType) {

    if (!actionType || !actionType.length) return NO;
    return !([actionType isEqualToString:TSActionTypeReboot]
        || [actionType isEqualToString:TSActionTypeLDRestart]
        || [actionType isEqualToString:TSActionTypeUserspaceReboot]);
};

int HandleActionForType(NSString *actionType) {

    if (!actionType || !actionType.length) return EXIT_FAILURE;

    int status = ExecuteCommand(CommandForActionType(actionType)).status;

    return status;
}

UIAlertController *ActionAlertForType(NSString *actionType) {

    NSString *title = TitleForActionType(actionType);
    NSString *message;
    if ([actionType isEqualToString:TSActionTypeTweakInject]) {
        message = [NSString stringWithFormat:NSLocalizedString(TWEAKINJECT_CONFIRM_MESSAGE_KEY, nil),
                   TweakInjectCurrentStateText(), title];
    } else {
        message = [NSString stringWithFormat:NSLocalizedString(ALERT_ACTION_MESSAGE_KEY, nil), SubtitleForActionType(actionType)];
    }
    UIAlertController *controller = [UIAlertController alertControllerWithTitle:title message:message preferredStyle:UIAlertControllerStyleAlert];

    [controller addAction:[UIAlertAction actionWithTitle:title style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        HandleActionForType(actionType);
    }]];
    [controller addAction:[UIAlertAction actionWithTitle:NSLocalizedString(ALERT_CANCEL_TITLE_KEY, nil) style:UIAlertActionStyleCancel handler:nil]];

    return controller;
}

UIAlertController *ActionListAlert(void) {

    BOOL userspace_supported = access(ROOT_PATH("/odyssey/jailbreakd.plist"), F_OK) == 0 || access(ROOT_PATH("/taurine/jailbreakd.plist"), F_OK) == 0;
    UIAlertController *controller = [UIAlertController alertControllerWithTitle:nil message:nil preferredStyle:UIAlertControllerStyleActionSheet];

    [controller addAction:[UIAlertAction actionWithTitle:NSLocalizedString(RESPRING_TITLE_KEY, nil) style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        [APP_DELEGATE handleActionForType:TSActionTypeRespring];
    }]];
    [controller addAction:[UIAlertAction actionWithTitle:NSLocalizedString(SAFEMODE_TITLE_KEY, nil) style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        [APP_DELEGATE handleActionForType:TSActionTypeSafemode];
    }]];
    [controller addAction:[UIAlertAction actionWithTitle:NSLocalizedString(UICACHE_TITLE_KEY, nil) style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        [APP_DELEGATE handleActionForType:TSActionTypeUICache];
    }]];
    [controller addAction:[UIAlertAction actionWithTitle:NSLocalizedString(LDRESTART_TITLE_KEY, nil) style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        [APP_DELEGATE handleActionForType:TSActionTypeLDRestart];
    }]];
    [controller addAction:[UIAlertAction actionWithTitle:NSLocalizedString(REBOOT_TITLE_KEY, nil) style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        [APP_DELEGATE handleActionForType:TSActionTypeReboot];
    }]];
    if (userspace_supported) {
        [controller addAction:[UIAlertAction actionWithTitle:NSLocalizedString(USREBOOT_TITLE_KEY, nil) style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
            [APP_DELEGATE handleActionForType:TSActionTypeUserspaceReboot];
        }]];
    }
    [controller addAction:[UIAlertAction actionWithTitle:TweakInjectNextActionTitle() style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        [APP_DELEGATE handleActionForType:TSActionTypeTweakInject];
    }]];
    [controller addAction:[UIAlertAction actionWithTitle:NSLocalizedString(ALERT_CANCEL_TITLE_KEY, nil) style:UIAlertActionStyleCancel handler:nil]];

    return controller;
}

UIMenu *ActionListMenu(void) API_AVAILABLE(ios(13.0)) {

    NSMutableArray *menuActions = [NSMutableArray new];
    BOOL userspace_supported =
            access(ROOT_PATH("/odyssey/jailbreakd.plist"), F_OK) == 0 ||
            access(ROOT_PATH("/taurine/jailbreakd.plist"), F_OK) == 0 ||
            access(ROOT_PATH("/.installed_dopamine"), F_OK) == 0;

    [menuActions addObject:[UIAction actionWithTitle:NSLocalizedString(RESPRING_TITLE_KEY, nil) image:nil identifier:nil handler:^(__kindof UIAction *action) {
        [APP_DELEGATE handleActionForType:TSActionTypeRespring];
    }]];
    [menuActions addObject:[UIAction actionWithTitle:NSLocalizedString(SAFEMODE_TITLE_KEY, nil) image:nil identifier:nil handler:^(__kindof UIAction *action) {
        [APP_DELEGATE handleActionForType:TSActionTypeSafemode];
    }]];
    [menuActions addObject:[UIAction actionWithTitle:NSLocalizedString(UICACHE_TITLE_KEY, nil) image:nil identifier:nil handler:^(__kindof UIAction *action) {
        [APP_DELEGATE handleActionForType:TSActionTypeUICache];
    }]];
    [menuActions addObject:[UIAction actionWithTitle:NSLocalizedString(LDRESTART_TITLE_KEY, nil) image:nil identifier:nil handler:^(__kindof UIAction *action) {
        [APP_DELEGATE handleActionForType:TSActionTypeLDRestart];
    }]];
    [menuActions addObject:[UIAction actionWithTitle:NSLocalizedString(REBOOT_TITLE_KEY, nil) image:nil identifier:nil handler:^(__kindof UIAction *action) {
        [APP_DELEGATE handleActionForType:TSActionTypeReboot];
    }]];
    if (userspace_supported) {
        [menuActions addObject:[UIAction actionWithTitle:NSLocalizedString(USREBOOT_TITLE_KEY, nil) image:nil identifier:nil handler:^(__kindof UIAction *action) {
            [APP_DELEGATE handleActionForType:TSActionTypeUserspaceReboot];
        }]];
    }
    [menuActions addObject:[UIAction actionWithTitle:TweakInjectNextActionTitle() image:nil identifier:nil handler:^(__kindof UIAction *action) {
        [APP_DELEGATE handleActionForType:TSActionTypeTweakInject];
    }]];

    return [UIMenu menuWithTitle:@"" children:menuActions];
}

NSString *CommandForActionType(NSString *actionType) {
    return [NSString stringWithFormat:ROOT_PATH_NS(@"/usr/bin/tweaksettings-utility --%@"), actionType];
}