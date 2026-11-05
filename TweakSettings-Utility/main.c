#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>
#include <string.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

#import "rootless.h"

int proc_pidpath(pid_t pid, void *buffer, uint32_t buffersize);

typedef char *(*jbroot_getter_t)(void);
typedef int (*jb_checkin_t)(char **, char **, char **, bool *);
typedef int (*jb_set_mac_label_t)(uint64_t, uint64_t, uint64_t *);

static char gJailbreakRoot[PATH_MAX];

static void resolve_jailbreak_root(void) {
    if (gJailbreakRoot[0]) return;

    void *systemHook = dlopen("systemhook.dylib", RTLD_LAZY | RTLD_NOLOAD);
    if (systemHook) {
        jbroot_getter_t getter = (jbroot_getter_t)dlsym(systemHook, "get_jbroot");
        const char *root = getter ? getter() : NULL;
        if (root && root[0] == '/') strlcpy(gJailbreakRoot, root, sizeof(gJailbreakRoot));
        dlclose(systemHook);
    }

    if (!gJailbreakRoot[0]) {
        void *libjailbreak = dlopen("/var/jb/usr/lib/libjailbreak.dylib", RTLD_LAZY);
        if (libjailbreak) {
            jbroot_getter_t getter = (jbroot_getter_t)dlsym(libjailbreak, "jbclient_get_jbroot");
            const char *root = getter ? getter() : NULL;
            if (root && root[0] == '/') strlcpy(gJailbreakRoot, root, sizeof(gJailbreakRoot));
            dlclose(libjailbreak);
        }
    }

    if (!gJailbreakRoot[0]) strlcpy(gJailbreakRoot, "/var/jb", sizeof(gJailbreakRoot));
}

