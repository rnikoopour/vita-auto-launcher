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

static int  session_disabled = 0;
static char fg_game[10]      = {0};

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

// Scans the full running-app list (count=8 returns all, not just foreground).
// Returns 1 if fg_game is present.
// Sets other_out to the first non-NPXS app found that is NOT fg_game.
static int scan_running_apps(char *other_out) {
    if (other_out) other_out[0] = 0;

    SceInt32 appIds[MAX_RUNNING] = {0};
    sceAppMgrGetRunningAppIdListForShell(appIds, MAX_RUNNING);

    int fg_found = 0;
    for (int i = 0; i < MAX_RUNNING; i++) {
        if (appIds[i] == 0) break;
        SceUID pid = sceAppMgrGetProcessIdByAppIdForShell(appIds[i]);
        if (pid <= 0) continue;
        char name[16] = {0};
        sceAppMgrGetNameById(pid, name);

        if (fg_game[0] && strcmp(name, fg_game) == 0) {
            fg_found = 1;
        } else if (strncmp(name, "NPXS", 4) != 0 && other_out && other_out[0] == 0) {
            memcpy(other_out, name, 10);
        }
    }
    return fg_found;
}

// Checks whether a specific app name is anywhere in the full running list.
static int is_app_running(const char *name) {
    SceInt32 appIds[MAX_RUNNING] = {0};
    sceAppMgrGetRunningAppIdListForShell(appIds, MAX_RUNNING);
    for (int i = 0; i < MAX_RUNNING; i++) {
        if (appIds[i] == 0) break;
        SceUID pid = sceAppMgrGetProcessIdByAppIdForShell(appIds[i]);
        if (pid <= 0) continue;
        char n[16] = {0};
        sceAppMgrGetNameById(pid, n);
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
                if (is_app_running(appName)) {
                    memcpy(fg_game, appName, sizeof(fg_game));
                    state = STATE_IN_GAME;
                }
            }
            break;

        case STATE_IN_GAME: {
            if (strcmp(appName, fg_game) == 0) {
                // fg_game is still the foreground app — nothing to do.
                break;
            }
            if (strcmp(appName, LIVEAREA_BUDDY) == 0) {
                // LiveArea is foreground. Use the full process list to check
                // whether fg_game actually exited or is just suspended.
                char other[10] = {0};
                int fg_running = scan_running_apps(other);

                if (!fg_running) {
                    if (other[0] != 0) {
                        // fg_game exited but another app launched (e.g. a game
                        // launched by the custom launcher). Track the new app.
                        memcpy(fg_game, other, sizeof(fg_game));
                    } else {
                        // If the app that just exited IS the configured launcher,
                        // the user pressed Home or quit it intentionally — don't
                        // redirect, just return to LiveArea.
                        char configured[10] = {0};
                        if (read_config(configured) && strcmp(fg_game, configured) == 0) {
                            state = STATE_LAUNCHER_ACTIVE;
                            fg_game[0] = 0;
                        } else {
                            // A game exited. Poll up to 5 seconds for a newly-launched
                            // app to appear before deciding to redirect.
                            int found_new = 0;
                            for (int i = 0; i < 10 && !found_new; i++) {
                                sceKernelDelayThread(500000);
                                other[0] = 0;
                                fg_running = scan_running_apps(other);
                                if (fg_running || other[0] != 0)
                                    found_new = 1;
                            }

                            if (found_new && !fg_running && other[0] != 0) {
                                memcpy(fg_game, other, sizeof(fg_game));
                            } else if (!found_new) {
                                maybe_redirect();
                                state = STATE_LAUNCHER_ACTIVE;
                                fg_game[0] = 0;
                            }
                            // fg_running true: fg_game came back — stay in STATE_IN_GAME
                        }
                    }
                }
                // fg_running true here: fg_game suspended (home pressed) — stay put
            } else if (strncmp(appName, "NPXS", 4) != 0) {
                // A different non-system app is now foreground. Track it.
                memcpy(fg_game, appName, sizeof(fg_game));
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
