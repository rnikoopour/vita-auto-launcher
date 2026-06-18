#include <vitasdk.h>
#include <vita2d.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CONFIG_PATH   "ux0:/data/VitaAutoLauncher/config.txt"
#define DISABLE_PATH  "ux0:/data/VitaAutoLauncher/disabled"
#define DATA_DIR      "ux0:/data/VitaAutoLauncher"
#define APPS_DIR      "ux0:/app"
#define OWN_TITLEID   "VALC00001"
#define TITLEID_LEN   9
#define MAX_APPS      128

// Screen layout
#define SCREEN_W      960
#define SCREEN_H      544
#define HEADER_H      80
#define CATBAR_H      36
#define FOOTER_H      30
#define ITEM_H        46
#define LIST_Y        (HEADER_H + CATBAR_H)
#define ITEMS_VISIBLE ((SCREEN_H - LIST_Y - ITEM_H - FOOTER_H) / ITEM_H)

// Colors
#define COL_BG         RGBA8(0x1A, 0x1A, 0x2E, 0xFF)
#define COL_HEADER     RGBA8(0x16, 0x21, 0x3E, 0xFF)
#define COL_HEADER_DIS RGBA8(0x3E, 0x16, 0x16, 0xFF)
#define COL_CATBAR     RGBA8(0x10, 0x18, 0x30, 0xFF)
#define COL_CAT_SEL    RGBA8(0x0F, 0x3A, 0x7F, 0xFF)
#define COL_SEL        RGBA8(0x0F, 0x3A, 0x7F, 0xFF)
#define COL_WHITE      RGBA8(0xFF, 0xFF, 0xFF, 0xFF)
#define COL_GRAY       RGBA8(0xAA, 0xAA, 0xAA, 0xFF)
#define COL_DIM        RGBA8(0x66, 0x66, 0x66, 0xFF)
#define COL_RED        RGBA8(0xFF, 0x44, 0x44, 0xFF)
#define COL_GREEN      RGBA8(0x44, 0xFF, 0x88, 0xFF)

typedef enum { CAT_ALL, CAT_GAMES, CAT_HOMEBREW, CAT_COUNT } Category;
static const char *CAT_NAMES[] = { "All", "Games", "Homebrew" };

typedef struct {
    char titleid[10];
    char title[128];
} AppEntry;

static AppEntry apps[MAX_APPS];
static int      app_count   = 0;

static int      filtered[MAX_APPS]; // indices into apps[]
static int      filtered_count = 0;

static Category current_cat     = CAT_ALL;
static int      selected        = -1;
static int      scroll_off      = 0;
static char     current_titleid[10] = {0};
static int      plugin_disabled     = 0;

// ---- Category helpers ----

static int is_game(const char *titleid) {
    return strncmp(titleid, "PCS", 3) == 0 || strncmp(titleid, "NP", 2) == 0;
}

static void rebuild_filter(void) {
    filtered_count = 0;
    for (int i = 0; i < app_count; i++) {
        int game = is_game(apps[i].titleid);
        if (current_cat == CAT_ALL ||
            (current_cat == CAT_GAMES    &&  game) ||
            (current_cat == CAT_HOMEBREW && !game)) {
            filtered[filtered_count++] = i;
        }
    }

    // Try to keep current_titleid selected across category switches
    selected = -1;
    scroll_off = 0;
    for (int i = 0; i < filtered_count; i++) {
        if (strcmp(apps[filtered[i]].titleid, current_titleid) == 0) {
            selected = i;
            break;
        }
    }
}

// ---- SFO parsing ----

#define SFO_MAGIC   0x46535000
#define SFO_FMT_STR 0x0204

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t key_table_off;
    uint32_t data_table_off;
    uint32_t entry_count;
} SfoHeader;

typedef struct __attribute__((packed)) {
    uint16_t key_off;
    uint16_t data_fmt;
    uint32_t data_len;
    uint32_t data_max_len;
    uint32_t data_off;
} SfoEntry;