static const char *jbroot_path(const char *relativePath) {
    static char path[PATH_MAX];
    resolve_jailbreak_root();
    if (snprintf(path, sizeof(path), "%s%s", gJailbreakRoot, relativePath) >= (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return "";
    }
    return path;
}

static int toggle_relaxin_tweak_injection(void) {
    const char *markerPath = jbroot_path("/basebin/.safe_mode");
    if (!markerPath[0]) return ENAMETOOLONG;

    void *libjailbreak = dlopen(jbroot_path("/usr/lib/libjailbreak.dylib"), RTLD_LAZY);
    if (!libjailbreak) {
        fprintf(stderr, "Unable to load RootHide libjailbreak: %s\n", dlerror());
        return ENOENT;
    }

    jb_checkin_t checkin = (jb_checkin_t)dlsym(libjailbreak, "jbclient_process_checkin");
    jb_set_mac_label_t setMacLabel = (jb_set_mac_label_t)dlsym(libjailbreak, "jbclient_root_set_mac_label");
    if (!checkin || !setMacLabel || checkin(NULL, NULL, NULL, NULL) != 0) {
        dlclose(libjailbreak);
        fprintf(stderr, "RootHide runtime check-in failed.\n");
        return EPERM;
    }

    uint64_t originalLabel = 0;
    int status = setMacLabel(1, UINT64_MAX, &originalLabel);
    if (status != 0) {
        dlclose(libjailbreak);
        fprintf(stderr, "Unable to enter the RootHide unsandboxed context (%d).\n", status);
        return status;
    }

    if (access(markerPath, F_OK) == 0) {
        if (unlink(markerPath) != 0) status = errno;
    } else {
        int fd = open(markerPath, O_WRONLY | O_CREAT, 0644);
        if (fd < 0) status = errno;
        else close(fd);
    }

    int restoreStatus = setMacLabel(1, originalLabel, NULL);
    dlclose(libjailbreak);

    if (status == 0 && restoreStatus != 0) status = restoreStatus;
    return status;
}

typedef enum {
    TSUtilityActionTypeNone,
    TSUtilityActionTypeHelp,
    TSUtilityActionTypeRespring,
    TSUtilityActionTypeSafemode,
    TSUtilityActionTypeUICache,
    TSUtilityActionTypeLDRestart,
    TSUtilityActionTypeReboot,
    TSUtilityActionTypeUSReboot,
    TSUtilityActionTypeTweakinject,
    TSUtilityActionTypeSubstrated
} TSUtilityActionType;

static void print_usage(void) {
    printf("tweaksettings-utility usage:\n\n");
    printf("[--respring]:\n\tRestart SpringBoard\n");
    printf("[--safemode]:\n\tEnter safe mode\n");
    printf("[--uicache]:\n\tRefresh icon cache\n");
    printf("[--ldrestart]:\n\tPerform ldrestart\n");
    printf("[--reboot]:\n\tReboot the device\n");
    printf("[--usreboot]:\n\tRestart userspace through Relaxin jbctl\n");
    printf("[--tweakinject]:\n\tToggle Relaxin tweak injection and restart userspace\n");
    printf("[--help]:\n\tShow this help text\n\n");
}

static bool parent_is_tweaksettings(void) {
    struct stat appStat;
    if (lstat(jbroot_path("/Applications/TweakSettings.app/TweakSettings"), &appStat) != 0) return false;

    char buffer[PATH_MAX] = {0};
    int length = proc_pidpath(getppid(), buffer, sizeof(buffer));
    if (length <= 0) return false;

    const char *expected = "/Applications/TweakSettings.app/TweakSettings";
    size_t actualLength = strlen(buffer);
    size_t expectedLength = strlen(expected);
    return actualLength >= expectedLength &&
           strcmp(buffer + actualLength - expectedLength, expected) == 0;
}

static int elevate_effective_ids(void) {
    // Relaxin's privileged post-jailbreak actions elevate effective IDs,
    // preserving the caller's real UID/GID.
    if (getegid() != 0 && setegid(0) != 0) return errno ?: EPERM;
    if (geteuid() != 0 && seteuid(0) != 0) return errno ?: EPERM;
    if (geteuid() != 0 || getegid() != 0) return EPERM;
    return 0;
}

int main(int argc, char **argv, char **envp) {
    if (!parent_is_tweaksettings()) {
        fprintf(stderr, "This utility can only be launched by TweakSettings.app.\n");
        return EX_NOPERM;
    }

    int privilegeStatus = elevate_effective_ids();
    if (privilegeStatus != 0) {
        fprintf(stderr, "Unable to obtain effective root privileges (%d).\n", privilegeStatus);
        return EX_NOPERM;
    }

    if (argc < 2) {
        print_usage();
        return EX_OK;
    }

    TSUtilityActionType action =
        strcmp(argv[1], "--respring") == 0 ? TSUtilityActionTypeRespring :
        strcmp(argv[1], "--safemode") == 0 ? TSUtilityActionTypeSafemode :
        strcmp(argv[1], "--uicache") == 0 ? TSUtilityActionTypeUICache :
        strcmp(argv[1], "--ldrestart") == 0 ? TSUtilityActionTypeLDRestart :
        strcmp(argv[1], "--reboot") == 0 ? TSUtilityActionTypeReboot :
        strcmp(argv[1], "--usreboot") == 0 ? TSUtilityActionTypeUSReboot :
        strcmp(argv[1], "--tweakinject") == 0 ? TSUtilityActionTypeTweakinject :
        strcmp(argv[1], "--substrated") == 0 ? TSUtilityActionTypeSubstrated :
        strcmp(argv[1], "--help") == 0 ? TSUtilityActionTypeHelp :
        TSUtilityActionTypeNone;

    switch (action) {
        case TSUtilityActionTypeNone:
            fprintf(stderr, "Invalid action argument.\n");
            return EX_USAGE;
        case TSUtilityActionTypeHelp:
            print_usage();
            return EX_OK;
        case TSUtilityActionTypeRespring:
            execl(jbroot_path("/usr/bin/killall"), "killall", "backboardd", NULL);
            break;
        case TSUtilityActionTypeSafemode:
            execl(jbroot_path("/usr/bin/killall"), "killall", "-SEGV", "SpringBoard", NULL);
            break;
        case TSUtilityActionTypeUICache:
            execl(jbroot_path("/usr/bin/uicache"), "uicache", NULL);
            break;
        case TSUtilityActionTypeLDRestart:
            execl(jbroot_path("/usr/bin/ldrestart"), "ldrestart", NULL);
            break;
        case TSUtilityActionTypeReboot:
            execl(jbroot_path("/usr/sbin/reboot"), "reboot", NULL);
            break;
        case TSUtilityActionTypeUSReboot:
            execl(jbroot_path("/basebin/jbctl"), "jbctl", "reboot_userspace", NULL);
            break;
        case TSUtilityActionTypeTweakinject: {
            int status = toggle_relaxin_tweak_injection();
            if (status != 0) {
                fprintf(stderr, "Failed to toggle Relaxin tweak injection (%d).\n", status);
                return EX_UNAVAILABLE;
            }
            execl(jbroot_path("/basebin/jbctl"), "jbctl", "reboot_userspace", NULL);
            break;
        }
        case TSUtilityActionTypeSubstrated:
            execl(jbroot_path("/etc/rc.d/substrate"), "substrate", NULL);
            break;
    }

    int execError = errno ?: EIO;
    fprintf(stderr, "Unable to execute requested action: %s\n", strerror(execError));
    return EX_UNAVAILABLE;
}
