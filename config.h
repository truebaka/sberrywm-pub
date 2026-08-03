#ifndef CONFIG_H
#define CONFIG_H

#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <xkbcommon/xkbcommon.h>
#include <wlr/types/wlr_keyboard.h>
#include <wayland-server-core.h>

struct sberry_server;

typedef enum {
    MODE_TILING,
    MODE_CANVAS,
} SberryMode;

typedef enum {
    TILING_DWINDLE,
    TILING_MASTER_STACK,
} TilingLayout;

/* ======================== appearance / layout ======================== */
#define MOD             WLR_MODIFIER_LOGO   /* main modifier (Super) */

#define GAP_SIZE        50
#define MARGIN          10
#define BORDER_WIDTH    3

/* Camera zoom (canvas mode). Already wired in main.c — just edit here.
 *   ZOOM_STEP  — multiplier per Super+=/− and Super+scroll (1.1 = +10%)
 *   MIN_ZOOM   — how far you can zoom out (0.1 = 10% of native)
 *   MAX_ZOOM   — how far you can zoom in   (5.0 = 500%)
 * Super+0 resets to 1.0; Super+w = zoom-to-fit. */
#define ZOOM_STEP       1.1
#define MIN_ZOOM        0.1
#define MAX_ZOOM        5.0

/* Cursor theme (xcursor). Name must match folder in ~/.icons or /usr/share/icons.
 * Space in the name is fine — matches "Future-Dark Cursors". */
#define XCURSOR_THEME_NAME  "Future-dark-cursors"
#define XCURSOR_THEME_SIZE  24
/* 0 = apps/games may set their own cursor (or hide it).
 * 1 = always force theme cursor (breaks games that draw their own). */
#define FORCE_THEME_CURSOR  0
/* Workspace (clearing) size is auto from monitor:
 *   width  = screen_w * CLEARING_SCALE
 *   height = width * 9/16   (forced 16:9)
 * If height < screen_h * CLEARING_SCALE, grow to fit and re-derive width.
 * 2.0–3.0 looks good in overview (drift-style zoom-to-fit). */
#define CLEARING_SCALE  2.5
/* Fallback only before first output is known */
#define CLEARING_WIDTH  4800
#define CLEARING_HEIGHT 2700
#define MOVE_STEP       80
#define RESIZE_STEP     60

/* ======================== soft animations ========================
 *
 *  Applies to kitty / simple clients. Electron/Zen/Discord never get
 *  dest_size zoom (see toplevel_dest_scale_ok) — only position moves.
 *
 *  ANIM_ENABLED 0  → everything instant (no lerp, no spawn/close zoom)
 *  ANIM_EASE        0 = linear, 1 = smooth cubic-out (default)
 * ============================================================== */
#define ANIM_ENABLED        1
#define ANIM_DURATION_MS    220
#define ANIM_EASE           1

/* Spawn (new window): 0=off  1=on (uses ANIM_SPAWN_STYLE) */
#define ANIM_SPAWN          1
/* 0=slide  1=zoom  2=slide+zoom */
#define ANIM_SPAWN_STYLE    1
/* 0=bottom  1=top  2=left  3=right  4=center */
#define ANIM_SPAWN_FROM     0
#define ANIM_SPAWN_DIST     80
#define ANIM_SPAWN_SCALE    0.35

/* Tiling rearrange / canvas swap lerp */
#define ANIM_MOVE           1

/* Overview enter/leave camera + window scale */
#define ANIM_OVERVIEW       1
#define ANIM_OVERVIEW_MS    220

/* Close: visual zoom-out then send_close */
#define ANIM_CLOSE          1
#define ANIM_CLOSE_MS       180
#define ANIM_CLOSE_SCALE    0.12

/* Camera zoom dest_size: 0 = never (hover correct for ALL windows).
 * 1 = shrink simple clients only (kitty); Electron/Zen always skipped. */
#define ANIM_ZOOM_DEST_SIZE 0

/* Dwindle: where new windows appear
 * 0 = under cursor (split the tile under pointer)
 * 1 = left side (oldest left, newest right leaf)
 * 2 = right side (mirrored) */
#define DWINDLE_SPAWN_SIDE  0

/* Overview (Super+Tab): 3x3 workspace grid, edges only on hover */
#define OVERVIEW_GAP        24
#define OVERVIEW_PAD        16
#define OVERVIEW_BORDER     3