static int sfo_read_string(const char *path, const char *key, char *dst, int dst_size) {
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0)
        return 0;

    SfoHeader hdr;
    if (sceIoRead(fd, &hdr, sizeof(hdr)) != sizeof(hdr) || hdr.magic != SFO_MAGIC) {
        sceIoClose(fd);
        return 0;
    }

    int count = hdr.entry_count < 64 ? (int)hdr.entry_count : 64;
    SfoEntry entries[64];
    sceIoRead(fd, entries, count * sizeof(SfoEntry));

    int key_table_size = hdr.data_table_off - hdr.key_table_off;
    char *key_table = malloc(key_table_size);
    if (!key_table) { sceIoClose(fd); return 0; }

    sceIoLseek(fd, hdr.key_table_off, SCE_SEEK_SET);
    sceIoRead(fd, key_table, key_table_size);

    int found = 0;
    for (int i = 0; i < count; i++) {
        if (entries[i].data_fmt != SFO_FMT_STR)
            continue;
        if (strcmp(key_table + entries[i].key_off, key) != 0)
            continue;
        sceIoLseek(fd, hdr.data_table_off + entries[i].data_off, SCE_SEEK_SET);
        int len = (int)entries[i].data_len < dst_size - 1 ? (int)entries[i].data_len : dst_size - 1;
        sceIoRead(fd, dst, len);
        dst[len] = 0;
        found = 1;
        break;
    }

    free(key_table);
    sceIoClose(fd);
    return found;
}

// ---- App enumeration ----

static void load_apps(void) {
    SceUID dfd = sceIoDopen(APPS_DIR);
    if (dfd < 0)
        return;

    SceIoDirent entry;
    app_count = 0;

    while (sceIoDread(dfd, &entry) > 0 && app_count < MAX_APPS) {
        const char *name = entry.d_name;
        if (name[0] == '.')
            continue;
        if (strncmp(name, "NPXS", 4) == 0)
            continue;
        if (strcmp(name, OWN_TITLEID) == 0)
            continue;
        if (strlen(name) != TITLEID_LEN)
            continue;

        strncpy(apps[app_count].titleid, name, TITLEID_LEN);
        apps[app_count].titleid[TITLEID_LEN] = 0;

        char sfo_path[256];
        snprintf(sfo_path, sizeof(sfo_path), "%s/%s/sce_sys/param.sfo", APPS_DIR, name);
        if (!sfo_read_string(sfo_path, "TITLE", apps[app_count].title, sizeof(apps[app_count].title)))
            snprintf(apps[app_count].title, sizeof(apps[app_count].title), "%s", name);

        app_count++;
    }

    sceIoDclose(dfd);
}

// ---- Config / disable I/O ----

static void ensure_data_dir(void) {
    sceIoMkdir(DATA_DIR, 0777);
}

static void load_state(void) {
    SceUID fd = sceIoOpen(CONFIG_PATH, SCE_O_RDONLY, 0);
    if (fd >= 0) {
        int n = sceIoRead(fd, current_titleid, TITLEID_LEN);
        sceIoClose(fd);
        current_titleid[n >= TITLEID_LEN ? TITLEID_LEN : n] = 0;
        if (n < TITLEID_LEN)
            current_titleid[0] = 0;
    }

    fd = sceIoOpen(DISABLE_PATH, SCE_O_RDONLY, 0);
    if (fd >= 0) {
        sceIoClose(fd);
        plugin_disabled = 1;
    }
}

static void save_config(const char *titleid) {
    ensure_data_dir();
    SceUID fd = sceIoOpen(CONFIG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0)
        return;
    sceIoWrite(fd, titleid, strlen(titleid));
    sceIoClose(fd);
}

static void clear_config(void) {
    sceIoRemove(CONFIG_PATH);
}

