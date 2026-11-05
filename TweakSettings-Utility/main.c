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
#include <spawn.h>
#include <signal.h>

#import "rootless.h"

extern char **environ;

int proc_pidpath(pid_t pid, void *buffer, uint32_t buffersize);

extern char **environ;

static char gJailbreakRoot[PATH_MAX];
static void *gLibjailbreak;
typedef char *(*jbroot_getter_t)(void);
typedef bool (*roothide_check_t)(void);
typedef int (*set_mac_label_t)(uint64_t, uint64_t, uint64_t *);

static void resolve_jailbreak_root(void) {
    if (gJailbreakRoot[0]) return;
    gLibjailbreak = dlopen("/var/jb/usr/lib/libjailbreak.dylib", RTLD_LAZY);
    if (gLibjailbreak) {
        jbroot_getter_t getter = (jbroot_getter_t)dlsym(gLibjailbreak, "jbclient_get_jbroot");
        const char *root = getter ? getter() : NULL;
        if (root && root[0] == '/') strlcpy(gJailbreakRoot, root, sizeof(gJailbreakRoot));
    }
    if (!gJailbreakRoot[0]) strlcpy(gJailbreakRoot, "/var/jb", sizeof(gJailbreakRoot));
}
static const char *jbroot_path(const char *relativePath) {
    static char paths[8][PATH_MAX];
    static unsigned int nextPath = 0;
    char *path = paths[nextPath++ % 8];
    resolve_jailbreak_root();
    if (snprintf(path, PATH_MAX, "%s%s", gJailbreakRoot, relativePath) >= PATH_MAX) {
        errno = ENAMETOOLONG;
        return "";
    }
    return path;
}
static int run_unsandboxed_begin(uint64_t *originalLabel, bool *changedLabel) {
    *changedLabel = false;
    if (!gLibjailbreak) resolve_jailbreak_root();
    if (!gLibjailbreak) return ENOENT;
    roothide_check_t isRootHide = (roothide_check_t)dlsym(gLibjailbreak, "jbclient_roothide_jailbroken");
    set_mac_label_t setMacLabel = (set_mac_label_t)dlsym(gLibjailbreak, "jbclient_root_set_mac_label");
    if (!isRootHide || !setMacLabel || !isRootHide()) return 0;
    int status = setMacLabel(1, UINT64_MAX, originalLabel);
    if (status == 0) *changedLabel = true;
    return status;
}
static int run_unsandboxed_end(uint64_t originalLabel, bool changedLabel) {
    if (!changedLabel) return 0;
    set_mac_label_t setMacLabel = (set_mac_label_t)dlsym(gLibjailbreak, "jbclient_root_set_mac_label");
    return setMacLabel ? setMacLabel(1, originalLabel, NULL) : ENOENT;
}
static int spawn_suspended_and_resume(const char *path, char *const argv[]) {
    posix_spawnattr_t attr;
    int status = posix_spawnattr_init(&attr);
    if (status != 0) return status;
    status = posix_spawnattr_setflags(&attr, POSIX_SPAWN_START_SUSPENDED);
    pid_t pid = -1;
    if (status == 0) status = posix_spawn(&pid, path, NULL, &attr, argv, environ);
    posix_spawnattr_destroy(&attr);
    if (status != 0) return status;
    return kill(pid, SIGCONT) == 0 ? 0 : (errno ? errno : EIO);
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
    printf("[--tweakinject]:\n\tNot available on Relaxin 0.5.7 (no public toggle API)\n");
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
        case TSUtilityActionTypeRespring: {
            uint64_t oldLabel = 0; bool changed = false;
            int scope = run_unsandboxed_begin(&oldLabel, &changed);
            if (scope != 0) return EX_NOPERM;
            execl(jbroot_path("/usr/bin/sbreload"), "sbreload", NULL);
            int err = errno ? errno : EIO;
            run_unsandboxed_end(oldLabel, changed);
            errno = err;
            break;
        }
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
        case TSUtilityActionTypeUSReboot: {
            uint64_t oldLabel = 0; bool changed = false;
            int scope = run_unsandboxed_begin(&oldLabel, &changed);
            if (scope != 0) return EX_NOPERM;
            char *args[] = { "jbctl", "reboot_userspace", NULL };
            int spawnStatus = spawn_suspended_and_resume(jbroot_path("/basebin/jbctl"), args);
            int restoreStatus = run_unsandboxed_end(oldLabel, changed);
            if (spawnStatus != 0 || restoreStatus != 0) {
                fprintf(stderr, "Relaxin userspace restart failed (%d / %d).\\n", spawnStatus, restoreStatus);
                return EX_UNAVAILABLE;
            }
            return EX_OK;
        }
        case TSUtilityActionTypeTweakinject:
            fprintf(stderr, "No verified public Relaxin tweak-injection toggle API is available.\\n");
            return EX_UNAVAILABLE;
        case TSUtilityActionTypeSubstrated:
            execl(jbroot_path("/etc/rc.d/substrate"), "substrate", NULL);
            break;
    }

    int execError = errno ?: EIO;
    fprintf(stderr, "Unable to execute requested action: %s\n", strerror(execError));
    return EX_UNAVAILABLE;
}