/* Canvas default size (clients often start tiny) */
#define CANVAS_DEFAULT_W    1100
#define CANVAS_DEFAULT_H    700
#define CANVAS_MIN_W        640
#define CANVAS_MIN_H        400

static const float border_focused[]   = { 0.85f, 0.55f, 0.15f, 1.0f };
static const float border_unfocused[] = { 0.25f, 0.25f, 0.28f, 1.0f };

/* ======================== spawn helper ======================== */
static inline void spawn(const char *cmd) {
    pid_t pid = fork();
    if (pid < 0)
        return;
    if (pid == 0) {
        if (fork() > 0)
            _exit(0);
        setsid();
        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) {
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > 2)
                close(fd);
        }
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
}

/* ======================== actions (one dispatcher) ========================
 *
 *  Everything goes through sberry_do(server, action, arg).
 *  Directions: DIR_LEFT=0 DIR_RIGHT=1 DIR_UP=2 DIR_DOWN=3
 *  Workspaces / VT: 1-based numbers
 *  Zoom arg: +1 in, -1 out, 0 reset, 2 fit
 *  AltTab arg: +1 next, -1 prev
 *  WS step arg: +1 next, -1 prev
 * ============================================================== */

enum {
    DIR_LEFT = 0,
    DIR_RIGHT = 1,
    DIR_UP = 2,
    DIR_DOWN = 3,
};

typedef enum {
    SB_QUIT = 1,
    SB_KILL,
    SB_TOGGLE_MODE,
    SB_TOGGLE_LAYOUT,
    SB_CYCLE_LAYOUT,
    SB_TOGGLE_FULLSCREEN,
    SB_TOGGLE_OVERVIEW,
    SB_FOCUS,          /* arg: DIR_* */
    SB_MOVE,           /* arg: DIR_*  (canvas nudge / tiling swap) */
    SB_SWAP,           /* arg: DIR_* */
    SB_RESIZE,         /* arg: DIR_* */
    SB_ZOOM,           /* arg: +1 / -1 / 0 reset / 2 fit */
    SB_CENTER,
    SB_ALT_TAB,        /* arg: +1 / -1 */
    SB_WS,             /* arg: 1..9 */
    SB_WS_STEP,        /* arg: +1 next / -1 prev */
    SB_MOVE_WS,        /* arg: 1..9 */
    SB_VT,             /* arg: 1..12 */
} SberryAction;

void sberry_do(struct sberry_server *server, int action, int arg);

/* ======================== keybind table ========================
 *
 *  KB(mod, key, action)           — action, arg=0
 *  KB_ARG(mod, key, action, arg)  — action with argument
 *  KB_CMD(mod, key, "shell")      — spawn shell command
 *
 *  mod: 0 | MOD | WLR_MODIFIER_SHIFT | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT
 * ============================================================== */

typedef struct {
    uint32_t     mod;
    xkb_keysym_t keysym;
    int          action;   /* SberryAction, ignored if cmd != NULL */
    int          arg;
    const char  *cmd;      /* non-NULL → spawn instead of sberry_do */
} Keybind;

#define KB(mod, key, act)             { (mod), (key), (act), 0, NULL }
#define KB_ARG(mod, key, act, a)      { (mod), (key), (act), (a), NULL }
#define KB_CMD(mod, key, command)     { (mod), (key), 0, 0, (command) }