static void set_disabled(int disabled) {
    ensure_data_dir();
    if (disabled) {
        SceUID fd = sceIoOpen(DISABLE_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        if (fd >= 0) sceIoClose(fd);
    } else {
        sceIoRemove(DISABLE_PATH);
    }
    plugin_disabled = disabled;
}

// ---- Rendering helpers ----

static void draw_item(vita2d_pgf *pgf, int app_idx, int y, int is_selected) {
    if (is_selected)
        vita2d_draw_rectangle(0, y - ITEM_H + 8, SCREEN_W, ITEM_H, COL_SEL);

    unsigned int col = is_selected ? COL_WHITE : COL_GRAY;

    if (app_idx == -1) {
        vita2d_pgf_draw_text(pgf, 24, y, col, 1.0f, "Default (LiveArea)");
    } else {
        char line[160];
        snprintf(line, sizeof(line), "%s  %s", apps[app_idx].titleid, apps[app_idx].title);
        vita2d_pgf_draw_text(pgf, 24, y, col, 0.9f, line);
    }
}

static void draw_catbar(vita2d_pgf *pgf) {
    vita2d_draw_rectangle(0, HEADER_H, SCREEN_W, CATBAR_H, COL_CATBAR);

    int tab_w = SCREEN_W / CAT_COUNT;
    // Text baseline sits near the bottom of the catbar so glyphs render
    // downward, staying inside the catbar and away from the header above.
    int text_y = HEADER_H + CATBAR_H - 6;
    for (int i = 0; i < CAT_COUNT; i++) {
        if (i == (int)current_cat)
            vita2d_draw_rectangle(i * tab_w, HEADER_H, tab_w, CATBAR_H, COL_CAT_SEL);
        unsigned int col = (i == (int)current_cat) ? COL_WHITE : COL_DIM;
        vita2d_pgf_draw_text(pgf, i * tab_w + tab_w / 2 - 30, text_y, col, 1.0f, CAT_NAMES[i]);
    }
}

// ---- Main ----

int main(void) {
    vita2d_init();
    vita2d_set_clear_color(COL_BG);

    vita2d_pgf *pgf = vita2d_load_default_pgf();

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    load_state();
    load_apps();
    rebuild_filter();

    SceCtrlData old_pad = {0}, pad;
    int running = 1;

    while (running) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        uint32_t pressed = pad.buttons & ~old_pad.buttons;
        old_pad = pad;

        // Category switching
        if (pressed & SCE_CTRL_LEFT) {
            current_cat = (current_cat + CAT_COUNT - 1) % CAT_COUNT;
            rebuild_filter();
        }
        if (pressed & SCE_CTRL_RIGHT) {
            current_cat = (current_cat + 1) % CAT_COUNT;
            rebuild_filter();
        }

        // List navigation
        if (pressed & SCE_CTRL_DOWN) {
            selected++;
            if (selected >= filtered_count)
                selected = -1;
        }
        if (pressed & SCE_CTRL_UP) {
            selected--;
            if (selected < -1)
                selected = filtered_count - 1;
        }

        // Scroll tracking
        if (selected == -1) {
            scroll_off = 0;
        } else {
            if (selected < scroll_off)
                scroll_off = selected;
            if (selected >= scroll_off + ITEMS_VISIBLE)
                scroll_off = selected - ITEMS_VISIBLE + 1;
            if (scroll_off < 0)
                scroll_off = 0;
        }

        // Select
        if (pressed & SCE_CTRL_CIRCLE) {
            if (selected == -1) {
                clear_config();
                current_titleid[0] = 0;
            } else {
                const char *tid = apps[filtered[selected]].titleid;
                save_config(tid);
                memcpy(current_titleid, tid, TITLEID_LEN + 1);
            }
        }

        if (pressed & SCE_CTRL_TRIANGLE)
            set_disabled(!plugin_disabled);

        if (pressed & SCE_CTRL_CROSS)
            running = 0;

        // ---- Draw ----
        vita2d_start_drawing();
        vita2d_clear_screen();

        // Header
        unsigned int hdr_col = plugin_disabled ? COL_HEADER_DIS : COL_HEADER;
        vita2d_draw_rectangle(0, 0, SCREEN_W, HEADER_H, hdr_col);
        vita2d_pgf_draw_text(pgf, 24, 40, COL_WHITE, 1.2f, "Vita Auto Launcher");

        const char *status_str = plugin_disabled ? "DISABLED" : "ENABLED";
        unsigned int status_col = plugin_disabled ? COL_RED : COL_GREEN;
        vita2d_pgf_draw_text(pgf, 24, 68, status_col, 0.8f, status_str);

        const char *launches = current_titleid[0] ? current_titleid : "Default";
        char status_line[96];
        snprintf(status_line, sizeof(status_line), "Launches: %s  |  Cat: %s",
            launches, CAT_NAMES[(int)current_cat]);
        vita2d_pgf_draw_text(pgf, 200, 68, COL_GRAY, 0.8f, status_line);

        // Category bar
        draw_catbar(pgf);

        // Default (LiveArea) option — always shown regardless of category
        draw_item(pgf, -1, LIST_Y + ITEM_H, selected == -1);

        // Filtered app list
        for (int i = 0; i < ITEMS_VISIBLE && (i + scroll_off) < filtered_count; i++) {
            int app_idx = filtered[i + scroll_off];
            int y       = LIST_Y + (i + 2) * ITEM_H;
            draw_item(pgf, app_idx, y, (i + scroll_off) == selected);
        }

        // Footer
        vita2d_pgf_draw_text(pgf, 24, SCREEN_H - 10, COL_DIM, 0.75f,
            "Left/Right: Category   O: Select   Triangle: Toggle   X: Exit");

        vita2d_end_drawing();
        vita2d_swap_buffers();
    }

    sceKernelExitProcess(0);
    return 0;
}
