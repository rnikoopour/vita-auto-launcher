#include <vitasdk.h>
#include <taihen.h>
#include <libk/string.h>
#include <libk/stdio.h>

#define CONFIG_PATH    "ux0:/data/VitaAutoLauncher/config.txt"
#define DISABLE_PATH   "ux0:/data/VitaAutoLauncher/disabled"
#define LIVEAREA_BUDDY "NPXS10079"
#define TITLEID_LEN    9
#define MAX_RUNNING    8

#define BTN_SKIP       SCE_CTRL_SELECT
#define BTN_DISABLE    (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER)

typedef enum {
    STATE_INIT,
    STATE_LAUNCHER_ACTIVE,
    STATE_IN_GAME,
} State;

static int    session_disabled = 0;
static char   fg_game[10]      = {0};
static SceUID fg_pid           = -1; // PID of tracked game; -1 when unknown
static SceUID launcher_pid     = -1; // PID of configured launcher; -1 when unknown
static int    livearea_stable  = 0;  // consecutive stable polls (fallback only)

static uint32_t buttons_held(void) {
    SceCtrlData ctrl;
    sceCtrlPeekBufferPositive(0, &ctrl, 1);
    return ctrl.buttons;
}

static int disabled_flag_exists(void) {
    SceUID fd = sceIoOpen(DISABLE_PATH, SCE_O_RDONLY, 0);
    if (fd < 0) return 0;
    sceIoClose(fd);
    return 1;
}

static int read_config(char *dst) {
    SceUID fd = sceIoOpen(CONFIG_PATH, SCE_O_RDONLY, 0);
    if (fd < 0) return 0;
    char buf[16];
    int n = sceIoRead(fd, buf, sizeof(buf) - 1);
    sceIoClose(fd);
    if (n < TITLEID_LEN) return 0;
    memcpy(dst, buf, TITLEID_LEN);
    dst[TITLEID_LEN] = 0;
    return 1;
}

// Returns 1 if fg_game's PID is still alive (process running or suspended).
// Suspended apps retain their PID; terminated apps return an empty name.
static int is_fg_game_alive(void) {
    if (fg_pid < 0 || fg_game[0] == 0) return 0;
    char name[16] = {0};
    sceAppMgrGetNameById(fg_pid, name);
    return (name[0] != 0 && strcmp(name, fg_game) == 0);
}

// Finds the first non-NPXS, non-fg_game app in the full process list.
// Writes its name and PID to the out params.
static void find_other_running(char *name_out, SceUID *pid_out) {
    if (name_out) name_out[0] = 0;
    if (pid_out)  *pid_out = -1;

    SceInt32 appIds[MAX_RUNNING] = {0};
    sceAppMgrGetRunningAppIdListForShell(appIds, MAX_RUNNING);

    for (int i = 0; i < MAX_RUNNING; i++) {
        if (appIds[i] == 0) break;
        SceUID p = sceAppMgrGetProcessIdByAppIdForShell(appIds[i]);
        if (p <= 0) continue;
        char name[16] = {0};
        sceAppMgrGetNameById(p, name);
        if (name[0] == 0) continue;
        if (strncmp(name, "NPXS", 4) == 0) continue;
        if (fg_game[0] && strcmp(name, fg_game) == 0) continue;
        if (name_out && name_out[0] == 0) {
            memcpy(name_out, name, 10);
            if (pid_out) *pid_out = p;
        }
    }
}

// Checks whether a specific app name is anywhere in the full running list.
static int is_app_running(const char *name) {
    SceInt32 appIds[MAX_RUNNING] = {0};
    sceAppMgrGetRunningAppIdListForShell(appIds, MAX_RUNNING);
    for (int i = 0; i < MAX_RUNNING; i++) {
        if (appIds[i] == 0) break;
        SceUID p = sceAppMgrGetProcessIdByAppIdForShell(appIds[i]);
        if (p <= 0) continue;
        char n[16] = {0};
        sceAppMgrGetNameById(p, n);
        if (strcmp(n, name) == 0) return 1;
    }
    return 0;
}

static void launch_titleid(const char *titleid) {
    char uri[32];
    sprintf(uri, "psgm:play?titleid=%s", titleid);
    for (int i = 0; i < 50; i++) {
        if (sceAppMgrLaunchAppByUri(0xFFFFF, uri) == 0)
            break;
        sceKernelDelayThread(100000);
    }
}

static int maybe_redirect(void) {
    if (session_disabled) return 0;
    uint32_t btns = buttons_held();
    if ((btns & BTN_DISABLE) == BTN_DISABLE) { session_disabled = 1; return 0; }
    if (btns & BTN_SKIP) return 0;
    if (disabled_flag_exists()) return 0;
    char titleid[10];
    if (!read_config(titleid)) return 0;
    launch_titleid(titleid);
    return 1;
}