static const Keybind keys[] = {
    /* ----- apps / shell ----- */
    KB_CMD(MOD,                      XKB_KEY_q,     "kitty"),
    KB_CMD(MOD,                      XKB_KEY_e,     "kitty -e yazi"),
    KB_CMD(MOD,                      XKB_KEY_r,     "noctalia msg panel-toggle launcher"),
    KB_CMD(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_v,     "noctalia msg panel-toggle clipboard"),
    KB_CMD(MOD,                      XKB_KEY_m,     "noctalia msg panel-toggle wallpaper"),
    KB_CMD(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_m,     "noctalia msg panel-toggle session"),
    KB_CMD(MOD | WLR_MODIFIER_ALT,   XKB_KEY_r,     "systemctl --user restart noctalia xdg-desktop-portal xdg-desktop-portal-wlr"),

    /* ----- screenshots / record / dnd ----- */
    KB_CMD(0,                        XKB_KEY_Print, "noctalia msg screenshot-fullscreen"),
    KB_CMD(WLR_MODIFIER_SHIFT,       XKB_KEY_Print, "noctalia msg screenshot-region"),
    KB_CMD(MOD,                      XKB_KEY_Print, "noctalia msg plugin noctalia/screen_recorder:service all toggle"),
    KB_CMD(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_Print, "noctalia msg plugin noctalia/screen_recorder:service all replay-save"),
    KB_CMD(0,                        XKB_KEY_F10,   "noctalia msg notification-dnd-toggle"),

    /* ----- session ----- */
    KB(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_q, SB_QUIT),
    KB(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_c, SB_KILL),

    /* ----- modes / layout ----- */
    KB(MOD,                      XKB_KEY_space,  SB_TOGGLE_MODE),
    KB(MOD | WLR_MODIFIER_ALT,   XKB_KEY_space,  SB_CYCLE_LAYOUT),
    KB(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_space,  SB_TOGGLE_LAYOUT),
    KB(MOD,                      XKB_KEY_f,      SB_TOGGLE_FULLSCREEN),

    /* ----- focus ----- */
    KB_ARG(MOD, XKB_KEY_h, SB_FOCUS, DIR_LEFT),
    KB_ARG(MOD, XKB_KEY_l, SB_FOCUS, DIR_RIGHT),
    KB_ARG(MOD, XKB_KEY_k, SB_FOCUS, DIR_UP),
    KB_ARG(MOD, XKB_KEY_j, SB_FOCUS, DIR_DOWN),

    /* ----- move ----- */
    KB_ARG(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_h, SB_MOVE, DIR_LEFT),
    KB_ARG(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_l, SB_MOVE, DIR_RIGHT),
    KB_ARG(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_k, SB_MOVE, DIR_UP),
    KB_ARG(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_j, SB_MOVE, DIR_DOWN),

    /* ----- swap ----- */
    KB_ARG(MOD | WLR_MODIFIER_CTRL, XKB_KEY_h, SB_SWAP, DIR_LEFT),
    KB_ARG(MOD | WLR_MODIFIER_CTRL, XKB_KEY_l, SB_SWAP, DIR_RIGHT),
    KB_ARG(MOD | WLR_MODIFIER_CTRL, XKB_KEY_k, SB_SWAP, DIR_UP),
    KB_ARG(MOD | WLR_MODIFIER_CTRL, XKB_KEY_j, SB_SWAP, DIR_DOWN),

    /* ----- resize ----- */
    KB_ARG(MOD | WLR_MODIFIER_ALT, XKB_KEY_h, SB_RESIZE, DIR_LEFT),
    KB_ARG(MOD | WLR_MODIFIER_ALT, XKB_KEY_l, SB_RESIZE, DIR_RIGHT),
    KB_ARG(MOD | WLR_MODIFIER_ALT, XKB_KEY_k, SB_RESIZE, DIR_UP),
    KB_ARG(MOD | WLR_MODIFIER_ALT, XKB_KEY_j, SB_RESIZE, DIR_DOWN),

    /* ----- zoom / canvas nav ----- */
    KB_ARG(MOD, XKB_KEY_equal, SB_ZOOM, +1),
    KB_ARG(MOD, XKB_KEY_plus,  SB_ZOOM, +1),
    KB_ARG(MOD, XKB_KEY_minus, SB_ZOOM, -1),
    KB_ARG(MOD, XKB_KEY_w,     SB_ZOOM,  2),
    KB_ARG(MOD, XKB_KEY_0,     SB_ZOOM,  0),
    KB(MOD, XKB_KEY_c, SB_CENTER),

    /* ----- alt-tab ----- */
    KB_ARG(WLR_MODIFIER_ALT,                      XKB_KEY_Tab, SB_ALT_TAB, +1),
    KB_ARG(WLR_MODIFIER_ALT | WLR_MODIFIER_SHIFT, XKB_KEY_Tab, SB_ALT_TAB, -1),

    /* ----- overview ----- */
    KB(MOD, XKB_KEY_Tab, SB_TOGGLE_OVERVIEW),

    /* ----- workspaces 1..9 ----- */
    KB_ARG(MOD, XKB_KEY_1, SB_WS, 1),
    KB_ARG(MOD, XKB_KEY_2, SB_WS, 2),
    KB_ARG(MOD, XKB_KEY_3, SB_WS, 3),
    KB_ARG(MOD, XKB_KEY_4, SB_WS, 4),
    KB_ARG(MOD, XKB_KEY_5, SB_WS, 5),
    KB_ARG(MOD, XKB_KEY_6, SB_WS, 6),
    KB_ARG(MOD, XKB_KEY_7, SB_WS, 7),
    KB_ARG(MOD, XKB_KEY_8, SB_WS, 8),
    KB_ARG(MOD, XKB_KEY_9, SB_WS, 9),
    KB_ARG(MOD, XKB_KEY_bracketright, SB_WS_STEP, +1),
    KB_ARG(MOD, XKB_KEY_bracketleft,  SB_WS_STEP, -1),

    /* ----- move window to workspace ----- */
    KB_ARG(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_1, SB_MOVE_WS, 1),
    KB_ARG(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_2, SB_MOVE_WS, 2),
    KB_ARG(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_3, SB_MOVE_WS, 3),
    KB_ARG(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_4, SB_MOVE_WS, 4),
    KB_ARG(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_5, SB_MOVE_WS, 5),
    KB_ARG(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_6, SB_MOVE_WS, 6),
    KB_ARG(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_7, SB_MOVE_WS, 7),
    KB_ARG(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_8, SB_MOVE_WS, 8),
    KB_ARG(MOD | WLR_MODIFIER_SHIFT, XKB_KEY_9, SB_MOVE_WS, 9),

    /* ----- VT switch ----- */
    KB_ARG(WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F1,  SB_VT, 1),
    KB_ARG(WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F2,  SB_VT, 2),
    KB_ARG(WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F3,  SB_VT, 3),
    KB_ARG(WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F4,  SB_VT, 4),
    KB_ARG(WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F5,  SB_VT, 5),
    KB_ARG(WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F6,  SB_VT, 6),
    KB_ARG(WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F7,  SB_VT, 7),
    KB_ARG(WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F8,  SB_VT, 8),
    KB_ARG(WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F9,  SB_VT, 9),
    KB_ARG(WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F10, SB_VT, 10),
    KB_ARG(WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F11, SB_VT, 11),
    KB_ARG(WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_F12, SB_VT, 12),
    KB_ARG(0, XKB_KEY_XF86Switch_VT_1,  SB_VT, 1),
    KB_ARG(0, XKB_KEY_XF86Switch_VT_2,  SB_VT, 2),
    KB_ARG(0, XKB_KEY_XF86Switch_VT_3,  SB_VT, 3),
    KB_ARG(0, XKB_KEY_XF86Switch_VT_4,  SB_VT, 4),
    KB_ARG(0, XKB_KEY_XF86Switch_VT_5,  SB_VT, 5),
    KB_ARG(0, XKB_KEY_XF86Switch_VT_6,  SB_VT, 6),
    KB_ARG(0, XKB_KEY_XF86Switch_VT_7,  SB_VT, 7),
    KB_ARG(0, XKB_KEY_XF86Switch_VT_8,  SB_VT, 8),
    KB_ARG(0, XKB_KEY_XF86Switch_VT_9,  SB_VT, 9),
    KB_ARG(0, XKB_KEY_XF86Switch_VT_10, SB_VT, 10),
    KB_ARG(0, XKB_KEY_XF86Switch_VT_11, SB_VT, 11),
    KB_ARG(0, XKB_KEY_XF86Switch_VT_12, SB_VT, 12),
};

static const size_t keys_len = sizeof(keys) / sizeof(keys[0]);

/* ======================== keyboard ======================== */
static const char *keyboard_layouts = "us,ru";
static const char *keyboard_options = "grp:alt_shift_toggle";

/* ======================== autostart ======================== */
static const char *autostart[] = {
    "dbus-update-activation-environment --systemd WAYLAND_DISPLAY XDG_CURRENT_DESKTOP",
    "systemctl --user import-environment WAYLAND_DISPLAY XDG_CURRENT_DESKTOP",
    "systemctl --user start noctalia xdg-desktop-portal xdg-desktop-portal-wlr",
    "waybar -c ~/.config/waybar-sberry/config -s ~/.config/waybar-sberry/style.css",
    "kitty",
};
static const size_t autostart_len = sizeof(autostart) / sizeof(autostart[0]);

#endif