static int launcher_thread(SceSize args, void *argp) {
    State state = STATE_INIT;
    SceInt32 appId = 0;
    char appName[16];
    SceUID pid;

    while (1) {
        sceKernelDelayThread(200000);

        appId = 0;
        sceAppMgrGetRunningAppIdListForShell(&appId, 1);

        if (appId == 0) {
            if (state == STATE_INIT)
                continue;
            strcpy(appName, LIVEAREA_BUDDY);
            pid = -1;
        } else {
            pid = sceAppMgrGetProcessIdByAppIdForShell(appId);
            if (pid <= 0)
                continue;
            memset(appName, 0, sizeof(appName));
            sceAppMgrGetNameById(pid, appName);
        }

        switch (state) {
        case STATE_INIT:
            if (strcmp(appName, LIVEAREA_BUDDY) == 0) {
                maybe_redirect();
                state = STATE_LAUNCHER_ACTIVE;
            }
            break;

        case STATE_LAUNCHER_ACTIVE:
            if (strcmp(appName, LIVEAREA_BUDDY) != 0 && strncmp(appName, "NPXS", 4) != 0) {
                // While the launcher is in the foreground, record its PID so we
                // can detect it later as a suspended process after a game exits.
                if (launcher_pid < 0) {
                    char configured[10] = {0};
                    if (read_config(configured) && strcmp(appName, configured) == 0)
                        launcher_pid = pid;
                }
                if (is_app_running(appName)) {
                    memcpy(fg_game, appName, sizeof(fg_game));
                    fg_pid = pid;
                    state = STATE_IN_GAME;
                }
            }
            break;

        case STATE_IN_GAME: {
            if (strcmp(appName, fg_game) == 0) {
                // fg_game is still the foreground app — reset stability counter.
                livearea_stable = 0;
                break;
            }
            if (strcmp(appName, LIVEAREA_BUDDY) == 0) {
                if (is_fg_game_alive()) {
                    // fg_game's process is still alive — the user pressed Home
                    // but hasn't closed the app. Don't redirect.
                    livearea_stable = 0;
                    break;
                }

                // fg_game's process is gone.
                char configured[10] = {0};
                int have_config = read_config(configured);

                // Was the exited app the configured launcher itself?
                // User intentionally quit it — return to LAUNCHER_ACTIVE, no redirect.
                if (have_config && strcmp(fg_game, configured) == 0) {
                    livearea_stable = 0;
                    launcher_pid = -1;
                    state = STATE_LAUNCHER_ACTIVE;
                    fg_game[0] = 0;
                    fg_pid = -1;
                    break;
                }

                // Is the configured launcher still alive (suspended)?
                // Suspended processes retain their PID and return a name via
                // GetNameById. If it's alive it will resume — no redirect needed.
                if (launcher_pid >= 0) {
                    char lname[16] = {0};
                    sceAppMgrGetNameById(launcher_pid, lname);
                    if (have_config && lname[0] != 0 && strcmp(lname, configured) == 0) {
                        livearea_stable = 0;
                        state = STATE_LAUNCHER_ACTIVE;
                        fg_game[0] = 0;
                        fg_pid = -1;
                        break;
                    }
                    launcher_pid = -1; // stale — launcher is gone
                }

                // Check if another app is already in the process list
                // (e.g. the launcher immediately started the next game).
                char other[10] = {0};
                SceUID other_pid = -1;
                find_other_running(other, &other_pid);

                if (other[0] != 0) {
                    livearea_stable = 0;
                    memcpy(fg_game, other, sizeof(fg_game));
                    fg_pid = other_pid;
                } else {
                    // Fallback: wait briefly before redirecting in case a launch
                    // is in flight but not yet visible in the process list.
                    livearea_stable++;
                    if (livearea_stable >= 5) { // 5 × 200ms = 1 second
                        livearea_stable = 0;
                        maybe_redirect();
                        state = STATE_LAUNCHER_ACTIVE;
                        fg_game[0] = 0;
                        fg_pid = -1;
                    }
                }
            } else if (strncmp(appName, "NPXS", 4) != 0) {
                // A different non-system app is now foreground — track it.
                livearea_stable = 0;
                memcpy(fg_game, appName, sizeof(fg_game));
                fg_pid = pid;
            }
            break;
        }
        }
    }

    return 0;
}

void _start() __attribute__((weak, alias("module_start")));

int module_start(SceSize argc, const void *args) {
    SceUID thid = sceKernelCreateThread("val_thread", launcher_thread, 0x40, 0x40000, 0, 0, NULL);
    if (thid >= 0)
        sceKernelStartThread(thid, 0, NULL);
    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) {
    return SCE_KERNEL_STOP_SUCCESS;
}
