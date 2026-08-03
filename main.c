#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <sys/types.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/backend.h>
#include <linux/input-event-codes.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <xkbcommon/xkbcommon.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/util/edges.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/xwayland.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <xcb/xproto.h>
#include "config.h"

struct sberry_keyboard {
    struct wl_list link;
    struct sberry_server *server;
    struct wlr_keyboard *keyboard;
    struct wl_listener key;
    struct wl_listener modifiers;
    struct wl_listener destroy;
};

struct sberry_output {
    struct wlr_output *wlr_output;
    struct sberry_server *server;
    struct wl_listener frame;
    struct wl_listener destroy;
    struct wl_list link;
};

struct sberry_camera {
    double cx, cy;
    double zoom;
};

struct sberry_clearing {
    double base_mx, base_my;
    int width, height;
    struct wl_list toplevels;
    double saved_cx, saved_cy, saved_zoom; /* last camera (any mode) */
    bool visited;
    /* Per-workspace mode: each clearing remembers tiling vs canvas */
    SberryMode mode;
    double canvas_cx, canvas_cy, canvas_zoom;
    bool canvas_saved;
};

struct sberry_server {
    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    int cursor_size_loaded; /* last xcursor pixel size for zoom scaling */
    struct wlr_cursor_shape_manager_v1 *cursor_shape_manager;
    struct wl_listener request_set_cursor_shape;
    struct wlr_surface *last_pointer_surface;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;
    struct wl_listener new_input;
    struct wl_list keyboards;
    struct wl_list toplevels; // Global list of all windows[cite: 2]
    struct wl_display *display;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_scene *scene;
    struct wlr_xdg_shell *xdg_shell;
    struct wl_listener new_output;
    struct wl_listener new_toplevel;
    struct wl_listener new_xdg_popup;
    struct wlr_seat *seat;
    struct wlr_output_layout *output_layout;
    struct wl_list outputs;
    struct sberry_camera camera;
    bool panning;
    double pan_start_x, pan_start_y;
    double camera_start_cx, camera_start_cy;
    int screen_width, screen_height;
    /* Exclusive zones from layer-shell (waybar/noctalia) — usable desktop area */
    int usable_x, usable_y, usable_w, usable_h;
    struct sberry_toplevel *grabbed_toplevel;
    double grab_x, grab_y;
    double grab_toplevel_mx, grab_toplevel_my;
    int grab_toplevel_w, grab_toplevel_h;
    uint32_t resize_last_configure_ms; /* throttle set_size during drag */
    bool tiling_drag;
    struct wl_listener request_cursor;
    struct wlr_output_manager_v1 *output_manager;
    struct wlr_layer_shell_v1 *layer_shell;
    struct wl_listener new_layer_surface;
    struct wlr_xdg_output_manager_v1 *xdg_output_manager;
    struct wl_list layer_surfaces[4];
    struct wlr_scene_tree *layer_tree[4];
    struct wlr_scene_tree *toplevel_tree;
    struct wlr_scene_tree *fullscreen_tree; /* above TOP bar, below OVERLAY */
    struct sberry_clearing clearings[9]; 
    int current_clearing_idx;
    bool super_pressed;
    SberryMode mode;
    TilingLayout tiling_layout; // DWINDLE or MASTER_STACK[cite: 2]
    struct sberry_toplevel *focused_toplevel;
    struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager;
    struct wl_listener new_xdg_decoration;
    struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_manager;
    bool resizing;
    uint32_t resize_edges;
    double canvas_cx, canvas_cy, canvas_zoom;
    bool canvas_saved;
    struct wlr_session *session;
    struct wl_listener session_active;
    bool session_is_active;
    xkb_layout_index_t kb_layout_index;
    struct wl_event_source *layer_timer;
    struct wl_event_source *anim_timer;
    int animating_count;
    struct wlr_xwayland *xwayland;
    struct wl_listener xwayland_ready;
    struct wl_listener new_xwayland_surface;
    struct wl_listener request_set_selection;
    struct wl_listener request_set_primary_selection;
    struct wlr_compositor *compositor;
    /* Games / FPS: relative pointer + constraints (cursor lock) */
    struct wlr_relative_pointer_manager_v1 *relative_pointer_manager;
    struct wlr_pointer_constraints_v1 *pointer_constraints;
    struct wlr_pointer_constraint_v1 *active_constraint;
    struct wl_listener new_pointer_constraint;
    struct wl_listener constraint_commit;
    struct wl_listener constraint_destroy;
    /* Overview: 3x3 workspace grid, borders only on hover */
    bool overview;
    bool overview_leaving; /* exit anim: camera zooms into target ws */
    int overview_leave_target; /* 0..8 */
    double overview_save_cx, overview_save_cy, overview_save_zoom;
    /* Camera lerp enter/leave — no per-window dest_size (browsers stay solid) */
    bool overview_cam_anim;
    double ov_cam_from_cx, ov_cam_from_cy, ov_cam_from_zoom;
    double ov_cam_to_cx, ov_cam_to_cy, ov_cam_to_zoom;
    uint32_t ov_cam_start_ms;
    int overview_hover; /* -1 none, 0..8 cell under cursor */
    struct wlr_scene_tree *overview_tree;
    struct wlr_scene_rect *overview_border[9][4]; /* 4 edges per cell */
    /* Live xdg_popups — hidden while overview is active */
    struct wl_list popups;
};

struct sberry_toplevel {
    struct wl_list link;
    struct wl_list clearing_link;
    struct sberry_server *server;
    struct wlr_xdg_toplevel *toplevel;              /* NULL for Xwayland */
    struct wlr_xwayland_surface *xwayland_surface;  /* NULL for Wayland */
    struct wlr_scene_tree *scene_tree;
    struct wlr_scene_tree *surface_tree;
    struct wlr_scene_rect *border[4];
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener request_configure;
    struct wl_listener associate;
    struct wl_listener dissociate;
    double mx, my;
    int width, height;
    bool mapped;
    /* Canvas positions preserved across tiling <-> canvas toggles */
    double canvas_mx, canvas_my;
    int canvas_width, canvas_height;
    bool has_canvas_pos;
    /* overview restore */
    double ov_mx, ov_my;
    int ov_w, ov_h;
    bool ov_saved;
    /* overview visual-only anim (screen-space pos + dest_size scale) */
    bool ov_animating;
    bool ov_leaving; /* true = exit anim; on finish restore world geom */
    double ov_from_sx, ov_from_sy, ov_to_sx, ov_to_sy;
    double ov_from_scale, ov_to_scale;
    uint32_t ov_anim_start_ms;
    /* close zoom-out then real close */
    bool closing;
    double visual_scale; /* last applied buffer scale (1.0 = native) */
    /* soft animation state */
    bool animating;
    bool anim_zoom;
    bool needs_spawn_anim; /* first tiling place → zoom from tile center */
    double anim_from_mx, anim_from_my, anim_to_mx, anim_to_my;
    int anim_from_w, anim_from_h, anim_to_w, anim_to_h;
    double anim_from_scale, anim_to_scale;
    double anim_anchor_x, anim_anchor_y;
    uint32_t anim_start_ms;
    /* Fullscreen */
    bool fullscreen;
    double fs_save_mx, fs_save_my;
    int fs_save_w, fs_save_h;
    struct wl_listener request_fullscreen;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wlr_xdg_toplevel_decoration_v1 *decoration;
    struct wl_listener decoration_request_mode;
    struct wl_listener decoration_destroy;
    struct wlr_foreign_toplevel_handle_v1 *foreign_handle;
};

struct sberry_layer_surface {
    struct wl_list link;
    struct sberry_server *server;
    struct wlr_layer_surface_v1 *layer_surface;
    struct wlr_scene_layer_surface_v1 *scene_layer;
    struct wl_listener destroy;
    struct wl_listener commit;
    enum zwlr_layer_shell_v1_layer layer;
};

static void toplevel_update_borders(struct sberry_toplevel *t);
static void focus_toplevel(struct sberry_server *server, struct sberry_toplevel *t);
static void server_arrange_tiles(struct sberry_server *server);
static void server_update_camera(struct sberry_server *server);
static void server_layout_clearings(struct sberry_server *server);
static void apply_camera(struct sberry_server *server, struct sberry_toplevel *t);
static void toplevel_apply_visual_scale(struct sberry_toplevel *t, double scale);
static bool toplevel_dest_scale_ok(struct sberry_toplevel *t);
static void camera_zoom_at_cursor(struct sberry_server *server, double factor);
static void publish_window_coords(struct sberry_server *server);
static void camera_clamp(struct sberry_server *server);
static struct sberry_clearing *
toplevel_get_clearing(struct sberry_server *server, struct sberry_toplevel *t);
static void toplevel_clamp_to_clearing(struct sberry_server *server,
                                       struct sberry_toplevel *t);
static void toplevel_world_xy(struct sberry_server *server, struct sberry_toplevel *t,
                              double *wx, double *wy);
static void tiling_reset_camera(struct sberry_server *server);
static void swap_with_neighbor(struct sberry_server *server, int dx, int dy);
static void activate_constraint(struct sberry_server *server,
                                struct wlr_pointer_constraint_v1 *constraint);
static void handle_new_pointer_constraint(struct wl_listener *listener, void *data);
static void overview_enter(struct sberry_server *server);
static void overview_leave(struct sberry_server *server, int goto_idx);
static int overview_cell_at(struct sberry_server *server, double sx, double sy);
static void overview_update_hover(struct sberry_server *server);
static void overview_place_border_screen(struct sberry_server *server, int i);
static void overview_cell_screen(struct sberry_server *server, int i,
                                 int *ox, int *oy, int *ow, int *oh);
static void overview_set_popups_visible(struct sberry_server *server, bool on);
static void overview_layout_clearing(struct sberry_server *server, int ci);
static void overview_apply_windows(struct sberry_server *server);
static void overview_fit_camera(struct sberry_server *server,
                                double *out_cx, double *out_cy, double *out_zoom);
static void server_update_usable_area(struct sberry_server *server);
static int layer_ns_rank(const char *ns);
static void cursor_set_themed(struct sberry_server *server, const char *name);
static void begin_interactive(struct sberry_server *server, struct sberry_toplevel *t,
                              bool resize, uint32_t edges);
static void interactive_commit_size(struct sberry_server *server,
                                    struct sberry_toplevel *t, bool force);
static bool surface_is_xdg_popup(struct wlr_surface *surface);



/* Write camera + window coords for external widgets (waybar / noctalia / scripts).
 *   $XDG_RUNTIME_DIR/sberry-coords
 * Format (one line per object):
 *   camera <world_cx> <world_cy> <zoom> <mode>
 *   camera_ws <local_cx> <local_cy> <ws_1based>  (0,0 = center of ws)
 *   window <local_mx> <local_my> <w> <h> <focused 0|1> <app_id>
 */
static void publish_window_coords(struct sberry_server *server) {
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (!rt)
        return;

    char path[256];
    snprintf(path, sizeof(path), "%s/sberry-coords", rt);
    FILE *f = fopen(path, "w");
    if (f) {
        struct sberry_clearing *curr_cl = &server->clearings[server->current_clearing_idx];
        /* world camera */
        fprintf(f, "camera %.2f %.2f %.4f %s\n",
                server->camera.cx, server->camera.cy, server->camera.zoom,
                server->mode == MODE_TILING ? "tiling" : "canvas");
        /* local to current workspace: origin 0,0 = CENTER of this ws
         * (camera parked on ws → 0,0 on every workspace, not only ws5) */
        fprintf(f, "camera_ws %.2f %.2f %d\n",
                server->camera.cx - (curr_cl->base_mx + curr_cl->width  / 2.0),
                server->camera.cy - (curr_cl->base_my + curr_cl->height / 2.0),
                server->current_clearing_idx + 1);

        struct sberry_toplevel *t;
        wl_list_for_each(t, &curr_cl->toplevels, clearing_link) {
            if (!t->mapped)
                continue;
            const char *app = "";
            if (t->toplevel && t->toplevel->app_id)
                app = t->toplevel->app_id;
            else if (t->xwayland_surface && t->xwayland_surface->class)
                app = t->xwayland_surface->class;
            char safe[64];
            size_t j = 0;
            for (size_t i = 0; app[i] && j + 1 < sizeof(safe); i++) {
                char c = app[i];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                    c = '_';
                safe[j++] = c;
            }
            safe[j] = '\0';
            fprintf(f, "window %.2f %.2f %d %d %d %s\n",
                    t->mx, t->my, t->width, t->height,
                    (server->focused_toplevel == t) ? 1 : 0, safe[0] ? safe : "-");
        }
        fclose(f);
    }

    /* --- Workspaces for Noctalia / waybar / scripts ---
     * Noctalia uses compositor backends or ext-workspace-v1 (wlroots 0.20+).
     * On 0.19 we publish files the bar can poll:
     *   $XDG_RUNTIME_DIR/sberry-workspace          → "3\n" (1-based active)
     *   $XDG_RUNTIME_DIR/sberry-workspaces         → one line per WS
     *   $XDG_RUNTIME_DIR/sberry-workspaces-waybar  → JSON for waybar custom
     * Switch from bar:
     *   echo workspace 3 > $XDG_RUNTIME_DIR/sberrywm.cmd
     *   echo workspace-move 5 > $XDG_RUNTIME_DIR/sberrywm.cmd
     */
    {
        snprintf(path, sizeof(path), "%s/sberry-workspace", rt);
        f = fopen(path, "w");
        if (f) {
            fprintf(f, "%d\n", server->current_clearing_idx + 1);
            fclose(f);
        }

        snprintf(path, sizeof(path), "%s/sberry-workspaces", rt);
        f = fopen(path, "w");
        if (f) {
            for (int i = 0; i < 9; i++) {
                int occupied = 0;
                struct sberry_toplevel *tw;
                wl_list_for_each(tw, &server->clearings[i].toplevels, clearing_link) {
                    if (tw->mapped) { occupied = 1; break; }
                }
                int active = (i == server->current_clearing_idx) ? 1 : 0;
                /* id active occupied name */
                fprintf(f, "%d %d %d %d\n", i + 1, active, occupied, i + 1);
            }
            fclose(f);
        }

        /* waybar-style JSON array */
        snprintf(path, sizeof(path), "%s/sberry-workspaces-waybar", rt);
        f = fopen(path, "w");
        if (f) {
            fprintf(f, "{\"text\":\"%d\",\"alt\":\"%d\",\"tooltip\":\"workspace %d/9\",\"class\":[",
                    server->current_clearing_idx + 1,
                    server->current_clearing_idx + 1,
                    server->current_clearing_idx + 1);
            for (int i = 0; i < 9; i++) {
                int occupied = 0;
                struct sberry_toplevel *tw;
                wl_list_for_each(tw, &server->clearings[i].toplevels, clearing_link) {
                    if (tw->mapped) { occupied = 1; break; }
                }
                const char *cls = (i == server->current_clearing_idx) ? "active"
                                  : (occupied ? "occupied" : "empty");
                fprintf(f, "%s\"%s\"", i ? "," : "", cls);
            }
            fprintf(f, "]}\n");
            fclose(f);
        }
    }
}

/* t->mx/my are LOCAL to the window's clearing (origin 0,0 = top-left of ws). */
static void toplevel_world_xy(struct sberry_server *server, struct sberry_toplevel *t,
                              double *wx, double *wy) {
    double bx = 0.0, by = 0.0;
    if (server && t) {
        struct sberry_clearing *cl = toplevel_get_clearing(server, t);
        if (cl) {
            bx = cl->base_mx;
            by = cl->base_my;
        }
    }
    if (wx) *wx = bx + (t ? t->mx : 0.0);
    if (wy) *wy = by + (t ? t->my : 0.0);
}

static void apply_camera(struct sberry_server *server, struct sberry_toplevel *t) {
    if (!t || !t->scene_tree) return;
    /* Fullscreen: exact (0,0) — no camera float (fixes 1px Roblox shift).
     * During overview we place FS windows into their workspace cell instead. */
    if (t->fullscreen && !server->overview && !server->overview_leaving) {
        wlr_scene_node_set_position(&t->scene_tree->node, 0, 0);
        if (t->surface_tree)
            wlr_scene_node_set_position(&t->surface_tree->node, 0, 0);
        if (t->xwayland_surface && t->mapped && t->width > 0 && t->height > 0)
            wlr_xwayland_surface_configure(t->xwayland_surface, 0, 0, t->width, t->height);
        return;
    }
    double wx, wy;
    toplevel_world_xy(server, t, &wx, &wy);
    int sx = (int)lround((wx - server->camera.cx) * server->camera.zoom
                    + server->screen_width / 2.0);
    int sy = (int)lround((wy - server->camera.cy) * server->camera.zoom
                    + server->screen_height / 2.0);
    wlr_scene_node_set_position(&t->scene_tree->node, sx, sy);
    /* Skip Xwayland configure while interactively resizing — size is
     * committed once on button release (avoids .NET / Electron thrash). */
    if (t->xwayland_surface && t->mapped && t->width > 0 && t->height > 0
            && !(server->resizing && server->grabbed_toplevel == t)
            && !server->overview) {
        wlr_xwayland_surface_configure(t->xwayland_surface,
            (int16_t)sx, (int16_t)sy, t->width, t->height);
    }

    /* Canvas hover for ALL windows: no dest_size from camera.zoom.
     * Positions still follow the camera; buffers stay 1:1 so pointer
     * coords match what the client draws (Electron/Zen/kitty/…).
     * Set ANIM_ZOOM_DEST_SIZE 1 in config.h to shrink simple clients only. */
    if (!server->overview && !server->overview_leaving
            && !t->animating && !t->closing && !t->ov_animating
            && !t->fullscreen) {
        if (ANIM_ZOOM_DEST_SIZE && toplevel_dest_scale_ok(t)) {
            double sc = server->camera.zoom;
            if (sc < 0.02) sc = 0.02;
            if (sc > 1.0) sc = 1.0;
            toplevel_apply_visual_scale(t, sc);
        } else if (t->visual_scale < 0.999 || t->visual_scale > 1.001) {
            toplevel_apply_visual_scale(t, 1.0);
        }
    }
}

static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

static double anim_ease(double t) {
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;
#if ANIM_EASE
    /* smooth cubic-out */
    double u = 1.0 - t;
    return 1.0 - u * u * u;
#else
    return t; /* linear */
#endif
}

static void anim_timer_arm(struct sberry_server *server);

/* Visual zoom: scale rendered buffers without resize-configure spam */
static void toplevel_scale_buffer_cb(struct wlr_scene_buffer *buffer,
                                     int sx, int sy, void *data) {
    (void)sx;
    (void)sy;
    double scale = *(double *)data;
    if (!buffer || !buffer->buffer)
        return;
    int bw = buffer->buffer->width;
    int bh = buffer->buffer->height;
    if (bw <= 0 || bh <= 0)
        return;
    if (scale >= 0.999) {
        wlr_scene_buffer_set_dest_size(buffer, bw, bh);
        return;
    }
    int dw = (int)(bw * scale + 0.5);
    int dh = (int)(bh * scale + 0.5);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    wlr_scene_buffer_set_dest_size(buffer, dw, dh);
}

/* Apps whose subsurface trees break under per-buffer dest_size (Electron,
 * Chromium, Firefox/Zen, …). For these we only move the scene node with the
 * camera — never shrink buffers. Simple clients (kitty, …) still get
 * driftwm-style dest_size = zoom. */
static bool toplevel_dest_scale_ok(struct sberry_toplevel *t) {
    if (!t)
        return false;
    if (t->xwayland_surface)
        return false;
    const char *id = (t->toplevel && t->toplevel->app_id) ? t->toplevel->app_id : NULL;
    if (!id || !id[0])
        return true;
    if (strstr(id, "discord") || strstr(id, "Discord")) return false;
    if (strstr(id, "chrom") || strstr(id, "Chromium") || strstr(id, "Chrome")) return false;
    if (strstr(id, "electron") || strstr(id, "Electron")) return false;
    if (strstr(id, "code") || strstr(id, "vscode") || strstr(id, "VSCodium")) return false;
    if (strstr(id, "slack") || strstr(id, "Slack")) return false;
    if (strstr(id, "zen") || strstr(id, "Zen")) return false;
    if (strstr(id, "firefox") || strstr(id, "Firefox") || strstr(id, "nightly")
            || strstr(id, "librewolf") || strstr(id, "floorp")) return false;
    if (strstr(id, "spotify") || strstr(id, "Spotify")) return false;
    if (strstr(id, "1password") || strstr(id, "obsidian")) return false;
    return true;
}

static void toplevel_apply_visual_scale(struct sberry_toplevel *t, double scale) {
    if (!t || !t->scene_tree)
        return;
    if (scale < 0.02) scale = 0.02;
    if (scale > 1.0) scale = 1.0;
    t->visual_scale = scale;

    /* Scale only client buffers under surface_tree — never whole scene_tree.
     * Walking scene_tree breaks xdg geometry (subsurface layout stays full-size
     * while dest_size shrinks → content spills over SSD / empty orange frames). */
    if (t->surface_tree) {
        wlr_scene_node_for_each_buffer(&t->surface_tree->node,
                                       toplevel_scale_buffer_cb, &scale);
    }

    /* SSD borders stick out past content and cross overview cell frames —
     * hide them while overview / leave-anim is active. */
    bool hide_ssd = (t->server && (t->server->overview || t->server->overview_leaving));

    if (t->border[0] && t->width > 0 && t->height > 0) {
        int bw = BORDER_WIDTH;
        int vw = (int)(t->width * scale + 0.5);
        int vh = (int)(t->height * scale + 0.5);
        if (vw < 1) vw = 1;
        if (vh < 1) vh = 1;
        if (t->surface_tree)
            wlr_scene_node_set_position(&t->surface_tree->node,
                                        hide_ssd ? 0 : bw, hide_ssd ? 0 : bw);
        wlr_scene_rect_set_size(t->border[0], vw + 2 * bw, bw);
        wlr_scene_node_set_position(&t->border[0]->node, 0, 0);
        wlr_scene_rect_set_size(t->border[1], vw + 2 * bw, bw);
        wlr_scene_node_set_position(&t->border[1]->node, 0, vh + bw);
        wlr_scene_rect_set_size(t->border[2], bw, vh);
        wlr_scene_node_set_position(&t->border[2]->node, 0, bw);
        wlr_scene_rect_set_size(t->border[3], bw, vh);
        wlr_scene_node_set_position(&t->border[3]->node, vw + bw, bw);
        for (int i = 0; i < 4; i++) {
            if (!t->border[i])
                continue;
            wlr_scene_node_set_enabled(&t->border[i]->node, !hide_ssd);
            if (!hide_ssd)
                wlr_scene_node_raise_to_top(&t->border[i]->node);
        }
    }
}

static void toplevel_anim_to(struct sberry_toplevel *t,
                             double to_mx, double to_my, int to_w, int to_h) {
    if (!t || !t->mapped)
        return;
#if !ANIM_ENABLED || !ANIM_MOVE
    t->mx = to_mx; t->my = to_my;
    if (to_w > 0) t->width = to_w;
    if (to_h > 0) t->height = to_h;
    if (t->toplevel)
        wlr_xdg_toplevel_set_size(t->toplevel, t->width, t->height);
    else if (t->xwayland_surface)
        wlr_xwayland_surface_configure(t->xwayland_surface,
            (int16_t)t->mx, (int16_t)t->my, t->width, t->height);
    toplevel_update_borders(t);
    apply_camera(t->server, t);
    return;
#else
    int target_w = (to_w > 0) ? to_w : t->width;
    int target_h = (to_h > 0) ? to_h : t->height;
    bool pos_same = fabs(t->mx - to_mx) < 0.5 && fabs(t->my - to_my) < 0.5;
    bool size_same = (target_w == t->width && target_h == t->height);
    if (pos_same && size_same) {
        t->mx = to_mx; t->my = to_my;
        t->animating = false;
        t->anim_zoom = false;
        apply_camera(t->server, t);
        return;
    }
    bool was = t->animating;
    t->anim_zoom = false;
    t->anim_from_mx = t->mx;
    t->anim_from_my = t->my;
    t->anim_to_mx = to_mx;
    t->anim_to_my = to_my;
    t->anim_from_w = t->width > 0 ? t->width : target_w;
    t->anim_from_h = t->height > 0 ? t->height : target_h;
    t->anim_to_w = target_w;
    t->anim_to_h = target_h;
    /* Configure final size NOW (once). Never spam set_size during anim —
     * Electron/Vesktop GPU process dies on rapid configures. */
    if (target_w != t->width || target_h != t->height) {
        t->width = target_w;
        t->height = target_h;
        if (t->toplevel)
            wlr_xdg_toplevel_set_size(t->toplevel, target_w, target_h);
        else if (t->xwayland_surface)
            wlr_xwayland_surface_configure(t->xwayland_surface,
                (int16_t)to_mx, (int16_t)to_my, target_w, target_h);
        toplevel_update_borders(t);
    }
    t->anim_start_ms = now_ms();
    t->animating = true;
    if (!was)
        t->server->animating_count++;
    anim_timer_arm(t->server);
#endif
}

static void toplevel_anim_spawn(struct sberry_toplevel *t,
                                double to_mx, double to_my, int to_w, int to_h) {
    if (!t)
        return;
    if (to_w <= 0) to_w = t->width > 0 ? t->width : CANVAS_DEFAULT_W;
    if (to_h <= 0) to_h = t->height > 0 ? t->height : CANVAS_DEFAULT_H;
#if !ANIM_ENABLED || !ANIM_SPAWN
    t->mx = to_mx; t->my = to_my;
    t->width = to_w; t->height = to_h;
    if (t->toplevel)
        wlr_xdg_toplevel_set_size(t->toplevel, t->width, t->height);
    toplevel_update_borders(t);
    apply_camera(t->server, t);
    return;
#else
    t->width = to_w;
    t->height = to_h;
    if (t->toplevel)
        wlr_xdg_toplevel_set_size(t->toplevel, to_w, to_h);

    double anchor_x = to_mx + to_w / 2.0;
    double anchor_y = to_my + to_h / 2.0;

    if (ANIM_SPAWN_STYLE == 1 || ANIM_SPAWN_STYLE == 2) {
        double scale0 = ANIM_SPAWN_SCALE;
        if (scale0 < 0.05) scale0 = 0.05;
        if (scale0 > 1.0) scale0 = 1.0;

        t->anim_zoom = true;
        t->anim_from_scale = scale0;
        t->anim_to_scale = 1.0;
        t->anim_anchor_x = anchor_x;
        t->anim_anchor_y = anchor_y;
        t->anim_to_mx = to_mx;
        t->anim_to_my = to_my;
        t->anim_to_w = to_w;
        t->anim_to_h = to_h;
        t->anim_from_w = to_w;
        t->anim_from_h = to_h;

        double vw = to_w * scale0;
        double vh = to_h * scale0;
        t->mx = anchor_x - vw / 2.0;
        t->my = anchor_y - vh / 2.0;
        t->anim_from_mx = t->mx;
        t->anim_from_my = t->my;

        if (ANIM_SPAWN_STYLE == 2) {
            double dist = (double)ANIM_SPAWN_DIST;
            switch (ANIM_SPAWN_FROM) {
            case 1: t->my -= dist; t->anim_from_my = t->my; break;
            case 2: t->mx -= dist; t->anim_from_mx = t->mx; break;
            case 3: t->mx += dist; t->anim_from_mx = t->mx; break;
            case 4: break;
            default: t->my += dist; t->anim_from_my = t->my; break;
            }
        }

        toplevel_apply_visual_scale(t, scale0);
        apply_camera(t->server, t);

        bool was = t->animating;
        t->anim_start_ms = now_ms();
        t->animating = true;
        if (!was)
            t->server->animating_count++;
        anim_timer_arm(t->server);
        return;
    }

    t->anim_zoom = false;
    double from_mx = to_mx, from_my = to_my;
    double dist = (double)ANIM_SPAWN_DIST;
    switch (ANIM_SPAWN_FROM) {
    case 1: from_my = to_my - dist; break;
    case 2: from_mx = to_mx - dist; break;
    case 3: from_mx = to_mx + dist; break;
    case 4: break;
    default: from_my = to_my + dist; break;
    }
    t->mx = from_mx;
    t->my = from_my;
    toplevel_update_borders(t);
    apply_camera(t->server, t);
    toplevel_anim_to(t, to_mx, to_my, to_w, to_h);
#endif
}

static void anim_tick(struct sberry_server *server) {
    if (server->animating_count <= 0 && !server->overview_cam_anim)
        return;
    uint32_t now = now_ms();
    int still = 0;

    /* drift-style overview: lerp camera; dest_size follows zoom */
    if (server->overview_cam_anim) {
        double elapsed = (double)(now - server->ov_cam_start_ms);
        double dur = (double)ANIM_OVERVIEW_MS;
        if (dur < 1.0) dur = 1.0;
        double p = anim_ease(elapsed / dur);
        if (p > 1.0) p = 1.0;
        server->camera.cx = server->ov_cam_from_cx
            + (server->ov_cam_to_cx - server->ov_cam_from_cx) * p;
        server->camera.cy = server->ov_cam_from_cy
            + (server->ov_cam_to_cy - server->ov_cam_from_cy) * p;
        server->camera.zoom = server->ov_cam_from_zoom
            + (server->ov_cam_to_zoom - server->ov_cam_from_zoom) * p;
        overview_apply_windows(server);
        for (int bi = 0; bi < 9; bi++)
            overview_place_border_screen(server, bi);
        if (server->overview)
            overview_update_hover(server);
        if (p >= 1.0) {
            server->overview_cam_anim = false;
            server->camera.cx = server->ov_cam_to_cx;
            server->camera.cy = server->ov_cam_to_cy;
            server->camera.zoom = server->ov_cam_to_zoom;
            if (server->overview_leaving) {
                int target = server->overview_leave_target;
                server->overview_leaving = false;
                overview_set_popups_visible(server, true);
                for (int i = 0; i < 9; i++) {
                    struct sberry_toplevel *tw;
                    wl_list_for_each(tw, &server->clearings[i].toplevels, clearing_link) {
                        tw->ov_animating = false;
                        tw->ov_leaving = false;
                        if (tw->fullscreen) {
                            /* Re-pin to real fullscreen presentation */
                            int sw = server->screen_width > 0 ? server->screen_width : 1920;
                            int sh = server->screen_height > 0 ? server->screen_height : 1080;
                            tw->mx = 0;
                            tw->my = 0;
                            tw->width = sw;
                            tw->height = sh;
                            tw->ov_saved = false;
                            for (int bi = 0; bi < 4; bi++) {
                                if (tw->border[bi])
                                    wlr_scene_node_set_enabled(&tw->border[bi]->node, false);
                            }
                            if (tw->surface_tree)
                                wlr_scene_node_set_position(&tw->surface_tree->node, 0, 0);
                            if (tw->scene_tree && server->fullscreen_tree) {
                                wlr_scene_node_reparent(&tw->scene_tree->node,
                                                        server->fullscreen_tree);
                                wlr_scene_node_set_enabled(&tw->scene_tree->node,
                                    i == target && tw->mapped);
                                if (i == target && tw->mapped)
                                    wlr_scene_node_raise_to_top(&tw->scene_tree->node);
                            }
                            toplevel_apply_visual_scale(tw, 1.0);
                            if (i == target && tw->mapped)
                                apply_camera(server, tw);
                            continue;
                        }
                        if (tw->ov_saved) {
                            tw->mx = tw->ov_mx;
                            tw->my = tw->ov_my;
                            tw->width = tw->ov_w;
                            tw->height = tw->ov_h;
                            tw->ov_saved = false;
                        }
                        toplevel_apply_visual_scale(tw, 1.0);
                        toplevel_update_borders(tw);
                        if (tw->scene_tree)
                            wlr_scene_node_set_enabled(&tw->scene_tree->node,
                                i == target && tw->mapped);
                        if (i == target && tw->mapped)
                            apply_camera(server, tw);
                    }
                }
                if (server->mode == MODE_TILING)
                    server_arrange_tiles(server);
                else
                    server_update_camera(server);
                if (!server->focused_toplevel) {
                    struct sberry_toplevel *tw;
                    wl_list_for_each(tw, &server->clearings[target].toplevels, clearing_link) {
                        if (tw->mapped) {
                            focus_toplevel(server, tw);
                            break;
                        }
                    }
                }
                publish_window_coords(server);
            } else if (server->overview) {
                overview_apply_windows(server);
            }
        } else {
            still++;
        }
    }

    for (int ci = 0; ci < 9; ci++) {
        struct sberry_toplevel *t;
        wl_list_for_each(t, &server->clearings[ci].toplevels, clearing_link) {
            /* legacy per-window overview anim (unused in camera path) */
            if (t->ov_animating) {
                double elapsed = (double)(now - t->ov_anim_start_ms);
                double dur = (double)ANIM_OVERVIEW_MS;
                if (dur < 1.0) dur = 1.0;
                double p = anim_ease(elapsed / dur);
                if (p > 1.0) p = 1.0;
                double sc = t->ov_from_scale + (t->ov_to_scale - t->ov_from_scale) * p;
                if (sc < 0.02) sc = 0.02;
                double sx = t->ov_from_sx + (t->ov_to_sx - t->ov_from_sx) * p;
                double sy = t->ov_from_sy + (t->ov_to_sy - t->ov_from_sy) * p;
                if (t->scene_tree)
                    wlr_scene_node_set_position(&t->scene_tree->node, (int)lround(sx), (int)lround(sy));
                toplevel_apply_visual_scale(t, sc);
                if (p >= 1.0) {
                    t->ov_animating = false;
                    if (!t->closing)
                        t->animating = false;
                    if (t->ov_leaving) {
                        /* Exit anim done: restore world geometry relative to target ws */
                        t->ov_leaving = false;
                        if (t->ov_saved) {
                            t->mx = t->ov_mx;
                            t->my = t->ov_my;
                            t->width = t->ov_w;
                            t->height = t->ov_h;
                            t->ov_saved = false;
                        }
                        toplevel_apply_visual_scale(t, 1.0);
                        toplevel_update_borders(t);
                        if (ci == server->overview_leave_target) {
                            if (t->scene_tree)
                                wlr_scene_node_set_enabled(&t->scene_tree->node, true);
                            apply_camera(server, t);
                        } else {
                            if (t->scene_tree)
                                wlr_scene_node_set_enabled(&t->scene_tree->node, false);
                        }
                    } else {
                        if (t->scene_tree)
                            wlr_scene_node_set_position(&t->scene_tree->node,
                                (int)lround(t->ov_to_sx), (int)lround(t->ov_to_sy));
                        toplevel_apply_visual_scale(t, t->ov_to_scale);
                    }
                } else {
                    still++;
                }
                continue;
            }
            /* --- close zoom-out from CENTER (visual dest_size only) --- */
            if (t->closing && t->animating) {
                double elapsed = (double)(now - t->anim_start_ms);
                double dur = (double)ANIM_CLOSE_MS;
                if (dur < 1.0) dur = 1.0;
                double p = anim_ease(elapsed / dur);
                if (p > 1.0) p = 1.0;
                double sc = 1.0 + (ANIM_CLOSE_SCALE - 1.0) * p;
                if (sc < 0.02) sc = 0.02;
                toplevel_apply_visual_scale(t, sc);
                /* Keep geometric center fixed: dest_size shrinks top-left → pad by (1-sc)/2 */
                apply_camera(server, t);
                if (t->scene_tree) {
                    double z = server->camera.zoom;
                    if (z < 0.01) z = 1.0;
                    int ox = (int)lround(t->width * (1.0 - sc) * 0.5 * z);
                    int oy = (int)lround(t->height * (1.0 - sc) * 0.5 * z);
                    wlr_scene_node_set_position(&t->scene_tree->node,
                        t->scene_tree->node.x + ox,
                        t->scene_tree->node.y + oy);
                }
                if (p >= 1.0) {
                    t->animating = false;
                    t->closing = false;
                    if (t->toplevel)
                        wlr_xdg_toplevel_send_close(t->toplevel);
                    else if (t->xwayland_surface)
                        wlr_xwayland_surface_close(t->xwayland_surface);
                } else {
                    still++;
                }
                continue;
            }
            if (!t->animating)
                continue;
            double elapsed = (double)(now - t->anim_start_ms);
            double p = anim_ease(elapsed / (double)ANIM_DURATION_MS);
            if (p > 1.0) p = 1.0;

            if (t->anim_zoom) {
                double scale = t->anim_from_scale
                    + (t->anim_to_scale - t->anim_from_scale) * p;
                double vw = t->anim_to_w * scale;
                double vh = t->anim_to_h * scale;
                if (ANIM_SPAWN_STYLE == 2) {
                    double start_mx = t->anim_from_mx;
                    double start_my = t->anim_from_my;
                    double final_mx = t->anim_to_mx;
                    double final_my = t->anim_to_my;
                    double cur_cx = start_mx + (t->anim_to_w * t->anim_from_scale) / 2.0;
                    double cur_cy = start_my + (t->anim_to_h * t->anim_from_scale) / 2.0;
                    double end_cx = final_mx + t->anim_to_w / 2.0;
                    double end_cy = final_my + t->anim_to_h / 2.0;
                    double cx = cur_cx + (end_cx - cur_cx) * p;
                    double cy = cur_cy + (end_cy - cur_cy) * p;
                    t->mx = cx - vw / 2.0;
                    t->my = cy - vh / 2.0;
                } else {
                    t->mx = t->anim_anchor_x - vw / 2.0;
                    t->my = t->anim_anchor_y - vh / 2.0;
                }
                toplevel_apply_visual_scale(t, scale);
                apply_camera(server, t);
                if (p >= 1.0) {
                    t->mx = t->anim_to_mx;
                    t->my = t->anim_to_my;
                    t->width = t->anim_to_w;
                    t->height = t->anim_to_h;
                    toplevel_apply_visual_scale(t, 1.0);
                    toplevel_update_borders(t);
                    t->animating = false;
                    t->anim_zoom = false;
                    apply_camera(server, t);
                } else {
                    still++;
                }
                continue;
            }

            /* Position only during animation. Size was already configured
             * at anim start (toplevel_anim_to). Intermediate set_size kills
             * Electron/Chromium GPU process (Vesktop reload). */
            t->mx = t->anim_from_mx + (t->anim_to_mx - t->anim_from_mx) * p;
            t->my = t->anim_from_my + (t->anim_to_my - t->anim_from_my) * p;
            apply_camera(server, t);
            if (p >= 1.0) {
                t->mx = t->anim_to_mx;
                t->my = t->anim_to_my;
                t->width = t->anim_to_w;
                t->height = t->anim_to_h;
                /* final size already sent at start; re-ack once for stubborn clients */
                if (t->toplevel)
                    wlr_xdg_toplevel_set_size(t->toplevel, t->width, t->height);
                else if (t->xwayland_surface)
                    wlr_xwayland_surface_configure(t->xwayland_surface,
                        (int16_t)t->mx, (int16_t)t->my, t->width, t->height);
                toplevel_update_borders(t);
                t->animating = false;
                apply_camera(server, t);
            } else {
                still++;
            }
        }
    }
    server->animating_count = still;
    if (still == 0) {
        if (server->overview_leaving) {
            int target = server->overview_leave_target;
            server->overview_leaving = false;
            /* Ensure only target ws is visible; restore SSD + native scale */
            for (int i = 0; i < 9; i++) {
                struct sberry_toplevel *tw;
                wl_list_for_each(tw, &server->clearings[i].toplevels, clearing_link) {
                    if (!tw->scene_tree)
                        continue;
                    bool on = (i == target) && tw->mapped;
                    wlr_scene_node_set_enabled(&tw->scene_tree->node, on);
                    if (tw->ov_saved) {
                        tw->mx = tw->ov_mx;
                        tw->my = tw->ov_my;
                        tw->width = tw->ov_w;
                        tw->height = tw->ov_h;
                        tw->ov_saved = false;
                        if (tw->toplevel)
                            wlr_xdg_toplevel_set_size(tw->toplevel, tw->ov_w, tw->ov_h);
                        else if (tw->xwayland_surface)
                            wlr_xwayland_surface_configure(tw->xwayland_surface,
                                (int16_t)tw->ov_mx, (int16_t)tw->ov_my, tw->ov_w, tw->ov_h);
                    }
                    tw->ov_animating = false;
                    tw->ov_leaving = false;
                    toplevel_apply_visual_scale(tw, 1.0);
                    if (on) {
                        toplevel_update_borders(tw);
                        apply_camera(server, tw);
                    }
                }
            }
            if (server->mode == MODE_TILING)
                server_arrange_tiles(server);
            else
                server_update_camera(server);
            overview_set_popups_visible(server, true);
            /* Focus first mapped on target */
            if (!server->focused_toplevel) {
                struct sberry_toplevel *tw;
                wl_list_for_each(tw, &server->clearings[target].toplevels, clearing_link) {
                    if (tw->mapped) {
                        focus_toplevel(server, tw);
                        break;
                    }
                }
            }
        }
        publish_window_coords(server);
    }
}


static int anim_timer_cb(void *data) {
    struct sberry_server *server = data;
    anim_tick(server);
    if ((server->animating_count > 0 || server->overview_cam_anim)
            && server->anim_timer) {
        wl_event_source_timer_update(server->anim_timer, 16);
        return 16;
    }
    return 0;
}

static void anim_timer_arm(struct sberry_server *server) {
    if (!server->anim_timer)
        return;
    wl_event_source_timer_update(server->anim_timer, 16);
}




/* Place one tile; zoom-spawn if first map in tiling */
/* sx,sy = screen pixels of tile. Stored as LOCAL coords in the current clearing
 * (0,0 = top-left of that workspace). Independent of global plane layout. */
static void tiling_place(struct sberry_server *server, struct sberry_toplevel *t,
                         double sx, double sy, int sw, int sh) {
    if (!t) return;
    struct sberry_clearing *cl = &server->clearings[server->current_clearing_idx];
    int screen_w = server->screen_width;
    int screen_h = server->screen_height;
    double z = server->camera.zoom;
    if (z < 0.01) z = 0.01;
    /* screen → world → local (relative to this ws origin) */
    double world_x = server->camera.cx + (sx - screen_w / 2.0) / z;
    double world_y = server->camera.cy + (sy - screen_h / 2.0) / z;
    double nmx = world_x - cl->base_mx;
    double nmy = world_y - cl->base_my;
    if (t->needs_spawn_anim) {
        t->needs_spawn_anim = false;
        t->mx = nmx;
        t->my = nmy;
        t->width = sw;
        t->height = sh;
        toplevel_anim_spawn(t, nmx, nmy, sw, sh);
    } else {
        toplevel_anim_to(t, nmx, nmy, sw, sh);
    }
}

/* Classic dwindle on array: wins[0] gets first half, rest recurse */
static void dwindle_layout_arr(struct sberry_server *server,
                               struct sberry_toplevel **wins, int n,
                               double x, double y, double w, double h,
                               int horiz) {
    if (n <= 0) return;
    const int gap = GAP_SIZE;

    if (n == 1) {
        int fw = (int)w, fh = (int)h;
        if (fw < 150) fw = 150;
        if (fh < 150) fh = 150;
        tiling_place(server, wins[0], x, y, fw, fh);
        return;
    }

    if (horiz) {
        int fw = (int)(w / 2.0 - gap / 2.0);
        int fh = (int)h;
        if (fw < 150) fw = 150;
        tiling_place(server, wins[0], x, y, fw, fh);
        double nx = x + w / 2.0 + gap / 2.0;
        double nw = w / 2.0 - gap / 2.0;
        if (nw < 150) nw = 150;
        dwindle_layout_arr(server, wins + 1, n - 1, nx, y, nw, h, 0);
    } else {
        int fw = (int)w;
        int fh = (int)(h / 2.0 - gap / 2.0);
        if (fh < 150) fh = 150;
        tiling_place(server, wins[0], x, y, fw, fh);
        double ny = y + h / 2.0 + gap / 2.0;
        double nh = h / 2.0 - gap / 2.0;
        if (nh < 150) nh = 150;
        dwindle_layout_arr(server, wins + 1, n - 1, x, ny, w, nh, 1);
    }
}

/* Cursor-relative dwindle: insert windows oldest→newest, each splits the tile under the pointer */
struct dwindle_tile {
    struct sberry_toplevel *t;
    double x, y, w, h;
};

static int dwindle_tile_at(struct dwindle_tile *tiles, int n,
                           double cx, double cy) {
    int best = 0;
    double best_d = 1e18;
    for (int i = 0; i < n; i++) {
        double x0 = tiles[i].x, y0 = tiles[i].y;
        double x1 = x0 + tiles[i].w, y1 = y0 + tiles[i].h;
        if (cx >= x0 && cx < x1 && cy >= y0 && cy < y1)
            return i;
        double mx = x0 + tiles[i].w / 2.0;
        double my = y0 + tiles[i].h / 2.0;
        double d = (cx - mx) * (cx - mx) + (cy - my) * (cy - my);
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

static void dwindle_layout_cursor(struct sberry_server *server,
                                 struct sberry_toplevel **wins, int n,
                                 double area_x, double area_y,
                                 double area_w, double area_h) {
    if (n <= 0) return;
    const int gap = GAP_SIZE;
    double cx = server->cursor ? server->cursor->x : area_x + area_w / 2.0;
    double cy = server->cursor ? server->cursor->y : area_y + area_h / 2.0;

    struct dwindle_tile tiles[128];
    int nt = 0;

    for (int i = 0; i < n; i++) {
        if (nt == 0) {
            tiles[0].t = wins[i];
            tiles[0].x = area_x;
            tiles[0].y = area_y;
            tiles[0].w = area_w;
            tiles[0].h = area_h;
            nt = 1;
            continue;
        }
        int idx = dwindle_tile_at(tiles, nt, cx, cy);
        struct dwindle_tile *T = &tiles[idx];
        double tx = T->x, ty = T->y, tw = T->w, th = T->h;
        struct sberry_toplevel *old = T->t;

        /* Prefer split along longer axis; side = where the cursor sits */
        int split_vert = (tw >= th); /* true → left|right */
        if (split_vert) {
            int left_w = (int)(tw / 2.0 - gap / 2.0);
            if (left_w < 150) left_w = 150;
            double right_x = tx + tw / 2.0 + gap / 2.0;
            double right_w = tw / 2.0 - gap / 2.0;
            if (right_w < 150) right_w = 150;
            int cursor_on_right = (cx >= tx + tw / 2.0);
            if (cursor_on_right) {
                /* old stays left, new gets right (under cursor) */
                T->t = old; T->x = tx; T->y = ty; T->w = left_w; T->h = th;
                tiles[nt].t = wins[i];
                tiles[nt].x = right_x; tiles[nt].y = ty;
                tiles[nt].w = right_w; tiles[nt].h = th;
            } else {
                T->t = wins[i]; T->x = tx; T->y = ty; T->w = left_w; T->h = th;
                tiles[nt].t = old;
                tiles[nt].x = right_x; tiles[nt].y = ty;
                tiles[nt].w = right_w; tiles[nt].h = th;
            }
        } else {
            int top_h = (int)(th / 2.0 - gap / 2.0);
            if (top_h < 150) top_h = 150;
            double bot_y = ty + th / 2.0 + gap / 2.0;
            double bot_h = th / 2.0 - gap / 2.0;
            if (bot_h < 150) bot_h = 150;
            int cursor_on_bottom = (cy >= ty + th / 2.0);
            if (cursor_on_bottom) {
                T->t = old; T->x = tx; T->y = ty; T->w = tw; T->h = top_h;
                tiles[nt].t = wins[i];
                tiles[nt].x = tx; tiles[nt].y = bot_y;
                tiles[nt].w = tw; tiles[nt].h = bot_h;
            } else {
                T->t = wins[i]; T->x = tx; T->y = ty; T->w = tw; T->h = top_h;
                tiles[nt].t = old;
                tiles[nt].x = tx; tiles[nt].y = bot_y;
                tiles[nt].w = tw; tiles[nt].h = bot_h;
            }
        }
        nt++;
        if (nt >= 128) break;
    }

    for (int i = 0; i < nt; i++) {
        int fw = (int)tiles[i].w, fh = (int)tiles[i].h;
        if (fw < 150) fw = 150;
        if (fh < 150) fh = 150;
        tiling_place(server, tiles[i].t, tiles[i].x, tiles[i].y, fw, fh);
    }
}

static void server_arrange_tiles(struct sberry_server *server) {
    if (server->overview)
        return;
    if (server->mode != MODE_TILING)
        return;
    tiling_reset_camera(server);

    struct sberry_clearing *curr_cl = &server->clearings[server->current_clearing_idx];
    const int gap = GAP_SIZE;
    const int margin = MARGIN;

    /* Prefer exclusive-zone usable area so tiles sit below/above bars */
    int screen_w = (server->usable_w > 0) ? server->usable_w : server->screen_width;
    int screen_h = (server->usable_h > 0) ? server->usable_h : server->screen_height;
    int origin_x = (server->usable_w > 0) ? server->usable_x : 0;
    int origin_y = (server->usable_h > 0) ? server->usable_y : 0;

    /* head = newest → reverse = oldest first */
    struct sberry_toplevel *wins[128];
    int count = 0;
    struct sberry_toplevel *t;
    wl_list_for_each_reverse(t, &curr_cl->toplevels, clearing_link) {
        if (t->mapped && !t->fullscreen) {
            if (count < 128)
                wins[count++] = t;
        }
    }
    if (count == 0) return;

    double ax = origin_x + margin, ay = origin_y + margin;
    double aw = screen_w - 2 * margin, ah = screen_h - 2 * margin;

    if (server->tiling_layout == TILING_DWINDLE) {
        if (DWINDLE_SPAWN_SIDE == 0) {
            dwindle_layout_cursor(server, wins, count, ax, ay, aw, ah);
        } else if (DWINDLE_SPAWN_SIDE == 2) {
            /* mirror: newest first → left */
            struct sberry_toplevel *rev[128];
            for (int i = 0; i < count; i++)
                rev[i] = wins[count - 1 - i];
            dwindle_layout_arr(server, rev, count, ax, ay, aw, ah, 1);
        } else {
            /* 1 = left: oldest left, newest right leaf */
            dwindle_layout_arr(server, wins, count, ax, ay, aw, ah, 1);
        }
    } else {
        int master_count = 1;
        int stack_count = count - master_count;
        if (stack_count < 0) stack_count = 0;

        int usable_w = (int)aw;
        int usable_h = (int)ah;

        int master_w = (stack_count > 0) ? (int)(usable_w * 0.6) - gap / 2 : usable_w;
        int stack_w  = usable_w - master_w - gap;
        if (stack_w < 200) stack_w = 200;

        for (int i = 0; i < count; i++) {
            t = wins[i];
            int sx, sy, sw, sh;
            if (i < master_count) {
                sx = (int)ax; sy = (int)ay; sw = master_w; sh = usable_h;
            } else {
                int si = i - master_count;
                sh = (stack_count > 0)
                    ? (usable_h - (stack_count - 1) * gap) / stack_count
                    : usable_h;
                if (sh < 150) sh = 150;
                sx = (int)ax + master_w + gap;
                sy = (int)ay + si * (sh + gap);
                sw = stack_w;
            }
            tiling_place(server, t, sx, sy, sw, sh);
        }
    }
    publish_window_coords(server);
}

/* Always SSD — Qt/GTK/Electron must not draw CSD over our borders. */
static void decoration_force_ssd(struct wlr_xdg_toplevel_decoration_v1 *decoration) {
    if (!decoration || !decoration->toplevel || !decoration->toplevel->base)
        return;
    if (!decoration->toplevel->base->initialized)
        return;
    wlr_xdg_toplevel_decoration_v1_set_mode(decoration,
        WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void handle_decoration_request_mode(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_toplevel *t = wl_container_of(listener, t, decoration_request_mode);
    if (t->decoration)
        decoration_force_ssd(t->decoration);
}

static void handle_decoration_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_toplevel *t = wl_container_of(listener, t, decoration_destroy);
    if (t->decoration_request_mode.link.prev) {
        wl_list_remove(&t->decoration_request_mode.link);
        wl_list_init(&t->decoration_request_mode.link);
    }
    if (t->decoration_destroy.link.prev) {
        wl_list_remove(&t->decoration_destroy.link);
        wl_list_init(&t->decoration_destroy.link);
    }
    t->decoration = NULL;
}

static void handle_new_xdg_decoration(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_xdg_toplevel_decoration_v1 *decoration = data;
    struct sberry_toplevel *t = decoration->toplevel->base->data;
    if (!t)
        return;
    t->decoration = decoration;

    wl_list_init(&t->decoration_request_mode.link);
    wl_list_init(&t->decoration_destroy.link);
    t->decoration_request_mode.notify = handle_decoration_request_mode;
    wl_signal_add(&decoration->events.request_mode, &t->decoration_request_mode);
    t->decoration_destroy.notify = handle_decoration_destroy;
    wl_signal_add(&decoration->events.destroy, &t->decoration_destroy);

    /* If surface already configured, force SSD now (late decoration). */
    decoration_force_ssd(decoration);
}

/* Keep SSD border rects above client content so Qt/GTK/Chromium cannot
 * paint over them (CSD shadows, max buffers, subsurfaces). */
static void toplevel_borders_raise(struct sberry_toplevel *t) {
    if (!t)
        return;
    for (int i = 0; i < 4; i++) {
        if (t->border[i])
            wlr_scene_node_raise_to_top(&t->border[i]->node);
    }
}

static void toplevel_update_borders(struct sberry_toplevel *t) {
    if (!t || !t->mapped || !t->border[0]) return;

    if (t->fullscreen) {
        if (t->surface_tree)
            wlr_scene_node_set_position(&t->surface_tree->node, 0, 0);
        for (int i = 0; i < 4; i++) {
            if (t->border[i])
                wlr_scene_node_set_enabled(&t->border[i]->node, false);
        }
        return;
    }

    double scale = (t->visual_scale > 0.02) ? t->visual_scale : 1.0;
    int bw = BORDER_WIDTH;
    int w = (int)(t->width * scale + 0.5);
    int h = (int)(t->height * scale + 0.5);
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    /* Content strictly inside the frame: offset by border width */
    if (t->surface_tree)
        wlr_scene_node_set_position(&t->surface_tree->node, bw, bw);

    /* Frame: top, bottom, left, right — outside the content box */
    wlr_scene_rect_set_size(t->border[0], w + 2 * bw, bw);
    wlr_scene_node_set_position(&t->border[0]->node, 0, 0);

    wlr_scene_rect_set_size(t->border[1], w + 2 * bw, bw);
    wlr_scene_node_set_position(&t->border[1]->node, 0, h + bw);

    wlr_scene_rect_set_size(t->border[2], bw, h);
    wlr_scene_node_set_position(&t->border[2]->node, 0, bw);

    wlr_scene_rect_set_size(t->border[3], bw, h);
    wlr_scene_node_set_position(&t->border[3]->node, w + bw, bw);

    for (int i = 0; i < 4; i++) {
        if (t->border[i])
            wlr_scene_node_set_enabled(&t->border[i]->node, true);
    }

    const float *color = (t->server->focused_toplevel == t)
                         ? border_focused : border_unfocused;
    for (int i = 0; i < 4; i++) {
        wlr_scene_rect_set_color(t->border[i], color);
    }

    /* Always above client buffers so nothing paints over the frame */
    toplevel_borders_raise(t);
}

static void focus_toplevel(struct sberry_server *server, struct sberry_toplevel *t) {
    if (!t || !t->mapped) return;

    struct sberry_toplevel *old = server->focused_toplevel;
    server->focused_toplevel = t;

    if (old && old != t) {
        toplevel_update_borders(old);
        if (old->foreign_handle)
            wlr_foreign_toplevel_handle_v1_set_activated(old->foreign_handle, false);
        if (old->xwayland_surface)
            wlr_xwayland_surface_activate(old->xwayland_surface, false);
    }
    toplevel_update_borders(t);
    if (t->foreign_handle)
        wlr_foreign_toplevel_handle_v1_set_activated(t->foreign_handle, true);

    /* Raise in scene so hit-testing / paint order match focus */
    if (t->scene_tree)
        wlr_scene_node_raise_to_top(&t->scene_tree->node);

    if (t->xwayland_surface) {
        wlr_xwayland_surface_activate(t->xwayland_surface, true);
        /* Stack above siblings so X11 focus/input routing is correct */
        wlr_xwayland_surface_restack(t->xwayland_surface, NULL, XCB_STACK_MODE_ABOVE);
    }

    struct wlr_surface *surface = NULL;
    if (t->toplevel)
        surface = t->toplevel->base->surface;
    else if (t->xwayland_surface)
        surface = t->xwayland_surface->surface;
    if (!surface) return;

    struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
    if (kb) {
        wlr_seat_keyboard_notify_enter(server->seat, surface,
            kb->keycodes, kb->num_keycodes, &kb->modifiers);
    }

    /* Activate pending pointer constraint for this surface (games cursor lock) */
    if (server->pointer_constraints) {
        struct wlr_pointer_constraint_v1 *c =
            wlr_pointer_constraints_v1_constraint_for_surface(
                server->pointer_constraints, surface, server->seat);
        if (c)
            activate_constraint(server, c);
        else if (server->active_constraint)
            activate_constraint(server, NULL);
    }
}

static struct sberry_toplevel *find_neighbor(struct sberry_server *server,
        struct sberry_toplevel *cur, int dx, int dy) {
    struct sberry_clearing *curr_cl = &server->clearings[server->current_clearing_idx];
    if (!cur) {
        struct sberry_toplevel *t;
        wl_list_for_each(t, &curr_cl->toplevels, clearing_link) {
            if (t->mapped) return t;
        }
        return NULL;
    }

    double cx = cur->mx + (double)cur->width / 2.0;
    double cy = cur->my + (double)cur->height / 2.0;

    struct sberry_toplevel *best = NULL;
    double best_score = 1e18;

    struct sberry_toplevel *t;
    wl_list_for_each(t, &curr_cl->toplevels, clearing_link) {
        if (t == cur || !t->mapped) continue;

        double tx = t->mx + (double)t->width / 2.0;
        double ty = t->my + (double)t->height / 2.0;

        double ddx = tx - cx;
        double ddy = ty - cy;

        double proj = ddx * dx + ddy * dy;
        if (proj <= 0) continue;

        double perp = ddx * dy - ddy * dx;
        double score = proj + fabs(perp) * 2.0;

        if (score < best_score) {
            best_score = score;
            best = t;
        }
    }
    return best;
}

void sberry_kill_focused(struct sberry_server *server) {
    if (!server->focused_toplevel) return;
    struct sberry_toplevel *t = server->focused_toplevel;
#if ANIM_ENABLED && ANIM_CLOSE
    if (t->mapped && !t->closing && !t->fullscreen) {
        t->closing = true;
        t->anim_start_ms = now_ms();
        t->anim_zoom = false;
        if (!t->animating) {
            t->animating = true;
            server->animating_count++;
        }
        anim_timer_arm(server);
        return;
    }
#endif
    if (t->toplevel)
        wlr_xdg_toplevel_send_close(t->toplevel);
    else if (t->xwayland_surface)
        wlr_xwayland_surface_close(t->xwayland_surface);
}

static void cursor_update_for_zoom(struct sberry_server *server);

static void camera_zoom_at_cursor(struct sberry_server *server, double factor) {
    if (server->overview)
        return;
    if (server->mode == MODE_TILING)
        return; /* tiling camera is fixed */
    double mouse_wx = (server->cursor->x - server->screen_width  / 2.0)
                      / server->camera.zoom + server->camera.cx;
    double mouse_wy = (server->cursor->y - server->screen_height / 2.0)
                      / server->camera.zoom + server->camera.cy;

    server->camera.zoom *= factor;
    if (server->camera.zoom < MIN_ZOOM) server->camera.zoom = MIN_ZOOM;
    if (server->camera.zoom > MAX_ZOOM) server->camera.zoom = MAX_ZOOM;

    server->camera.cx = mouse_wx
        - (server->cursor->x - server->screen_width  / 2.0) / server->camera.zoom;
    server->camera.cy = mouse_wy
        - (server->cursor->y - server->screen_height / 2.0) / server->camera.zoom;

    camera_clamp(server);

    /* Persist canvas camera per-workspace so return restores zoom */
    if (server->mode == MODE_CANVAS) {
        struct sberry_clearing *cl = &server->clearings[server->current_clearing_idx];
        cl->canvas_cx = server->camera.cx;
        cl->canvas_cy = server->camera.cy;
        cl->canvas_zoom = server->camera.zoom;
        cl->canvas_saved = true;
        cl->saved_cx = server->camera.cx;
        cl->saved_cy = server->camera.cy;
        cl->saved_zoom = server->camera.zoom;
    }

    server_update_camera(server);
    cursor_update_for_zoom(server);
    publish_window_coords(server);
}

void sberry_zoom_in(struct sberry_server *s)  { camera_zoom_at_cursor(s, ZOOM_STEP); }
void sberry_zoom_out(struct sberry_server *s) { camera_zoom_at_cursor(s, 1.0 / ZOOM_STEP); }

/* Canvas only: keep camera inside the current clearing */
static void camera_clamp(struct sberry_server *server) {
    if (server->mode == MODE_TILING)
        return;
    struct sberry_clearing *cl = &server->clearings[server->current_clearing_idx];
    double margin = 200.0;
    double min_x = cl->base_mx - margin;
    double max_x = cl->base_mx + cl->width  + margin;
    double min_y = cl->base_my - margin;
    double max_y = cl->base_my + cl->height + margin;
    if (server->camera.cx < min_x) server->camera.cx = min_x;
    if (server->camera.cx > max_x) server->camera.cx = max_x;
    if (server->camera.cy < min_y) server->camera.cy = min_y;
    if (server->camera.cy > max_y) server->camera.cy = max_y;
}

/* Tiling always pins the camera to the clearing center at zoom 1.0 */
static void tiling_reset_camera(struct sberry_server *server) {
    struct sberry_clearing *cl = &server->clearings[server->current_clearing_idx];
    server->camera.cx = cl->base_mx + cl->width  / 2.0;
    server->camera.cy = cl->base_my + cl->height / 2.0;
    server->camera.zoom = 1.0;
}

/* driftwm-style zoom-to-fit — canvas only */
void sberry_zoom_fit(struct sberry_server *server) {
    if (server->mode == MODE_TILING)
        return;
    struct sberry_clearing *cl = &server->clearings[server->current_clearing_idx];
    double min_x = 1e18, min_y = 1e18, max_x = -1e18, max_y = -1e18;
    int n = 0;
    struct sberry_toplevel *t;
    wl_list_for_each(t, &cl->toplevels, clearing_link) {
        if (!t->mapped || t->fullscreen)
            continue;
        double wx, wy;
        toplevel_world_xy(server, t, &wx, &wy);
        double x0 = wx, y0 = wy;
        double x1 = wx + t->width, y1 = wy + t->height;
        if (x0 < min_x) min_x = x0;
        if (y0 < min_y) min_y = y0;
        if (x1 > max_x) max_x = x1;
        if (y1 > max_y) max_y = y1;
        n++;
    }
    if (n == 0) {
        min_x = cl->base_mx; min_y = cl->base_my;
        max_x = cl->base_mx + cl->width;
        max_y = cl->base_my + cl->height;
    }

    double pad = 80.0;
    min_x -= pad; min_y -= pad;
    max_x += pad; max_y += pad;
    double bw = max_x - min_x;
    double bh = max_y - min_y;
    if (bw < 100) bw = 100;
    if (bh < 100) bh = 100;

    double zx = (double)server->screen_width  / bw;
    double zy = (double)server->screen_height / bh;
    double z = (zx < zy ? zx : zy);
    if (z < MIN_ZOOM) z = MIN_ZOOM;
    if (z > MAX_ZOOM) z = MAX_ZOOM;

    server->camera.zoom = z;
    server->camera.cx = (min_x + max_x) / 2.0;
    server->camera.cy = (min_y + max_y) / 2.0;
    camera_clamp(server);
    if (server->mode == MODE_CANVAS) {
        struct sberry_clearing *cl = &server->clearings[server->current_clearing_idx];
        cl->canvas_cx = server->camera.cx;
        cl->canvas_cy = server->camera.cy;
        cl->canvas_zoom = server->camera.zoom;
        cl->canvas_saved = true;
        cl->saved_cx = server->camera.cx;
        cl->saved_cy = server->camera.cy;
        cl->saved_zoom = server->camera.zoom;
    }
    server_update_camera(server);
    cursor_update_for_zoom(server);
    publish_window_coords(server);
    wlr_log(WLR_INFO, "zoom-fit z=%.3f center=(%.0f,%.0f) windows=%d", z,
            server->camera.cx, server->camera.cy, n);
}

void sberry_zoom_reset(struct sberry_server *server) {
    if (server->mode == MODE_TILING) {
        tiling_reset_camera(server);
        server_arrange_tiles(server);
        publish_window_coords(server);
        return;
    }
    server->camera.zoom = 1.0;
    camera_clamp(server);
    {
        struct sberry_clearing *cl = &server->clearings[server->current_clearing_idx];
        cl->canvas_cx = server->camera.cx;
        cl->canvas_cy = server->camera.cy;
        cl->canvas_zoom = server->camera.zoom;
        cl->canvas_saved = true;
        cl->saved_cx = server->camera.cx;
        cl->saved_cy = server->camera.cy;
        cl->saved_zoom = server->camera.zoom;
    }
    server_update_camera(server);
    cursor_update_for_zoom(server);
    publish_window_coords(server);
}

void sberry_center_focused(struct sberry_server *server) {
    if (server->mode == MODE_TILING)
        return;
    struct sberry_toplevel *t = server->focused_toplevel;
    if (!t || !t->mapped)
        return;
    double wx, wy;
    toplevel_world_xy(server, t, &wx, &wy);
    server->camera.cx = wx + t->width  / 2.0;
    server->camera.cy = wy + t->height / 2.0;
    camera_clamp(server);
    server_update_camera(server);
    publish_window_coords(server);
}

void sberry_alt_tab(struct sberry_server *server) {
    struct sberry_clearing *cl = &server->clearings[server->current_clearing_idx];
    struct sberry_toplevel *cur = server->focused_toplevel;
    struct sberry_toplevel *first = NULL, *next = NULL;
    bool seen = false;
    struct sberry_toplevel *t;
    wl_list_for_each(t, &cl->toplevels, clearing_link) {
        if (!t->mapped)
            continue;
        if (!first)
            first = t;
        if (seen && !next)
            next = t;
        if (t == cur)
            seen = true;
    }
    struct sberry_toplevel *target = next ? next : first;
    if (!target)
        return;
    focus_toplevel(server, target);
    /* in canvas: gently center on the window */
    if (server->mode == MODE_CANVAS) {
        server->camera.cx = target->mx + target->width  / 2.0;
        server->camera.cy = target->my + target->height / 2.0;
        camera_clamp(server);
        server_update_camera(server);
        publish_window_coords(server);
    }
}

void sberry_alt_tab_prev(struct sberry_server *server) {
    struct sberry_clearing *cl = &server->clearings[server->current_clearing_idx];
    struct sberry_toplevel *cur = server->focused_toplevel;
    struct sberry_toplevel *prev = NULL, *last = NULL;
    struct sberry_toplevel *t;
    wl_list_for_each(t, &cl->toplevels, clearing_link) {
        if (!t->mapped)
            continue;
        if (t == cur && prev)
            break;
        if (t == cur)
            break;
        prev = t;
        last = t;
    }
    /* if cur was first, wrap to last */
    if (!prev) {
        wl_list_for_each(t, &cl->toplevels, clearing_link) {
            if (t->mapped)
                last = t;
        }
        prev = last;
    }
    if (!prev)
        return;
    focus_toplevel(server, prev);
    if (server->mode == MODE_CANVAS) {
        server->camera.cx = prev->mx + prev->width  / 2.0;
        server->camera.cy = prev->my + prev->height / 2.0;
        camera_clamp(server);
        server_update_camera(server);
        publish_window_coords(server);
    }
}


/* Keep window geometry inside its workspace (clearing) bounds. */
static struct sberry_clearing *
toplevel_get_clearing(struct sberry_server *server, struct sberry_toplevel *t) {
    if (!server || !t)
        return NULL;
    for (int i = 0; i < 9; i++) {
        struct sberry_toplevel *x;
        wl_list_for_each(x, &server->clearings[i].toplevels, clearing_link) {
            if (x == t)
                return &server->clearings[i];
        }
    }
    return &server->clearings[server->current_clearing_idx];
}

static void toplevel_clamp_to_clearing(struct sberry_server *server,
                                       struct sberry_toplevel *t) {
    if (!t || t->fullscreen)
        return;
    struct sberry_clearing *cl = toplevel_get_clearing(server, t);
    if (!cl)
        return;
    /* Local space: origin (0,0) = top-left of this workspace */
    double min_x = 0.0;
    double min_y = 0.0;
    double max_x = (double)cl->width;
    double max_y = (double)cl->height;
    if (t->width > cl->width)
        t->width = cl->width;
    if (t->height > cl->height)
        t->height = cl->height;
    if (t->width < 150)
        t->width = 150;
    if (t->height < 100)
        t->height = 100;
    if (t->mx < min_x)
        t->mx = min_x;
    if (t->my < min_y)
        t->my = min_y;
    if (t->mx + t->width > max_x)
        t->mx = max_x - t->width;
    if (t->my + t->height > max_y)
        t->my = max_y - t->height;
    if (t->mx < min_x)
        t->mx = min_x;
    if (t->my < min_y)
        t->my = min_y;
}

static void move_focused(struct sberry_server *server, double dx, double dy) {
    struct sberry_toplevel *t = server->focused_toplevel;
    if (!t) return;
    if (server->mode == MODE_TILING) {
        int sx = 0, sy = 0;
        if (dx < 0) sx = -1;
        else if (dx > 0) sx = 1;
        if (dy < 0) sy = -1;
        else if (dy > 0) sy = 1;
        swap_with_neighbor(server, sx, sy);
        return;
    }
    t->mx += dx;
    t->my += dy;
    toplevel_clamp_to_clearing(server, t);
    t->canvas_mx = t->mx;
    t->canvas_my = t->my;
    t->canvas_width  = t->width;
    t->canvas_height = t->height;
    t->has_canvas_pos = true;
    apply_camera(server, t);
    publish_window_coords(server);
}
void sberry_move_left(struct sberry_server *s)  { move_focused(s, -MOVE_STEP, 0); }
void sberry_move_right(struct sberry_server *s) { move_focused(s,  MOVE_STEP, 0); }
void sberry_move_up(struct sberry_server *s)    { move_focused(s, 0, -MOVE_STEP); }
void sberry_move_down(struct sberry_server *s)  { move_focused(s, 0,  MOVE_STEP); }

static void resize_focused(struct sberry_server *server, int dw, int dh) {
    struct sberry_toplevel *t = server->focused_toplevel;
    if (!t) return;
    t->width  += dw;
    t->height += dh;
    if (t->width  < 150) t->width  = 150;
    if (t->height < 100) t->height = 100;
    toplevel_clamp_to_clearing(server, t);
    if (server->mode == MODE_CANVAS) {
        t->canvas_mx = t->mx;
        t->canvas_my = t->my;
        t->canvas_width  = t->width;
        t->canvas_height = t->height;
        t->has_canvas_pos = true;
    }
    if (t->toplevel)
        wlr_xdg_toplevel_set_size(t->toplevel, t->width, t->height);
    else if (t->xwayland_surface)
        wlr_xwayland_surface_configure(t->xwayland_surface,
            (int16_t)t->mx, (int16_t)t->my, t->width, t->height);
    toplevel_update_borders(t);
    apply_camera(server, t);
    publish_window_coords(server);
}
void sberry_resize_left(struct sberry_server *s)  { resize_focused(s, -RESIZE_STEP, 0); }
void sberry_resize_right(struct sberry_server *s) { resize_focused(s,  RESIZE_STEP, 0); }
void sberry_resize_up(struct sberry_server *s)    { resize_focused(s, 0, -RESIZE_STEP); }
void sberry_resize_down(struct sberry_server *s)  { resize_focused(s, 0,  RESIZE_STEP); }

static void list_swap_nodes(struct wl_list *a, struct wl_list *b) {
    if (a == b)
        return;
    struct wl_list *ap = a->prev, *an = a->next;
    struct wl_list *bp = b->prev, *bn = b->next;
    if (an == b) {
        ap->next = b; b->prev = ap;
        b->next = a; a->prev = b;
        a->next = bn; bn->prev = a;
    } else if (bn == a) {
        bp->next = a; a->prev = bp;
        a->next = b; b->prev = a;
        b->next = an; an->prev = b;
    } else {
        ap->next = b; an->prev = b; b->prev = ap; b->next = an;
        bp->next = a; bn->prev = a; a->prev = bp; a->next = bn;
    }
}

static void swap_with_neighbor(struct sberry_server *server, int dx, int dy) {
    struct sberry_toplevel *cur = server->focused_toplevel;
    if (!cur) return;
    struct sberry_toplevel *other = find_neighbor(server, cur, dx, dy);
    if (!other) return;

    if (server->mode == MODE_TILING) {
        /* Swap list order so layout (dwindle/master) reflows, then animate */
        list_swap_nodes(&cur->clearing_link, &other->clearing_link);
        server_arrange_tiles(server);
        publish_window_coords(server);
        return;
    }

    /* Canvas: swap positions (ANIM_MOVE lerp or instant) */
    double cmx = cur->mx, cmy = cur->my;
    int cw = cur->width, ch = cur->height;
    double omx = other->mx, omy = other->my;
    int ow = other->width, oh = other->height;

#if ANIM_ENABLED && ANIM_MOVE
    toplevel_anim_to(cur, omx, omy, ow, oh);
    toplevel_anim_to(other, cmx, cmy, cw, ch);
#else
    cur->mx = omx; cur->my = omy; cur->width = ow; cur->height = oh;
    other->mx = cmx; other->my = cmy; other->width = cw; other->height = ch;
    if (cur->toplevel)
        wlr_xdg_toplevel_set_size(cur->toplevel, cur->width, cur->height);
    if (other->toplevel)
        wlr_xdg_toplevel_set_size(other->toplevel, other->width, other->height);
    toplevel_update_borders(cur);
    toplevel_update_borders(other);
    apply_camera(server, cur);
    apply_camera(server, other);
#endif

    cur->canvas_mx = omx; cur->canvas_my = omy;
    cur->canvas_width = ow; cur->canvas_height = oh;
    cur->has_canvas_pos = true;
    other->canvas_mx = cmx; other->canvas_my = cmy;
    other->canvas_width = cw; other->canvas_height = ch;
    other->has_canvas_pos = true;
    publish_window_coords(server);
}

void sberry_swap_left(struct sberry_server *s)  { swap_with_neighbor(s, -1, 0); }
void sberry_swap_right(struct sberry_server *s) { swap_with_neighbor(s,  1, 0); }
void sberry_swap_up(struct sberry_server *s)    { swap_with_neighbor(s, 0, -1); }
void sberry_swap_down(struct sberry_server *s)  { swap_with_neighbor(s, 0,  1); }

void sberry_focus_left(struct sberry_server *server) {
    focus_toplevel(server, find_neighbor(server, server->focused_toplevel, -1, 0));
}
void sberry_focus_right(struct sberry_server *server) {
    focus_toplevel(server, find_neighbor(server, server->focused_toplevel,  1, 0));
}
void sberry_focus_up(struct sberry_server *server) {
    focus_toplevel(server, find_neighbor(server, server->focused_toplevel, 0, -1));
}
void sberry_focus_down(struct sberry_server *server) {
    focus_toplevel(server, find_neighbor(server, server->focused_toplevel, 0,  1));
}

void sberry_toggle_mode(struct sberry_server *server) {
    struct sberry_clearing *curr_cl = &server->clearings[server->current_clearing_idx];
    struct sberry_toplevel *t;

    if (server->mode == MODE_TILING) {
        /* TILING -> CANVAS (this workspace only) */
        curr_cl->mode = MODE_CANVAS;
        server->mode = MODE_CANVAS;

        if (curr_cl->canvas_saved) {
            server->camera.cx   = curr_cl->canvas_cx;
            server->camera.cy   = curr_cl->canvas_cy;
            server->camera.zoom = curr_cl->canvas_zoom;
        }
        cursor_update_for_zoom(server);

        wl_list_for_each(t, &curr_cl->toplevels, clearing_link) {
            if (!t->mapped || t->fullscreen)
                continue;
            if (t->has_canvas_pos) {
                int nw = t->canvas_width  > 0 ? t->canvas_width  : t->width;
                int nh = t->canvas_height > 0 ? t->canvas_height : t->height;
                toplevel_anim_to(t, t->canvas_mx, t->canvas_my, nw, nh);
            } else {
                toplevel_update_borders(t);
                apply_camera(server, t);
            }
        }

        publish_window_coords(server);
        wlr_log(WLR_INFO, "ws %d mode: CANVAS", server->current_clearing_idx + 1);
    } else {
        /* CANVAS -> TILING (this workspace only) */
        curr_cl->canvas_cx   = server->camera.cx;
        curr_cl->canvas_cy   = server->camera.cy;
        curr_cl->canvas_zoom = server->camera.zoom;
        curr_cl->canvas_saved = true;

        wl_list_for_each(t, &curr_cl->toplevels, clearing_link) {
            if (!t->mapped || t->fullscreen)
                continue;
            t->canvas_mx = t->mx;
            t->canvas_my = t->my;
            t->canvas_width  = t->width;
            t->canvas_height = t->height;
            t->has_canvas_pos = true;
        }

        curr_cl->mode = MODE_TILING;
        server->mode = MODE_TILING;
        server_arrange_tiles(server);
        publish_window_coords(server);
        wlr_log(WLR_INFO, "ws %d mode: TILING", server->current_clearing_idx + 1);
    }
}

void sberry_toggle_layout(struct sberry_server *server) {
    if (server->tiling_layout == TILING_DWINDLE) {
        server->tiling_layout = TILING_MASTER_STACK;
        wlr_log(WLR_INFO, "Layout: Master-Stack");
    } else {
        server->tiling_layout = TILING_DWINDLE;
        wlr_log(WLR_INFO, "Layout: Dwindle");
    }
    if (server->mode == MODE_TILING) {
        server_arrange_tiles(server);
    }
}

static void sberry_switch_clearing(struct sberry_server *server, int idx) {
    if (idx < 0 || idx > 8)
        return;
    /* Workspace bind while overview is up → leave into that ws (don't leave
     * overview "stuck" on the old workspace). */
    if (server->overview || server->overview_leaving) {
        if (server->overview)
            overview_leave(server, idx);
        return;
    }
    if (idx == server->current_clearing_idx)
        return;

    struct sberry_clearing *old_cl = &server->clearings[server->current_clearing_idx];

    /* Snapshot camera for the workspace we leave */
    old_cl->saved_cx   = server->camera.cx;
    old_cl->saved_cy   = server->camera.cy;
    old_cl->saved_zoom = server->camera.zoom;
    old_cl->mode       = server->mode;
    if (server->mode == MODE_CANVAS) {
        old_cl->canvas_cx   = server->camera.cx;
        old_cl->canvas_cy   = server->camera.cy;
        old_cl->canvas_zoom = server->camera.zoom;
        old_cl->canvas_saved = true;
        /* freeze per-window canvas positions */
        struct sberry_toplevel *tw;
        wl_list_for_each(tw, &old_cl->toplevels, clearing_link) {
            if (!tw->mapped || tw->fullscreen)
                continue;
            tw->canvas_mx = tw->mx;
            tw->canvas_my = tw->my;
            tw->canvas_width  = tw->width;
            tw->canvas_height = tw->height;
            tw->has_canvas_pos = true;
        }
    }

    struct sberry_toplevel *t;
    wl_list_for_each(t, &old_cl->toplevels, clearing_link) {
        if (t->scene_tree)
            wlr_scene_node_set_enabled(&t->scene_tree->node, false);
    }

    server->current_clearing_idx = idx;
    struct sberry_clearing *curr_cl = &server->clearings[idx];

    /* Restore this workspace's mode */
    server->mode = curr_cl->mode;

    if (curr_cl->visited) {
        if (curr_cl->mode == MODE_CANVAS && curr_cl->canvas_saved) {
            server->camera.cx   = curr_cl->canvas_cx;
            server->camera.cy   = curr_cl->canvas_cy;
            server->camera.zoom = curr_cl->canvas_zoom;
        } else {
            server->camera.cx   = curr_cl->saved_cx;
            server->camera.cy   = curr_cl->saved_cy;
            server->camera.zoom = curr_cl->saved_zoom;
        }
    } else {
        server->camera.cx = curr_cl->base_mx + curr_cl->width  / 2.0;
        server->camera.cy = curr_cl->base_my + curr_cl->height / 2.0;
        double zoom_x = (double)server->screen_width  / curr_cl->width;
        double zoom_y = (double)server->screen_height / curr_cl->height;
        server->camera.zoom = (zoom_x < zoom_y ? zoom_x : zoom_y) * 0.95;
        if (server->camera.zoom < MIN_ZOOM) server->camera.zoom = MIN_ZOOM;
        if (server->camera.zoom > MAX_ZOOM) server->camera.zoom = MAX_ZOOM;
        curr_cl->visited = true;
        curr_cl->saved_cx = server->camera.cx;
        curr_cl->saved_cy = server->camera.cy;
        curr_cl->saved_zoom = server->camera.zoom;
    }

    server->focused_toplevel = NULL;
    wl_list_for_each(t, &curr_cl->toplevels, clearing_link) {
        if (t->scene_tree)
            wlr_scene_node_set_enabled(&t->scene_tree->node, true);
        if (t->mapped) {
            apply_camera(server, t);
            if (!server->focused_toplevel)
                focus_toplevel(server, t);
        }
    }

    if (server->mode == MODE_TILING)
        server_arrange_tiles(server);
    else
        server_update_camera(server);
    cursor_update_for_zoom(server);

    publish_window_coords(server);
    wlr_log(WLR_INFO, "Clearing/workspace: %d (%s)", idx + 1,
            server->mode == MODE_TILING ? "tiling" : "canvas");
}



/* ========== Overview: driftwm-style camera zoom-to-fit over 3×3 clearings ==========
 * Windows keep native size (no set_size). dest_size = camera.zoom downscales
 * full-res buffers (compositor-side, like driftwm). Mode stays separate.
 */

static void overview_fit_camera(struct sberry_server *server,
                                double *out_cx, double *out_cy, double *out_zoom) {
    double min_x = 1e18, min_y = 1e18, max_x = -1e18, max_y = -1e18;
    for (int i = 0; i < 9; i++) {
        struct sberry_clearing *cl = &server->clearings[i];
        if (cl->base_mx < min_x) min_x = cl->base_mx;
        if (cl->base_my < min_y) min_y = cl->base_my;
        if (cl->base_mx + cl->width > max_x) max_x = cl->base_mx + cl->width;
        if (cl->base_my + cl->height > max_y) max_y = cl->base_my + cl->height;
    }
    double world_w = max_x - min_x;
    double world_h = max_y - min_y;
    if (world_w < 1.0) world_w = 1.0;
    if (world_h < 1.0) world_h = 1.0;

    int base_w = server->usable_w > 0 ? server->usable_w : server->screen_width;
    int base_h = server->usable_h > 0 ? server->usable_h : server->screen_height;
    int pad = OVERVIEW_PAD;
    if (pad < 24) pad = 24; /* keep cells off screen edges */
    double view_w = base_w - 2 * pad;
    double view_h = base_h - 2 * pad;
    if (view_w < 40) view_w = 40;
    if (view_h < 30) view_h = 30;

    double zx = view_w / world_w;
    double zy = view_h / world_h;
    double z = (zx < zy) ? zx : zy;
    z *= 0.92;
    if (z < MIN_ZOOM) z = MIN_ZOOM;
    if (z > 1.0) z = 1.0; /* overview only zooms out */

    *out_cx = (min_x + max_x) * 0.5;
    *out_cy = (min_y + max_y) * 0.5;
    *out_zoom = z;
}

/* Project clearing i's world rect through current camera → screen */
static void overview_cell_screen(struct sberry_server *server, int i,
                                 int *ox, int *oy, int *ow, int *oh) {
    struct sberry_clearing *cl = &server->clearings[i];
    double z = server->camera.zoom;
    if (z < 0.01) z = 0.01;
    double sx0 = (cl->base_mx - server->camera.cx) * z + server->screen_width / 2.0;
    double sy0 = (cl->base_my - server->camera.cy) * z + server->screen_height / 2.0;
    double sx1 = (cl->base_mx + cl->width - server->camera.cx) * z
        + server->screen_width / 2.0;
    double sy1 = (cl->base_my + cl->height - server->camera.cy) * z
        + server->screen_height / 2.0;
    *ox = (int)lround(sx0);
    *oy = (int)lround(sy0);
    *ow = (int)lround(sx1 - sx0);
    *oh = (int)lround(sy1 - sy0);
    if (*ow < 4) *ow = 4;
    if (*oh < 4) *oh = 4;
}

/* Position + dest_size=zoom for every mapped window (drift-style). */
static void overview_apply_windows(struct sberry_server *server) {
    for (int i = 0; i < 9; i++)
        overview_layout_clearing(server, i);
}

static int overview_cell_at(struct sberry_server *server, double sx, double sy) {
    for (int i = 0; i < 9; i++) {
        int ox, oy, ow, oh;
        overview_cell_screen(server, i, &ox, &oy, &ow, &oh);
        if (sx >= ox && sx < ox + ow && sy >= oy && sy < oy + oh)
            return i;
    }
    return -1;
}

static void overview_set_border_visible(struct sberry_server *server, int idx, bool on) {
    if (idx < 0 || idx > 8) return;
    for (int e = 0; e < 4; e++) {
        if (server->overview_border[idx][e])
            wlr_scene_node_set_enabled(&server->overview_border[idx][e]->node, on);
    }
}

static void overview_place_border_screen(struct sberry_server *server, int i) {
    int x0, y0, w, h;
    overview_cell_screen(server, i, &x0, &y0, &w, &h);
    int bw = OVERVIEW_BORDER;
    if (w < 4) w = 4;
    if (h < 4) h = 4;
    /* top */
    wlr_scene_rect_set_size(server->overview_border[i][0], w, bw);
    wlr_scene_node_set_position(&server->overview_border[i][0]->node, x0, y0);
    /* bottom */
    wlr_scene_rect_set_size(server->overview_border[i][1], w, bw);
    wlr_scene_node_set_position(&server->overview_border[i][1]->node, x0, y0 + h - bw);
    /* left */
    wlr_scene_rect_set_size(server->overview_border[i][2], bw, h);
    wlr_scene_node_set_position(&server->overview_border[i][2]->node, x0, y0);
    /* right */
    wlr_scene_rect_set_size(server->overview_border[i][3], bw, h);
    wlr_scene_node_set_position(&server->overview_border[i][3]->node, x0 + w - bw, y0);
}

static void overview_update_hover(struct sberry_server *server) {
    if (!server->overview) return;
    int cell = overview_cell_at(server, server->cursor->x, server->cursor->y);
    if (cell == server->overview_hover)
        return;
    if (server->overview_hover >= 0)
        overview_set_border_visible(server, server->overview_hover, false);
    server->overview_hover = cell;
    if (cell >= 0) {
        overview_place_border_screen(server, cell);
        overview_set_border_visible(server, cell, true);
    }
}

/* Per-clearing apply for camera-zoom overview.
 * dest_size = zoom * inset so content stays strictly inside the cell frame. */
static void overview_layout_clearing(struct sberry_server *server, int ci) {
    if (ci < 0 || ci > 8)
        return;
    double z = server->camera.zoom;
    if (z < 0.02) z = 0.02;
    if (z > 1.0) z = 1.0;
    /* Content scale slightly under camera zoom → air between window and cell edge */
    double inset = 0.90;
    double sc = z * inset;
    if (sc < 0.02) sc = 0.02;

    int cell_x, cell_y, cell_w, cell_h;
    overview_cell_screen(server, ci, &cell_x, &cell_y, &cell_w, &cell_h);
    int pad = OVERVIEW_BORDER + 6;
    if (pad < 8) pad = 8;
    double box_x0 = cell_x + pad;
    double box_y0 = cell_y + pad;
    double box_x1 = cell_x + cell_w - pad;
    double box_y1 = cell_y + cell_h - pad;
    if (box_x1 <= box_x0 + 8) {
        box_x0 = cell_x + 2;
        box_x1 = cell_x + cell_w - 2;
    }
    if (box_y1 <= box_y0 + 8) {
        box_y0 = cell_y + 2;
        box_y1 = cell_y + cell_h - 2;
    }

    struct sberry_toplevel *t;
    wl_list_for_each(t, &server->clearings[ci].toplevels, clearing_link) {
        if (!t->mapped || !t->scene_tree)
            continue;
        if (t->ov_saved) {
            if (t->fullscreen) {
                /* Keep client FS size; only local origin for camera place */
                t->mx = t->ov_mx;
                t->my = t->ov_my;
            } else if (t->width != t->ov_w || t->height != t->ov_h) {
                t->width = t->ov_w;
                t->height = t->ov_h;
                t->mx = t->ov_mx;
                t->my = t->ov_my;
                if (t->toplevel)
                    wlr_xdg_toplevel_set_size(t->toplevel, t->ov_w, t->ov_h);
                else if (t->xwayland_surface)
                    wlr_xwayland_surface_configure(t->xwayland_surface,
                        (int16_t)t->ov_mx, (int16_t)t->ov_my, t->ov_w, t->ov_h);
            }
        }
        wlr_scene_node_set_enabled(&t->scene_tree->node, true);
        apply_camera(server, t);

        /* FS buffer is screen-sized — scale to fit the cell box, not raw zoom */
        double use_sc = sc;
        if (t->fullscreen) {
            double bw = (double)(t->width > 0 ? t->width : 1);
            double bh = (double)(t->height > 0 ? t->height : 1);
            double fit_x = (box_x1 - box_x0) / bw;
            double fit_y = (box_y1 - box_y0) / bh;
            double fit = fit_x < fit_y ? fit_x : fit_y;
            if (fit < 0.02) fit = 0.02;
            if (fit > 1.0) fit = 1.0;
            use_sc = fit;
        }
        toplevel_apply_visual_scale(t, use_sc);

        /* Hard clamp screen rect of scaled window into the inset cell box */
        double vis_w = (double)t->width * use_sc;
        double vis_h = (double)t->height * use_sc;
        if (vis_w < 1.0) vis_w = 1.0;
        if (vis_h < 1.0) vis_h = 1.0;
        double sx = (double)t->scene_tree->node.x;
        double sy = (double)t->scene_tree->node.y;
        if (sx < box_x0) sx = box_x0;
        if (sy < box_y0) sy = box_y0;
        if (sx + vis_w > box_x1) sx = box_x1 - vis_w;
        if (sy + vis_h > box_y1) sy = box_y1 - vis_h;
        /* If still larger than box, pin top-left and let scale handle the rest */
        if (sx < box_x0) sx = box_x0;
        if (sy < box_y0) sy = box_y0;
        wlr_scene_node_set_position(&t->scene_tree->node,
            (int)lround(sx), (int)lround(sy));
    }
}

static void overview_ensure_borders(struct sberry_server *server) {
    if (server->overview_tree)
        return;
    server->overview_tree = wlr_scene_tree_create(server->toplevel_tree);
    static const float col[] = { 0.90f, 0.55f, 0.15f, 1.0f };
    for (int i = 0; i < 9; i++) {
        for (int e = 0; e < 4; e++) {
            server->overview_border[i][e] = wlr_scene_rect_create(
                server->overview_tree, 1, 1, col);
            wlr_scene_node_set_enabled(&server->overview_border[i][e]->node, false);
        }
    }
}

static void overview_enter(struct sberry_server *server) {
    if (server->overview || server->overview_leaving)
        return;
    server->overview = true;
    server->overview_hover = -1;
    server->overview_save_cx = server->camera.cx;
    server->overview_save_cy = server->camera.cy;
    server->overview_save_zoom = server->camera.zoom;
    server->panning = false;
    server->grabbed_toplevel = NULL;
    server->resizing = false;
    server->overview_cam_anim = false;

    if (server->active_constraint)
        activate_constraint(server, NULL);

    overview_ensure_borders(server);
    overview_set_popups_visible(server, false);

    /* Snapshot native geometry (no set_size — dest_size only).
     * Fullscreen windows also appear in their workspace cell: use pre-FS
     * geometry for layout, reparent under toplevel_tree so they are not
     * stuck at screen (0,0) above the overview grid. */
    for (int i = 0; i < 9; i++) {
        struct sberry_clearing *cl = &server->clearings[i];
        struct sberry_toplevel *t;
        wl_list_for_each(t, &cl->toplevels, clearing_link) {
            if (!t->mapped)
                continue;
            if (t->fullscreen) {
                /* Preview as filling the workspace (client stays FS-sized;
                 * dest_size scale shrinks the buffer into the cell). */
                t->ov_mx = 0;
                t->ov_my = 0;
                t->ov_w = cl->width > 0 ? cl->width : t->width;
                t->ov_h = cl->height > 0 ? cl->height : t->height;
                t->mx = t->ov_mx;
                t->my = t->ov_my;
                /* keep t->width/height = screen — only layout coords change */
                if (t->scene_tree && server->toplevel_tree) {
                    wlr_scene_node_reparent(&t->scene_tree->node,
                                           server->toplevel_tree);
                    wlr_scene_node_set_enabled(&t->scene_tree->node, true);
                }
                /* show borders slightly so FS is readable as a window tile */
                for (int bi = 0; bi < 4; bi++) {
                    if (t->border[bi])
                        wlr_scene_node_set_enabled(&t->border[bi]->node, true);
                }
                if (t->surface_tree)
                    wlr_scene_node_set_position(&t->surface_tree->node,
                                                BORDER_WIDTH, BORDER_WIDTH);
            } else {
                t->ov_mx = t->mx;
                t->ov_my = t->my;
                t->ov_w = t->width;
                t->ov_h = t->height;
            }
            t->ov_saved = true;
            t->ov_animating = false;
            t->ov_leaving = false;
        }
    }

    double to_cx, to_cy, to_zoom;
    overview_fit_camera(server, &to_cx, &to_cy, &to_zoom);

#if ANIM_ENABLED && ANIM_OVERVIEW
    server->ov_cam_from_cx = server->camera.cx;
    server->ov_cam_from_cy = server->camera.cy;
    server->ov_cam_from_zoom = server->camera.zoom;
    server->ov_cam_to_cx = to_cx;
    server->ov_cam_to_cy = to_cy;
    server->ov_cam_to_zoom = to_zoom;
    server->ov_cam_start_ms = now_ms();
    server->overview_cam_anim = true;
    if (server->animating_count < 1)
        server->animating_count = 1;
    anim_timer_arm(server);
#else
    server->camera.cx = to_cx;
    server->camera.cy = to_cy;
    server->camera.zoom = to_zoom;
    overview_apply_windows(server);
#endif

    for (int i = 0; i < 9; i++) {
        overview_place_border_screen(server, i);
        overview_set_border_visible(server, i, false);
    }
    overview_update_hover(server);
    publish_window_coords(server);
    wlr_log(WLR_INFO, "overview: enter (camera zoom-to-fit, drift-style)");
}

static void overview_leave(struct sberry_server *server, int goto_idx) {
    if (!server->overview || server->overview_leaving)
        return;

    int target = (goto_idx >= 0 && goto_idx <= 8) ? goto_idx : server->current_clearing_idx;

    for (int i = 0; i < 9; i++)
        overview_set_border_visible(server, i, false);
    server->overview_hover = -1;
    server->overview = false;
    server->overview_leaving = true;
    server->overview_leave_target = target;

    struct sberry_clearing *tcl = &server->clearings[target];
    double tcx, tcy, tzoom;
    if (target == server->current_clearing_idx) {
        tcx = server->overview_save_cx;
        tcy = server->overview_save_cy;
        tzoom = server->overview_save_zoom;
    } else if (tcl->visited) {
        if (tcl->mode == MODE_CANVAS && tcl->canvas_saved) {
            tcx = tcl->canvas_cx;
            tcy = tcl->canvas_cy;
            tzoom = tcl->canvas_zoom;
        } else {
            tcx = tcl->saved_cx;
            tcy = tcl->saved_cy;
            tzoom = tcl->saved_zoom;
        }
    } else {
        tcx = tcl->base_mx + tcl->width / 2.0;
        tcy = tcl->base_my + tcl->height / 2.0;
        double zx = (double)server->screen_width / tcl->width;
        double zy = (double)server->screen_height / tcl->height;
        tzoom = (zx < zy ? zx : zy) * 0.95;
        if (tzoom < MIN_ZOOM) tzoom = MIN_ZOOM;
        if (tzoom > MAX_ZOOM) tzoom = MAX_ZOOM;
    }

    if (target != server->current_clearing_idx) {
        struct sberry_clearing *old_cl = &server->clearings[server->current_clearing_idx];
        old_cl->saved_cx = server->overview_save_cx;
        old_cl->saved_cy = server->overview_save_cy;
        old_cl->saved_zoom = server->overview_save_zoom;
        old_cl->mode = server->mode;
        server->current_clearing_idx = target;
        server->mode = tcl->mode;
        tcl->visited = true;
        tcl->saved_cx = tcx;
        tcl->saved_cy = tcy;
        tcl->saved_zoom = tzoom;
    }

    server->focused_toplevel = NULL;

#if ANIM_ENABLED && ANIM_OVERVIEW
    /* Animate camera from current overview view into target ws (drift-style) */
    server->ov_cam_from_cx = server->camera.cx;
    server->ov_cam_from_cy = server->camera.cy;
    server->ov_cam_from_zoom = server->camera.zoom;
    server->ov_cam_to_cx = tcx;
    server->ov_cam_to_cy = tcy;
    server->ov_cam_to_zoom = tzoom;
    server->ov_cam_start_ms = now_ms();
    server->overview_cam_anim = true;
    if (server->animating_count < 1)
        server->animating_count = 1;
    anim_timer_arm(server);
    wlr_log(WLR_INFO, "overview: leave anim → ws %d (camera zoom)", target + 1);
    return;
#endif

    server->camera.cx = tcx;
    server->camera.cy = tcy;
    server->camera.zoom = tzoom;
    server->overview_leaving = false;
    overview_set_popups_visible(server, true);
    for (int i = 0; i < 9; i++) {
        struct sberry_toplevel *t;
        wl_list_for_each(t, &server->clearings[i].toplevels, clearing_link) {
            t->ov_animating = false;
            t->ov_leaving = false;
            if (t->fullscreen) {
                int sw = server->screen_width > 0 ? server->screen_width : 1920;
                int sh = server->screen_height > 0 ? server->screen_height : 1080;
                t->mx = 0;
                t->my = 0;
                t->width = sw;
                t->height = sh;
                t->ov_saved = false;
                for (int bi = 0; bi < 4; bi++) {
                    if (t->border[bi])
                        wlr_scene_node_set_enabled(&t->border[bi]->node, false);
                }
                if (t->surface_tree)
                    wlr_scene_node_set_position(&t->surface_tree->node, 0, 0);
                if (t->scene_tree && server->fullscreen_tree) {
                    wlr_scene_node_reparent(&t->scene_tree->node,
                                            server->fullscreen_tree);
                    wlr_scene_node_set_enabled(&t->scene_tree->node,
                        i == target && t->mapped);
                    if (i == target && t->mapped)
                        wlr_scene_node_raise_to_top(&t->scene_tree->node);
                }
                toplevel_apply_visual_scale(t, 1.0);
                if (i == target && t->mapped)
                    apply_camera(server, t);
                continue;
            }
            if (t->ov_saved) {
                t->mx = t->ov_mx;
                t->my = t->ov_my;
                t->width = t->ov_w;
                t->height = t->ov_h;
                t->ov_saved = false;
            }
            toplevel_apply_visual_scale(t, 1.0);
            toplevel_update_borders(t);
            if (t->scene_tree)
                wlr_scene_node_set_enabled(&t->scene_tree->node, i == target && t->mapped);
            if (i == target && t->mapped)
                apply_camera(server, t);
        }
    }
    if (server->mode == MODE_TILING)
        server_arrange_tiles(server);
    else
        server_update_camera(server);
    {
        struct sberry_toplevel *t;
        wl_list_for_each(t, &server->clearings[target].toplevels, clearing_link) {
            if (t->mapped) {
                focus_toplevel(server, t);
                break;
            }
        }
    }
    publish_window_coords(server);
    wlr_log(WLR_INFO, "overview: leave → ws %d", target + 1);
}


void sberry_toggle_overview(struct sberry_server *server) {
    if (server->overview_leaving)
        return; /* wait for exit anim */
    if (server->overview)
        overview_leave(server, -1);
    else
        overview_enter(server);
}

void sberry_ws1(struct sberry_server *s) { sberry_switch_clearing(s, 0); }
void sberry_ws2(struct sberry_server *s) { sberry_switch_clearing(s, 1); }
void sberry_ws3(struct sberry_server *s) { sberry_switch_clearing(s, 2); }
void sberry_ws4(struct sberry_server *s) { sberry_switch_clearing(s, 3); }
void sberry_ws5(struct sberry_server *s) { sberry_switch_clearing(s, 4); }
void sberry_ws6(struct sberry_server *s) { sberry_switch_clearing(s, 5); }
void sberry_ws7(struct sberry_server *s) { sberry_switch_clearing(s, 6); }
void sberry_ws8(struct sberry_server *s) { sberry_switch_clearing(s, 7); }
void sberry_ws9(struct sberry_server *s) { sberry_switch_clearing(s, 8); }

void sberry_ws_next(struct sberry_server *s) {
    int n = s->current_clearing_idx + 1;
    if (n > 8) n = 0;
    sberry_switch_clearing(s, n);
}
void sberry_ws_prev(struct sberry_server *s) {
    int n = s->current_clearing_idx - 1;
    if (n < 0) n = 8;
    sberry_switch_clearing(s, n);
}

static void sberry_move_to_clearing(struct sberry_server *server, int idx) {
    if (idx < 0 || idx > 8)
        return;
    struct sberry_toplevel *t = server->focused_toplevel;
    if (!t || !t->mapped)
        return;
    if (idx == server->current_clearing_idx)
        return;

    struct sberry_clearing *old_cl = &server->clearings[server->current_clearing_idx];
    struct sberry_clearing *new_cl = &server->clearings[idx];

    /* detach from current clearing list */
    wl_list_remove(&t->clearing_link);
    wl_list_insert(&new_cl->toplevels, &t->clearing_link);

    /* hide on old workspace view; will show when we switch or if we stay */
    if (t->scene_tree)
        wlr_scene_node_set_enabled(&t->scene_tree->node, false);

    /* local origin (0,0) of destination ws — center the window there */
    t->mx = (double)new_cl->width  / 2.0 - t->width  / 2.0;
    t->my = (double)new_cl->height / 2.0 - t->height / 2.0;
    if (t->mx < 0) t->mx = 0;
    if (t->my < 0) t->my = 0;
    if (t->has_canvas_pos) {
        t->canvas_mx = t->mx;
        t->canvas_my = t->my;
    }

    /* unfocus here; pick another on current clearing */
    server->focused_toplevel = NULL;
    struct sberry_toplevel *next;
    wl_list_for_each(next, &old_cl->toplevels, clearing_link) {
        if (next->mapped) {
            focus_toplevel(server, next);
            break;
        }
    }

    if (server->mode == MODE_TILING)
        server_arrange_tiles(server);

    /* follow the window to the target workspace */
    sberry_switch_clearing(server, idx);
    /* ensure it is visible + focused after switch */
    if (t->scene_tree)
        wlr_scene_node_set_enabled(&t->scene_tree->node, true);
    focus_toplevel(server, t);
    apply_camera(server, t);
    if (server->mode == MODE_TILING)
        server_arrange_tiles(server);

    publish_window_coords(server);
    wlr_log(WLR_INFO, "Moved window to clearing %d", idx + 1);
}

void sberry_move_ws1(struct sberry_server *s) { sberry_move_to_clearing(s, 0); }
void sberry_move_ws2(struct sberry_server *s) { sberry_move_to_clearing(s, 1); }
void sberry_move_ws3(struct sberry_server *s) { sberry_move_to_clearing(s, 2); }
void sberry_move_ws4(struct sberry_server *s) { sberry_move_to_clearing(s, 3); }
void sberry_move_ws5(struct sberry_server *s) { sberry_move_to_clearing(s, 4); }
void sberry_move_ws6(struct sberry_server *s) { sberry_move_to_clearing(s, 5); }
void sberry_move_ws7(struct sberry_server *s) { sberry_move_to_clearing(s, 6); }
void sberry_move_ws8(struct sberry_server *s) { sberry_move_to_clearing(s, 7); }
void sberry_move_ws9(struct sberry_server *s) { sberry_move_to_clearing(s, 8); }

static void toplevel_set_fullscreen(struct sberry_toplevel *t, bool fs) {
    if (!t || !t->server || t->fullscreen == fs)
        return;
    struct sberry_server *server = t->server;

    /* Abort in-flight soft anim — set_size mid-anim + fullscreen confuses Qt/Telegram */
    if (t->animating) {
        t->animating = false;
        t->anim_zoom = false;
        if (server->animating_count > 0)
            server->animating_count--;
        toplevel_apply_visual_scale(t, 1.0);
    }

    if (fs) {
        if (t->mapped) {
            t->fs_save_mx = t->mx;
            t->fs_save_my = t->my;
            t->fs_save_w  = t->width > 0 ? t->width : 400;
            t->fs_save_h  = t->height > 0 ? t->height : 300;
        } else {
            t->fs_save_mx = 0;
            t->fs_save_my = 0;
            t->fs_save_w  = 400;
            t->fs_save_h  = 300;
        }
        t->fullscreen = true;

        int sw = server->screen_width > 0 ? server->screen_width : 1920;
        int sh = server->screen_height > 0 ? server->screen_height : 1080;
        t->width  = sw;
        t->height = sh;
        /* World coords unused while fullscreen — apply_camera pins to 0,0 */
        t->mx = 0;
        t->my = 0;

        if (t->toplevel && t->toplevel->base && t->toplevel->base->initialized) {
            wlr_xdg_toplevel_set_fullscreen(t->toplevel, true);
            wlr_xdg_toplevel_set_size(t->toplevel, sw, sh);
        }
        if (t->xwayland_surface) {
            wlr_xwayland_surface_set_fullscreen(t->xwayland_surface, true);
            wlr_xwayland_surface_configure(t->xwayland_surface, 0, 0, sw, sh);
        }

        for (int i = 0; i < 4; i++) {
            if (t->border[i])
                wlr_scene_node_set_enabled(&t->border[i]->node, false);
        }
        if (t->surface_tree)
            wlr_scene_node_set_position(&t->surface_tree->node, 0, 0);

        if (t->scene_tree) {
            if (server->overview || server->overview_leaving) {
                /* Stay in overview grid: parent under toplevel_tree, fill cell */
                if (server->toplevel_tree)
                    wlr_scene_node_reparent(&t->scene_tree->node, server->toplevel_tree);
                {
                    struct sberry_clearing *cl =
                        &server->clearings[server->current_clearing_idx];
                    t->mx = 0;
                    t->my = 0;
                    t->ov_mx = 0;
                    t->ov_my = 0;
                    t->ov_w = cl->width > 0 ? cl->width : t->width;
                    t->ov_h = cl->height > 0 ? cl->height : t->height;
                    t->ov_saved = true;
                }
                wlr_scene_node_set_enabled(&t->scene_tree->node, true);
            } else if (server->fullscreen_tree) {
                wlr_scene_node_reparent(&t->scene_tree->node, server->fullscreen_tree);
                wlr_scene_node_set_enabled(&t->scene_tree->node, true);
                wlr_scene_node_raise_to_top(&t->scene_tree->node);
            }
        }
        apply_camera(server, t);

        /* Auto default zoom: pin camera to current ws center + fit screen.
         * Applies to Super+f and client-requested fullscreen alike. */
        if (!server->overview && !server->overview_leaving) {
            struct sberry_clearing *cl =
                &server->clearings[server->current_clearing_idx];
            server->camera.cx = cl->base_mx + cl->width  / 2.0;
            server->camera.cy = cl->base_my + cl->height / 2.0;
            double zx = (double)server->screen_width  / (double)cl->width;
            double zy = (double)server->screen_height / (double)cl->height;
            double z = (zx < zy ? zx : zy) * 0.95;
            if (z < MIN_ZOOM) z = MIN_ZOOM;
            if (z > MAX_ZOOM) z = MAX_ZOOM;
            server->camera.zoom = z;
            cl->saved_cx = server->camera.cx;
            cl->saved_cy = server->camera.cy;
            cl->saved_zoom = server->camera.zoom;
            if (server->mode == MODE_CANVAS) {
                cl->canvas_cx = server->camera.cx;
                cl->canvas_cy = server->camera.cy;
                cl->canvas_zoom = server->camera.zoom;
                cl->canvas_saved = true;
            }
            server_update_camera(server);
            apply_camera(server, t);
        }

        if (t->foreign_handle)
            wlr_foreign_toplevel_handle_v1_set_fullscreen(t->foreign_handle, true);
    } else {
        t->fullscreen = false;
        t->mx = t->fs_save_mx;
        t->my = t->fs_save_my;
        t->width  = t->fs_save_w > 0 ? t->fs_save_w : 400;
        t->height = t->fs_save_h > 0 ? t->fs_save_h : 300;

        if (t->toplevel && t->toplevel->base && t->toplevel->base->initialized) {
            wlr_xdg_toplevel_set_fullscreen(t->toplevel, false);
            wlr_xdg_toplevel_set_size(t->toplevel, t->width, t->height);
        }
        if (t->xwayland_surface) {
            wlr_xwayland_surface_set_fullscreen(t->xwayland_surface, false);
            if (t->mapped)
                wlr_xwayland_surface_configure(t->xwayland_surface,
                    (int16_t)t->mx, (int16_t)t->my, t->width, t->height);
        }

        for (int i = 0; i < 4; i++) {
            if (t->border[i])
                wlr_scene_node_set_enabled(&t->border[i]->node, true);
        }
        if (t->surface_tree)
            wlr_scene_node_set_position(&t->surface_tree->node, BORDER_WIDTH, BORDER_WIDTH);

        if (t->scene_tree && server->toplevel_tree) {
            wlr_scene_node_reparent(&t->scene_tree->node, server->toplevel_tree);
            wlr_scene_node_set_enabled(&t->scene_tree->node, true);
        }
        toplevel_update_borders(t);
        apply_camera(server, t);

        if (t->foreign_handle)
            wlr_foreign_toplevel_handle_v1_set_fullscreen(t->foreign_handle, false);

        if (server->mode == MODE_TILING && !server->overview)
            server_arrange_tiles(server);
    }
    publish_window_coords(server);
}

void sberry_toggle_fullscreen(struct sberry_server *server) {
    struct sberry_toplevel *t = server->focused_toplevel;
    if (!t || !t->mapped)
        return;
    if (server->overview)
        return;
    toplevel_set_fullscreen(t, !t->fullscreen);
}

static void handle_request_move(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_toplevel *t = wl_container_of(listener, t, request_move);
    if (!t || !t->server)
        return;
    /* Client CSD titlebar drag — canvas free move; tiling: ignore (use Super) */
    if (t->server->mode != MODE_CANVAS)
        return;
    begin_interactive(t->server, t, false, 0);
}

static void handle_request_resize(struct wl_listener *listener, void *data) {
    struct sberry_toplevel *t = wl_container_of(listener, t, request_resize);
    if (!t || !t->server)
        return;
    if (t->server->mode == MODE_TILING)
        return;
    uint32_t edges = 0;
    if (t->xwayland_surface) {
        struct wlr_xwayland_resize_event *ev = data;
        if (ev)
            edges = ev->edges;
    } else {
        struct wlr_xdg_toplevel_resize_event *ev = data;
        if (ev)
            edges = ev->edges;
    }
    /* Electron / Chromium / Firefox / X11 CSD edges → interactive resize */
    begin_interactive(t->server, t, true, edges);
}

static void handle_request_fullscreen(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_toplevel *t = wl_container_of(listener, t, request_fullscreen);
    if (!t || !t->toplevel || !t->toplevel->base)
        return;
    /* Telegram/Qt can fire this before initial configure — defer until initialized */
    if (!t->toplevel->base->initialized) {
        /* Stash intent via the requested flag; handle_map / commit will apply */
        return;
    }
    bool want = t->toplevel->requested.fullscreen;
    toplevel_set_fullscreen(t, want);
}


static void handle_toplevel_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_toplevel *t = wl_container_of(listener, t, commit);
    if (!t || !t->toplevel || !t->toplevel->base)
        return;
    struct wlr_xdg_surface *xdg_surface = t->toplevel->base;

    /* First commit MUST get a configure before any buffer attach.
     * 0x0 = client chooses size. Fixes xdg_surface.error 3 (unconfigured_buffer). */
    if (xdg_surface->initial_commit) {
        if (!xdg_surface->initialized)
            return;
        if (t->decoration)
            decoration_force_ssd(t->decoration);
        /* Honor pending fullscreen (Telegram media viewer often requests on first commit) */
        if (t->toplevel->requested.fullscreen) {
            wlr_xdg_toplevel_set_fullscreen(t->toplevel, true);
            int sw = t->server->screen_width > 0 ? t->server->screen_width : 1920;
            int sh = t->server->screen_height > 0 ? t->server->screen_height : 1080;
            wlr_xdg_toplevel_set_size(t->toplevel, sw, sh);
            /* Full geometry applied on map via toplevel_set_fullscreen */
            return;
        }
        if (t->server->mode == MODE_CANVAS) {
            wlr_xdg_toplevel_set_size(t->toplevel, CANVAS_DEFAULT_W, CANVAS_DEFAULT_H);
            t->width = CANVAS_DEFAULT_W;
            t->height = CANVAS_DEFAULT_H;
        } else {
            wlr_xdg_toplevel_set_size(t->toplevel, 0, 0);
        }
        return;
    }

    /* Do NOT sync requested.fullscreen here.
     * Compositor-initiated fullscreen (Super+f) leaves requested=false;
     * re-applying it would immediately exit fullscreen. Client requests
     * go through handle_request_fullscreen instead. */

    /* Re-apply scale after buffer replace (focused terminal cursor blink) */
    if (t->mapped && t->visual_scale > 0.0 && t->visual_scale < 0.999
            && !(t->animating && t->anim_zoom)) {
        toplevel_apply_visual_scale(t, t->visual_scale);
    }

    if (t->server->mode == MODE_TILING)
        return;
    if (t->fullscreen)
        return;
    if (t->animating)
        return;

    if (xdg_surface->geometry.width > 0 && xdg_surface->geometry.height > 0) {
        int gw = xdg_surface->geometry.width;
        int gh = xdg_surface->geometry.height;
        if (gw >= CANVAS_MIN_W)
            t->width = gw;
        if (gh >= CANVAS_MIN_H)
            t->height = gh;
        if (t->mapped) {
            if (t->visual_scale > 0.0 && t->visual_scale < 0.999)
                toplevel_apply_visual_scale(t, t->visual_scale);
            else
                toplevel_update_borders(t);
        }
    }
}


static void handle_map(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_toplevel *t = wl_container_of(listener, t, map);
    struct sberry_server *server = t->server;

    /* Overview: no new windows (screenshots / non-window cmds still OK) */
    if (server->overview) {
        wlr_log(WLR_INFO, "overview: reject new window");
        if (t->toplevel)
            wlr_xdg_toplevel_send_close(t->toplevel);
        else if (t->xwayland_surface)
            wlr_xwayland_surface_close(t->xwayland_surface);
        return;
    }

    t->mapped = true;

    if (t->foreign_handle) {
        wlr_foreign_toplevel_handle_v1_set_title(t->foreign_handle,
            t->toplevel->title ? t->toplevel->title : "");
        wlr_foreign_toplevel_handle_v1_set_app_id(t->foreign_handle,
            t->toplevel->app_id ? t->toplevel->app_id : "");
        struct sberry_output *out;
        wl_list_for_each(out, &server->outputs, link) {
            wlr_foreign_toplevel_handle_v1_output_enter(t->foreign_handle, out->wlr_output);
        }
    }

    /* Pending fullscreen from initial commit / request before map */
    if (t->toplevel && t->toplevel->requested.fullscreen && !t->fullscreen) {
        toplevel_set_fullscreen(t, true);
        focus_toplevel(server, t);
        publish_window_coords(server);
        return;
    }

    if (server->mode == MODE_TILING) {
        t->needs_spawn_anim = true;
        server_arrange_tiles(server);
    } else {
        /* Canvas: open under cursor. mx/my are LOCAL to the clearing
         * (0,0 = top-left of ws) — must subtract base_mx/base_my. */
        if (!t->has_canvas_pos) {
            struct sberry_clearing *cl =
                &server->clearings[server->current_clearing_idx];
            double z = server->camera.zoom;
            if (z < 0.01) z = 0.01;
            double wx = (server->cursor->x - server->screen_width  / 2.0) / z
                        + server->camera.cx;
            double wy = (server->cursor->y - server->screen_height / 2.0) / z
                        + server->camera.cy;
            int w = CANVAS_DEFAULT_W, h = CANVAS_DEFAULT_H;
            if (t->toplevel && t->toplevel->base) {
                struct wlr_box geo = t->toplevel->base->current.geometry;
                if (geo.width >= CANVAS_MIN_W) w = geo.width;
                if (geo.height >= CANVAS_MIN_H) h = geo.height;
                if (t->toplevel->base->surface) {
                    int sw = t->toplevel->base->surface->current.width;
                    int sh = t->toplevel->base->surface->current.height;
                    if (sw >= CANVAS_MIN_W) w = sw;
                    if (sh >= CANVAS_MIN_H) h = sh;
                }
            }
            if (t->width >= CANVAS_MIN_W) w = t->width;
            if (t->height >= CANVAS_MIN_H) h = t->height;
            if (w < CANVAS_MIN_W) w = CANVAS_DEFAULT_W;
            if (h < CANVAS_MIN_H) h = CANVAS_DEFAULT_H;
            t->width = w;
            t->height = h;
            if (t->toplevel)
                wlr_xdg_toplevel_set_size(t->toplevel, w, h);
            /* world -> local */
            double nmx = wx - cl->base_mx - w / 2.0;
            double nmy = wy - cl->base_my - h / 2.0;
            t->mx = nmx;
            t->my = nmy;
            toplevel_clamp_to_clearing(server, t);
            t->canvas_mx = t->mx;
            t->canvas_my = t->my;
            t->canvas_width  = t->width;
            t->canvas_height = t->height;
            t->has_canvas_pos = true;
            toplevel_anim_spawn(t, t->mx, t->my, t->width, t->height);
        } else {
            apply_camera(server, t);
            toplevel_update_borders(t);
        }
    }
    /* After position is known — no (0,0) flash */
    if (t->scene_tree)
        wlr_scene_node_set_enabled(&t->scene_tree->node, true);

    focus_toplevel(server, t);
    publish_window_coords(server);
}


static void focus_fallback(struct sberry_server *server, struct sberry_toplevel *dying) {
    if (!server || server->focused_toplevel != dying)
        return;
    server->focused_toplevel = NULL;
    struct sberry_clearing *cl = &server->clearings[server->current_clearing_idx];
    struct sberry_toplevel *prev = NULL, *t;
    wl_list_for_each(t, &cl->toplevels, clearing_link) {
        if (t == dying)
            break;
        if (t->mapped)
            prev = t;
    }
    if (!prev) {
        wl_list_for_each(t, &cl->toplevels, clearing_link) {
            if (t != dying && t->mapped) {
                prev = t;
                break;
            }
        }
    }
    if (prev)
        focus_toplevel(server, prev);
}

static void handle_unmap(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_toplevel *t = wl_container_of(listener, t, unmap);
    struct sberry_server *server = t->server;
    t->mapped = false;
    t->animating = false;
    t->ov_animating = false;
    t->closing = false;
    toplevel_apply_visual_scale(t, 1.0);
    focus_fallback(server, t);
    if (server->mode == MODE_TILING)
        server_arrange_tiles(server);
}


static void server_update_camera(struct sberry_server *server) {
    struct sberry_clearing *curr_cl = &server->clearings[server->current_clearing_idx];
    struct sberry_toplevel *t;
    wl_list_for_each(t, &curr_cl->toplevels, clearing_link) {
        apply_camera(server, t);
    }
}

static bool handle_keybinding(struct sberry_server *server, xkb_keysym_t sym, uint32_t modifiers) {
    modifiers &= (WLR_MODIFIER_SHIFT | WLR_MODIFIER_CTRL |
                  WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO);
    sym = xkb_keysym_to_lower(sym);

    for (size_t i = 0; i < keys_len; i++) {
        xkb_keysym_t want = xkb_keysym_to_lower(keys[i].keysym);
        if (keys[i].mod == modifiers && want == sym) {
            if (keys[i].cmd)
                spawn(keys[i].cmd);
            else if (keys[i].action)
                sberry_do(server, keys[i].action, keys[i].arg);
            return true;
        }
    }
    return false;
}

static void handle_key(struct wl_listener *listener, void *data) {
    struct sberry_keyboard *kb = wl_container_of(listener, kb, key);
    struct wlr_keyboard_key_event *event = data;
    struct sberry_server *server = kb->server;

    uint32_t modifiers = wlr_keyboard_get_modifiers(kb->keyboard);
    server->super_pressed = (modifiers & WLR_MODIFIER_LOGO) != 0;

    /* evdev scancode + 8 = xkb keycode */
    uint32_t keycode = event->keycode + 8;

    /* Compositor keybinds ALWAYS win over layer-shell / clients.
     * Match against layout group 0 / level 0 keysyms so Super+q works
     * the same on us, ru, ara, de, … — physical key, not typed letter. */
    bool handled = false;
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED && kb->keyboard->keymap) {
        const xkb_keysym_t *bind_syms = NULL;
        int n_bind = xkb_keymap_key_get_syms_by_level(
            kb->keyboard->keymap, keycode, 0, 0, &bind_syms);
        for (int i = 0; i < n_bind; i++) {
            if (handle_keybinding(server, bind_syms[i], modifiers)) {
                handled = true;
                break;
            }
        }
        /* Fallback: current layout keysyms (for Print, F-keys, etc.) */
        if (!handled) {
            const xkb_keysym_t *syms = NULL;
            int nsyms = xkb_state_key_get_syms(
                kb->keyboard->xkb_state, keycode, &syms);
            for (int i = 0; i < nsyms; i++) {
                if (handle_keybinding(server, syms[i], modifiers)) {
                    handled = true;
                    break;
                }
            }
        }
    }

    if (!handled) {
        wlr_seat_set_keyboard(server->seat, kb->keyboard);
        wlr_seat_keyboard_notify_key(server->seat,
            event->time_msec, event->keycode, event->state);
    }
}

/* Universal layout feed for waybar / noctalia / any bar:
 *   $XDG_RUNTIME_DIR/sberry-layout        → layout name (e.g. "English (US)")
 *   $XDG_RUNTIME_DIR/sberry-layout-short  → group index (0, 1, ...)
 * Waybar:
 *   "custom/layout": { "exec": "cat $XDG_RUNTIME_DIR/sberry-layout 2>/dev/null", "interval": 1, "format": " {}" }
 */
static void publish_keyboard_layout(struct sberry_server *server, struct wlr_keyboard *keyboard) {
    if (!keyboard || !keyboard->keymap)
        return;
    xkb_layout_index_t idx = xkb_state_serialize_layout(
        keyboard->xkb_state, XKB_STATE_LAYOUT_EFFECTIVE);
    const char *name = xkb_keymap_layout_get_name(keyboard->keymap, idx);
    if (!name || !name[0])
        name = "??";

    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (!rt)
        return;

    char shortcode[32] = "?";
    {
        char layouts_copy[128];
        snprintf(layouts_copy, sizeof(layouts_copy), "%s", keyboard_layouts);
        unsigned want = (unsigned)idx, li = 0;
        char *save = NULL;
        for (char *tok = strtok_r(layouts_copy, ",", &save); tok;
             tok = strtok_r(NULL, ",", &save), li++) {
            if (li == want) {
                /* uppercase short tag: us -> US, ru -> RU, ara -> ARA */
                size_t k = 0;
                for (; tok[k] && k + 1 < sizeof(shortcode); k++) {
                    char c = tok[k];
                    if (c >= 'a' && c <= 'z')
                        c = (char)(c - 'a' + 'A');
                    shortcode[k] = c;
                }
                shortcode[k] = '\0';
                break;
            }
        }
    }

    char path[256];
    snprintf(path, sizeof(path), "%s/sberry-layout", rt);
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%s\n", name);
        fclose(f);
    }
    snprintf(path, sizeof(path), "%s/sberry-layout-short", rt);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "%s\n", shortcode);
        fclose(f);
    }
    /* waybar-style one-liner */
    snprintf(path, sizeof(path), "%s/sberry-layout-waybar", rt);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "{\"text\":\"%s\",\"alt\":\"%s\",\"tooltip\":\"%s\"}\n",
                shortcode, shortcode, name);
        fclose(f);
    }

    server->kb_layout_index = idx;
    wlr_log(WLR_INFO, "layout published: %s / %s (group %u)", shortcode, name, (unsigned)idx);
}


void sberry_cycle_layout(struct sberry_server *server) {
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
    if (!kb || !kb->keymap || !kb->xkb_state)
        return;

    xkb_layout_index_t n = xkb_keymap_num_layouts(kb->keymap);
    if (n < 2)
        return;

    xkb_layout_index_t cur = xkb_state_serialize_layout(
        kb->xkb_state, XKB_STATE_LAYOUT_EFFECTIVE);
    xkb_layout_index_t next = (cur + 1) % n;

    xkb_mod_mask_t depressed = xkb_state_serialize_mods(kb->xkb_state, XKB_STATE_MODS_DEPRESSED);
    xkb_mod_mask_t latched   = xkb_state_serialize_mods(kb->xkb_state, XKB_STATE_MODS_LATCHED);
    xkb_mod_mask_t locked    = xkb_state_serialize_mods(kb->xkb_state, XKB_STATE_MODS_LOCKED);

    xkb_state_update_mask(kb->xkb_state, depressed, latched, locked, 0, 0, next);

    kb->modifiers.depressed = depressed;
    kb->modifiers.latched = latched;
    kb->modifiers.locked = locked;
    kb->modifiers.group = next;

    publish_keyboard_layout(server, kb);
    wlr_seat_set_keyboard(server->seat, kb);
    wlr_seat_keyboard_notify_modifiers(server->seat, &kb->modifiers);
    wlr_log(WLR_INFO, "layout cycled %u -> %u", (unsigned)cur, (unsigned)next);
}

static void handle_modifiers(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_keyboard *kb = wl_container_of(listener, kb, modifiers);
    uint32_t modifiers = wlr_keyboard_get_modifiers(kb->keyboard);
    kb->server->super_pressed = (modifiers & WLR_MODIFIER_LOGO) != 0;

    xkb_layout_index_t group = xkb_state_serialize_layout(
        kb->keyboard->xkb_state, XKB_STATE_LAYOUT_EFFECTIVE);
    if (group != kb->server->kb_layout_index)
        publish_keyboard_layout(kb->server, kb->keyboard);

    /* Super held → take keyboard focus from noctalia/layer back to focused window
     * so compositor chords (Super+q etc.) are not eaten by the shell. */
    if (kb->server->super_pressed && kb->server->focused_toplevel) {
        struct sberry_toplevel *t = kb->server->focused_toplevel;
        struct wlr_surface *surf = NULL;
        if (t->toplevel)
            surf = t->toplevel->base->surface;
        else if (t->xwayland_surface)
            surf = t->xwayland_surface->surface;
        if (surf) {
            wlr_seat_keyboard_notify_enter(kb->server->seat, surf,
                kb->keyboard->keycodes, kb->keyboard->num_keycodes,
                &kb->keyboard->modifiers);
        }
    }

    wlr_seat_set_keyboard(kb->server->seat, kb->keyboard);
    wlr_seat_keyboard_notify_modifiers(kb->server->seat, &kb->keyboard->modifiers);
}

static void handle_keyboard_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_keyboard *kb = wl_container_of(listener, kb, destroy);
    
    wl_list_remove(&kb->key.link);
    wl_list_remove(&kb->modifiers.link);
    wl_list_remove(&kb->destroy.link);
    wl_list_remove(&kb->link);
    
    free(kb);
}

static void handle_session_active(struct wl_listener *listener, void *data) {
    struct sberry_server *server = wl_container_of(listener, server, session_active);

    /* Log BEFORE touching data — if we crash on deref, at least we know we got here */
    wlr_log(WLR_INFO, "session active signal received (data=%p)", data);

    bool active = true;
    if (data)
        active = *(bool *)data;
    server->session_is_active = active;

    if (active) {
        wlr_log(WLR_INFO, "Session active - switched back to our VT");
        struct sberry_output *out;
        wl_list_for_each(out, &server->outputs, link) {
            if (out->wlr_output && out->wlr_output->enabled)
                wlr_output_schedule_frame(out->wlr_output);
        }
    } else {
        wlr_log(WLR_INFO, "Session inactive - switched away from our VT");
        /* Do not free outputs. Skip frames via session_is_active flag. */
    }
}

/* Explicit VT switch (Ctrl+Alt+Fn). Safe no-op without a session (nested). */
static void sberry_change_vt(struct sberry_server *server, unsigned int vt) {
    if (!server->session) {
        wlr_log(WLR_ERROR, "No session — cannot switch VT (nested backend?)");
        return;
    }
    if (!wlr_session_change_vt(server->session, vt))
        wlr_log(WLR_ERROR, "wlr_session_change_vt(%u) failed", vt);
}

void sberry_vt1(struct sberry_server *s)  { sberry_change_vt(s, 1); }
void sberry_vt2(struct sberry_server *s)  { sberry_change_vt(s, 2); }
void sberry_vt3(struct sberry_server *s)  { sberry_change_vt(s, 3); }
void sberry_vt4(struct sberry_server *s)  { sberry_change_vt(s, 4); }
void sberry_vt5(struct sberry_server *s)  { sberry_change_vt(s, 5); }
void sberry_vt6(struct sberry_server *s)  { sberry_change_vt(s, 6); }
void sberry_vt7(struct sberry_server *s)  { sberry_change_vt(s, 7); }
void sberry_vt8(struct sberry_server *s)  { sberry_change_vt(s, 8); }
void sberry_vt9(struct sberry_server *s)  { sberry_change_vt(s, 9); }
void sberry_vt10(struct sberry_server *s) { sberry_change_vt(s, 10); }
void sberry_vt11(struct sberry_server *s) { sberry_change_vt(s, 11); }
void sberry_vt12(struct sberry_server *s) { sberry_change_vt(s, 12); }

/* Universal keybind dispatcher — config.h calls only this. */
void sberry_quit(struct sberry_server *server); /* defined near main */
void sberry_do(struct sberry_server *server, int action, int arg) {
    if (!server)
        return;
    switch (action) {
    case SB_QUIT:
        sberry_quit(server);
        break;
    case SB_KILL:
        sberry_kill_focused(server);
        break;
    case SB_TOGGLE_MODE:
        sberry_toggle_mode(server);
        break;
    case SB_TOGGLE_LAYOUT:
        sberry_toggle_layout(server);
        break;
    case SB_CYCLE_LAYOUT:
        sberry_cycle_layout(server);
        break;
    case SB_TOGGLE_FULLSCREEN:
        sberry_toggle_fullscreen(server);
        break;
    case SB_TOGGLE_OVERVIEW:
        sberry_toggle_overview(server);
        break;
    case SB_FOCUS: {
        int dx = 0, dy = 0;
        if (arg == DIR_LEFT) dx = -1;
        else if (arg == DIR_RIGHT) dx = 1;
        else if (arg == DIR_UP) dy = -1;
        else if (arg == DIR_DOWN) dy = 1;
        struct sberry_toplevel *cur = server->focused_toplevel;
        struct sberry_toplevel *n = find_neighbor(server, cur, dx, dy);
        if (n)
            focus_toplevel(server, n);
        break;
    }
    case SB_MOVE: {
        if (arg == DIR_LEFT) move_focused(server, -MOVE_STEP, 0);
        else if (arg == DIR_RIGHT) move_focused(server, MOVE_STEP, 0);
        else if (arg == DIR_UP) move_focused(server, 0, -MOVE_STEP);
        else if (arg == DIR_DOWN) move_focused(server, 0, MOVE_STEP);
        break;
    }
    case SB_SWAP: {
        int dx = 0, dy = 0;
        if (arg == DIR_LEFT) dx = -1;
        else if (arg == DIR_RIGHT) dx = 1;
        else if (arg == DIR_UP) dy = -1;
        else if (arg == DIR_DOWN) dy = 1;
        swap_with_neighbor(server, dx, dy);
        break;
    }
    case SB_RESIZE: {
        if (arg == DIR_LEFT) resize_focused(server, -RESIZE_STEP, 0);
        else if (arg == DIR_RIGHT) resize_focused(server, RESIZE_STEP, 0);
        else if (arg == DIR_UP) resize_focused(server, 0, -RESIZE_STEP);
        else if (arg == DIR_DOWN) resize_focused(server, 0, RESIZE_STEP);
        break;
    }
    case SB_ZOOM:
        if (arg > 0 && arg != 2)
            camera_zoom_at_cursor(server, ZOOM_STEP);
        else if (arg < 0)
            camera_zoom_at_cursor(server, 1.0 / ZOOM_STEP);
        else if (arg == 2)
            sberry_zoom_fit(server);
        else
            sberry_zoom_reset(server);
        break;
    case SB_CENTER:
        sberry_center_focused(server);
        break;
    case SB_ALT_TAB:
        if (arg < 0)
            sberry_alt_tab_prev(server);
        else
            sberry_alt_tab(server);
        break;
    case SB_WS:
        if (arg >= 1 && arg <= 9)
            sberry_switch_clearing(server, arg - 1);
        break;
    case SB_WS_STEP: {
        int n = server->current_clearing_idx + (arg >= 0 ? 1 : -1);
        if (n > 8) n = 0;
        if (n < 0) n = 8;
        sberry_switch_clearing(server, n);
        break;
    }
    case SB_MOVE_WS:
        if (arg >= 1 && arg <= 9)
            sberry_move_to_clearing(server, arg - 1);
        break;
    case SB_VT:
        if (arg >= 1 && arg <= 12)
            sberry_change_vt(server, (unsigned)arg);
        break;
    default:
        wlr_log(WLR_ERROR, "sberry_do: unknown action %d", action);
        break;
    }
}



/* Clipboard: must approve set_selection (tinywl pattern). */
static void handle_request_set_selection(struct wl_listener *listener, void *data) {
    struct sberry_server *server =
        wl_container_of(listener, server, request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static void handle_request_set_primary_selection(struct wl_listener *listener, void *data) {
    struct sberry_server *server =
        wl_container_of(listener, server, request_set_primary_selection);
    struct wlr_seat_request_set_primary_selection_event *event = data;
    wlr_seat_set_primary_selection(server->seat, event->source, event->serial);
}

static void handle_new_input(struct wl_listener *listener, void *data) {
    struct sberry_server *server = wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;

    if (device->type == WLR_INPUT_DEVICE_KEYBOARD) {
        struct sberry_keyboard *kb = calloc(1, sizeof(*kb));
        kb->server = server;
        kb->keyboard = wlr_keyboard_from_input_device(device);

        struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        
        struct xkb_rule_names names = {
            .rules = NULL,
            .model = NULL,
            .layout = keyboard_layouts,   
            .variant = NULL,
            .options = keyboard_options   
        };

        struct xkb_keymap *keymap = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
        
        if (keymap) {
            wlr_keyboard_set_keymap(kb->keyboard, keymap);
            xkb_keymap_unref(keymap);
        } else {
            wlr_log(WLR_ERROR, "Failed to compile xkb keymap!");
        }
        
        xkb_context_unref(ctx);

        wlr_keyboard_set_repeat_info(kb->keyboard, 40, 200);  

        kb->key.notify = handle_key;
        wl_signal_add(&kb->keyboard->events.key, &kb->key);
        kb->modifiers.notify = handle_modifiers;
        wl_signal_add(&kb->keyboard->events.modifiers, &kb->modifiers);

        kb->destroy.notify = handle_keyboard_destroy;
        wl_signal_add(&kb->keyboard->base.events.destroy, &kb->destroy);

        wlr_seat_set_keyboard(server->seat, kb->keyboard);
        wl_list_insert(&server->keyboards, &kb->link);
        publish_keyboard_layout(server, kb->keyboard);
    } else if (device->type == WLR_INPUT_DEVICE_POINTER) {
        wlr_cursor_attach_input_device(server->cursor, device);
    }

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&server->keyboards)) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    wlr_seat_set_capabilities(server->seat, caps);
}

static bool scene_node_is_desc(struct wlr_scene_node *node,
                               struct wlr_scene_node *ancestor) {
    while (node) {
        if (node == ancestor)
            return true;
        node = node->parent ? &node->parent->node : NULL;
    }
    return false;
}

static void *surface_at(struct sberry_server *server,
                        double cx, double cy, struct wlr_surface **surface,
                        double *sx, double *sy, bool *is_layer) {
    *is_layer = false;
    *surface = NULL;

    struct wlr_scene_node *node = wlr_scene_node_at(
        &server->scene->tree.node, cx, cy, sx, sy);
    if (!node)
        return NULL;

    /* Prefer the actual buffer surface under the cursor (popup subsurfaces too) */
    if (node->type == WLR_SCENE_NODE_BUFFER) {
        struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
        struct wlr_scene_surface *scene_surface =
            wlr_scene_surface_try_from_buffer(scene_buffer);
        if (scene_surface)
            *surface = scene_surface->surface;
    }

    /* Layer-shell (waybar / noctalia panels / their xdg popups parented under them) */
    for (int i = 3; i >= 0; i--) { /* OVERLAY → TOP → … hit top layers first in list walk */
        if (!server->layer_tree[i])
            continue;
        if (!scene_node_is_desc(node, &server->layer_tree[i]->node))
            continue;
        *is_layer = true;
        struct sberry_layer_surface *ls;
        wl_list_for_each(ls, &server->layer_surfaces[i], link) {
            if (!ls->scene_layer)
                continue;
            if (scene_node_is_desc(node, &ls->scene_layer->tree->node)) {
                if (!*surface && ls->layer_surface && ls->layer_surface->surface)
                    *surface = ls->layer_surface->surface;
                return ls;
            }
        }
        /* Under this layer tree but no exact ls match — still a layer hit so
         * windows underneath don't steal the click. Use first ls as token. */
        if (!wl_list_empty(&server->layer_surfaces[i])) {
            struct sberry_layer_surface *any =
                wl_container_of(server->layer_surfaces[i].next,
                                any, link);
            return any;
        }
        return NULL;
    }

    /* Regular windows on current clearing */
    struct sberry_clearing *curr_cl = &server->clearings[server->current_clearing_idx];
    struct sberry_toplevel *t;
    wl_list_for_each(t, &curr_cl->toplevels, clearing_link) {
        if (!t->scene_tree)
            continue;
        if (scene_node_is_desc(node, &t->scene_tree->node)) {
            *is_layer = false;
            if (!*surface) {
                if (t->toplevel)
                    *surface = t->toplevel->base->surface;
                else if (t->xwayland_surface)
                    *surface = t->xwayland_surface->surface;
            }
            return t;
        }
    }

    /* Xwayland override-redirect menus / dropdowns live under fullscreen_tree.
     * Without this hit-test returns NULL → pointer_clear_focus → menu dies. */
    if (server->fullscreen_tree
            && scene_node_is_desc(node, &server->fullscreen_tree->node)) {
        *is_layer = true; /* treat like overlay: don't focus_toplevel */
        return server; /* non-NULL token so motion keeps focus */
    }

    /* xdg_popup / orphan surface under scene but not matched above —
     * still must keep pointer focus (browser/GTK menus). */
    if (*surface) {
        *is_layer = true;
        return server;
    }

    return NULL;
}


static void constraint_destroy_notify(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_server *server = wl_container_of(listener, server, constraint_destroy);
    if (server->constraint_commit.link.prev)
        wl_list_remove(&server->constraint_commit.link);
    if (server->constraint_destroy.link.prev)
        wl_list_remove(&server->constraint_destroy.link);
    server->active_constraint = NULL;
    cursor_set_themed(server, "left_ptr");
}

static void constraint_commit_notify(struct wl_listener *listener, void *data) {
    (void)data;
    (void)listener;
    /* region may change; nothing mandatory for locked pointer */
}

static void activate_constraint(struct sberry_server *server,
                                struct wlr_pointer_constraint_v1 *constraint) {
    if (server->active_constraint == constraint)
        return;
    if (server->active_constraint) {
        if (server->constraint_commit.link.prev)
            wl_list_remove(&server->constraint_commit.link);
        if (server->constraint_destroy.link.prev)
            wl_list_remove(&server->constraint_destroy.link);
        wlr_pointer_constraint_v1_send_deactivated(server->active_constraint);
        server->active_constraint = NULL;
    }
    server->active_constraint = constraint;
    if (!constraint)
        return;

    server->constraint_commit.notify = constraint_commit_notify;
    wl_signal_add(&constraint->surface->events.commit, &server->constraint_commit);
    server->constraint_destroy.notify = constraint_destroy_notify;
    wl_signal_add(&constraint->events.destroy, &server->constraint_destroy);

    wlr_pointer_constraint_v1_send_activated(constraint);
    /* hide hardware/software cursor while locked (FPS games) */
    if (constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED)
        wlr_cursor_set_surface(server->cursor, NULL, 0, 0);
}

static void handle_new_pointer_constraint(struct wl_listener *listener, void *data) {
    struct sberry_server *server =
        wl_container_of(listener, server, new_pointer_constraint);
    struct wlr_pointer_constraint_v1 *constraint = data;

    /* Activate when the constrained surface is the focused one */
    struct wlr_surface *focused = NULL;
    if (server->focused_toplevel) {
        if (server->focused_toplevel->toplevel)
            focused = server->focused_toplevel->toplevel->base->surface;
        else if (server->focused_toplevel->xwayland_surface)
            focused = server->focused_toplevel->xwayland_surface->surface;
    }
    if (focused && constraint->surface == focused)
        activate_constraint(server, constraint);
    else {
        /* Defer: activate on next focus/click if still pending.
         * Store via constraint->data for later — activate when surface focused. */
        constraint->data = server;
    }
}


static void clearing_link_swap(struct sberry_toplevel *a, struct sberry_toplevel *b) {
    if (!a || !b || a == b) return;
    struct wl_list *A = &a->clearing_link;
    struct wl_list *B = &b->clearing_link;
    if (A->next == B) {
        struct wl_list *ap = A->prev, *bn = B->next;
        ap->next = B; B->prev = ap;
        B->next = A; A->prev = B;
        A->next = bn; bn->prev = A;
    } else if (B->next == A) {
        struct wl_list *bp = B->prev, *an = A->next;
        bp->next = A; A->prev = bp;
        A->next = B; B->prev = A;
        B->next = an; an->prev = B;
    } else {
        struct wl_list *ap = A->prev, *an = A->next;
        struct wl_list *bp = B->prev, *bn = B->next;
        ap->next = B; an->prev = B;
        bp->next = A; bn->prev = A;
        B->prev = ap; B->next = an;
        A->prev = bp; A->next = bn;
    }
}


/* Apply theme cursor from config.h.
 * wlr_cursor_set_xcursor returns void — try left_ptr first (most themes). */

/* Scale hardware cursor with camera.zoom (canvas downscale). */
static void cursor_update_for_zoom(struct sberry_server *server) {
    if (!server || !server->cursor)
        return;
    if (server->overview || server->overview_leaving)
        return;
    double z = server->camera.zoom;
    if (z > 1.0) z = 1.0;
    if (z < 0.15) z = 0.15;
    int base = XCURSOR_THEME_SIZE;
    if (base < 12) base = 12;
    int sz = (int)lround((double)base * z);
    if (sz < 12) sz = 12;
    if (sz > base) sz = base;
    if (sz == server->cursor_size_loaded && server->cursor_mgr)
        return;

    const char *theme = getenv("XCURSOR_THEME");
    if (!theme || !theme[0])
        theme = XCURSOR_THEME_NAME;
    if (server->cursor_mgr) {
        wlr_xcursor_manager_destroy(server->cursor_mgr);
        server->cursor_mgr = NULL;
    }
    server->cursor_mgr = wlr_xcursor_manager_create(theme, sz);
    if (server->cursor_mgr) {
        wlr_xcursor_manager_load(server->cursor_mgr, 1);
        server->cursor_size_loaded = sz;
        cursor_set_themed(server, "left_ptr");
    }
}

static void cursor_set_themed(struct sberry_server *server, const char *name) {
    if (!server || !server->cursor || !server->cursor_mgr)
        return;
    const char *pick = (name && name[0]) ? name : "left_ptr";
    wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, pick);
    /* If theme lacks this name, wlroots keeps previous cursor; try left_ptr once more */
    if (strcmp(pick, "left_ptr") != 0)
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
    if (strcmp(pick, "left_ptr") != 0)
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, pick);
}

static void handle_request_set_cursor_shape(struct wl_listener *listener, void *data) {
    struct sberry_server *server =
        wl_container_of(listener, server, request_set_cursor_shape);
    struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;
    struct wlr_seat_client *focused = server->seat->pointer_state.focused_client;
    if (!event || focused != event->seat_client)
        return;
    if (server->active_constraint &&
        server->active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED)
        return;
    const char *name = wlr_cursor_shape_v1_name(event->shape);
    if (!name)
        name = "left_ptr";
    cursor_set_themed(server, name);
}

static void handle_request_cursor(struct wl_listener *listener, void *data) {
    struct sberry_server *server = wl_container_of(listener, server, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *event = data;
    struct wlr_seat_client *focused = server->seat->pointer_state.focused_client;
    if (focused != event->seat_client)
        return;
    /* Games: locked pointer or explicit hide → no hardware cursor on top */
    if (server->active_constraint &&
        server->active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
        wlr_cursor_set_surface(server->cursor, NULL, 0, 0);
        return;
    }
    if (!event->surface) {
        wlr_cursor_set_surface(server->cursor, NULL, 0, 0);
        return;
    }
#if FORCE_THEME_CURSOR
    cursor_set_themed(server, "left_ptr");
#else
    wlr_cursor_set_surface(server->cursor, event->surface,
                           event->hotspot_x, event->hotspot_y);
#endif
}


static void process_cursor_motion(struct sberry_server *server, uint32_t time) {
    double sx, sy;
    bool is_layer = false;
    struct wlr_surface *surface = NULL;
    void *found = surface_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy, &is_layer);

    if (found && surface) {
        /* Dedup enter — re-enter every motion closes Zen/Firefox popups */
        if (server->last_pointer_surface != surface) {
            wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
            server->last_pointer_surface = surface;
            /* Keyboard only for real layer-shell TOP/OVERLAY panels, not for
             * OR/popup tokens (found == server) — those keep parent kb focus. */
            if (is_layer && found != (void *)server) {
                struct sberry_layer_surface *ls = found;
                int layer = (ls && ls->layer_surface) ? (int)ls->layer : 2;
                struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
                if (kb && layer >= 2)
                    wlr_seat_keyboard_notify_enter(server->seat, surface,
                        kb->keycodes, kb->num_keycodes, &kb->modifiers);
            }
        }
        wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
        /* No keyboard enter on every motion for normal windows — kills xdg popups */
    } else {
        if (server->last_pointer_surface) {
            wlr_seat_pointer_clear_focus(server->seat);
            server->last_pointer_surface = NULL;
        }
        cursor_set_themed(server, "left_ptr");
    }
}



/* Begin compositor-driven move/resize (Super+mouse or client CSD request). */
static void begin_interactive(struct sberry_server *server, struct sberry_toplevel *t,
                              bool resize, uint32_t edges) {
    if (!server || !t || !t->mapped || t->fullscreen)
        return;
    if (server->overview || server->overview_leaving)
        return;
    focus_toplevel(server, t);
    server->grabbed_toplevel = t;
    server->resizing = resize;
    server->resize_edges = edges;
    server->tiling_drag = false;
    double z = server->camera.zoom;
    if (z < 0.01) z = 0.01;
    server->grab_x = (server->cursor->x - server->screen_width / 2.0) / z
                     + server->camera.cx;
    server->grab_y = (server->cursor->y - server->screen_height / 2.0) / z
                     + server->camera.cy;
    server->grab_toplevel_mx = t->mx;
    server->grab_toplevel_my = t->my;
    server->grab_toplevel_w = t->width > 0 ? t->width : 200;
    server->grab_toplevel_h = t->height > 0 ? t->height : 150;
    server->resize_last_configure_ms = 0;
}

/* Edge-aware geometry from grab snapshot + total cursor delta (world space). */
static void interactive_resize_geom(struct sberry_server *server,
                                    double *out_mx, double *out_my,
                                    int *out_w, int *out_h) {
    double dx = 0, dy = 0;
    double z = server->camera.zoom;
    if (z < 0.01) z = 0.01;
    double cur_x = (server->cursor->x - server->screen_width / 2.0) / z
                   + server->camera.cx;
    double cur_y = (server->cursor->y - server->screen_height / 2.0) / z
                   + server->camera.cy;
    dx = cur_x - server->grab_x;
    dy = cur_y - server->grab_y;

    uint32_t e = server->resize_edges;
    double mx = server->grab_toplevel_mx;
    double my = server->grab_toplevel_my;
    int w = server->grab_toplevel_w;
    int h = server->grab_toplevel_h;

    if (e & WLR_EDGE_LEFT) {
        mx += dx;
        w -= (int)dx;
    } else if (e & WLR_EDGE_RIGHT) {
        w += (int)dx;
    }
    if (e & WLR_EDGE_TOP) {
        my += dy;
        h -= (int)dy;
    } else if (e & WLR_EDGE_BOTTOM) {
        h += (int)dy;
    }
    /* No edges → bottom-right (Super+RMB default) */
    if (!(e & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT | WLR_EDGE_TOP | WLR_EDGE_BOTTOM))) {
        w += (int)dx;
        h += (int)dy;
    }

    int min_w = 150, min_h = 100;
    struct sberry_toplevel *t = server->grabbed_toplevel;
    if (t && t->toplevel) {
        if (t->toplevel->current.min_width > min_w)
            min_w = t->toplevel->current.min_width;
        if (t->toplevel->current.min_height > min_h)
            min_h = t->toplevel->current.min_height;
    }
    if (w < min_w) {
        if (e & WLR_EDGE_LEFT)
            mx -= (min_w - w);
        w = min_w;
    }
    if (h < min_h) {
        if (e & WLR_EDGE_TOP)
            my -= (min_h - h);
        h = min_h;
    }
    if (out_mx) *out_mx = mx;
    if (out_my) *out_my = my;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
}

static void interactive_commit_size(struct sberry_server *server,
                                    struct sberry_toplevel *t, bool force) {
    if (!t || !t->mapped)
        return;
    /* Live drag: borders/position only (GPU-smooth). Configure once on release
     * so Electron/Zen don't thrash. Optional mid-drag for simple clients. */
    if (!force) {
        if (!toplevel_dest_scale_ok(t))
            return; /* electron-like: wait for button up */
        uint32_t now = now_ms();
        if (server->resize_last_configure_ms != 0
                && now - server->resize_last_configure_ms < 50)
            return;
        server->resize_last_configure_ms = now;
    } else {
        server->resize_last_configure_ms = now_ms();
    }
    if (t->toplevel)
        wlr_xdg_toplevel_set_size(t->toplevel, t->width, t->height);
    else if (t->xwayland_surface)
        wlr_xwayland_surface_configure(t->xwayland_surface,
            (int16_t)t->mx, (int16_t)t->my, t->width, t->height);
}

static void handle_cursor_motion(struct wl_listener *listener, void *data) {
    struct sberry_server *server = wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *event = data;

    /* Always emit relative motion for games (FPS look) */
    wlr_relative_pointer_manager_v1_send_relative_motion(
        server->relative_pointer_manager,
        server->seat, (uint64_t)event->time_msec * 1000,
        event->delta_x, event->delta_y,
        event->unaccel_dx, event->unaccel_dy);

    /* Locked pointer: do not move the cursor, only relative events */
    if (server->active_constraint &&
        server->active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
        return;
    }

    wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);

    /* Confined pointer: clamp to region if set */
    if (server->active_constraint &&
        server->active_constraint->type == WLR_POINTER_CONSTRAINT_V1_CONFINED) {
        /* basic clamp to output for now */
        if (server->cursor->x < 0) server->cursor->x = 0;
        if (server->cursor->y < 0) server->cursor->y = 0;
        if (server->cursor->x > server->screen_width)
            server->cursor->x = server->screen_width;
        if (server->cursor->y > server->screen_height)
            server->cursor->y = server->screen_height;
    }

    if (server->overview) {
        overview_update_hover(server);
        process_cursor_motion(server, event->time_msec);
        return;
    }

    if (server->panning) {
        if (server->mode == MODE_TILING)
            return;
        double dx = server->cursor->x - server->pan_start_x;
        double dy = server->cursor->y - server->pan_start_y;
        server->camera.cx = server->camera_start_cx - dx / server->camera.zoom;
        server->camera.cy = server->camera_start_cy - dy / server->camera.zoom;
        camera_clamp(server);
        server_update_camera(server);
        publish_window_coords(server);
        return;
    }

    if (server->grabbed_toplevel) {
        double current_mx = (server->cursor->x - server->screen_width / 2.0) / server->camera.zoom + server->camera.cx;
        double current_my = (server->cursor->y - server->screen_height / 2.0) / server->camera.zoom + server->camera.cy;

        double dx = current_mx - server->grab_x;
        double dy = current_my - server->grab_y;

        if (server->resizing) {
            double nmx, nmy;
            int nw, nh;
            interactive_resize_geom(server, &nmx, &nmy, &nw, &nh);
            server->grabbed_toplevel->mx = nmx;
            server->grabbed_toplevel->my = nmy;
            server->grabbed_toplevel->width = nw;
            server->grabbed_toplevel->height = nh;
            toplevel_clamp_to_clearing(server, server->grabbed_toplevel);
            if (server->mode == MODE_CANVAS) {
                server->grabbed_toplevel->canvas_mx = server->grabbed_toplevel->mx;
                server->grabbed_toplevel->canvas_my = server->grabbed_toplevel->my;
                server->grabbed_toplevel->canvas_width  = server->grabbed_toplevel->width;
                server->grabbed_toplevel->canvas_height = server->grabbed_toplevel->height;
                server->grabbed_toplevel->has_canvas_pos = true;
            }
            toplevel_update_borders(server->grabbed_toplevel);
            apply_camera(server, server->grabbed_toplevel);
            /* Throttled configure — Electron survives, content updates live */
            interactive_commit_size(server, server->grabbed_toplevel, false);
        } else {
            server->grabbed_toplevel->mx = server->grab_toplevel_mx + dx;
            server->grabbed_toplevel->my = server->grab_toplevel_my + dy;
            toplevel_clamp_to_clearing(server, server->grabbed_toplevel);
            server->grabbed_toplevel->canvas_mx = server->grabbed_toplevel->mx;
            server->grabbed_toplevel->canvas_my = server->grabbed_toplevel->my;
            server->grabbed_toplevel->canvas_width  = server->grabbed_toplevel->width;
            server->grabbed_toplevel->canvas_height = server->grabbed_toplevel->height;
            server->grabbed_toplevel->has_canvas_pos = true;
            apply_camera(server, server->grabbed_toplevel);
        }
        return;
    }
    /* Don't stomp client/game cursor every frame */
    if (!(server->active_constraint &&
          server->active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED)) {
        double _sx, _sy;
        bool _il = false;
        struct wlr_surface *_surf = NULL;
        void *_f = surface_at(server, server->cursor->x, server->cursor->y,
                              &_surf, &_sx, &_sy, &_il);
        if (!_f || !_surf)
            cursor_set_themed(server, "left_ptr");
    }
    process_cursor_motion(server, event->time_msec);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener, void *data) {
    struct sberry_server *server = wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    if (server->active_constraint &&
        server->active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED)
        return;
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
    process_cursor_motion(server, event->time_msec);
}

static void handle_cursor_button(struct wl_listener *listener, void *data) {
	struct sberry_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;

	if (event->button == BTN_MIDDLE) {
		if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
			double sx, sy;
			bool is_layer = false;
			struct wlr_surface *surface = NULL;
			void *found = surface_at(server, server->cursor->x, server->cursor->y,
			                         &surface, &sx, &sy, &is_layer);
			if (surface && surface_is_xdg_popup(surface)) {
				wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
				server->last_pointer_surface = surface;
				wlr_seat_pointer_notify_button(server->seat, event->time_msec,
				                               event->button, event->state);
				return;
			}
			if (found && !is_layer) {
				struct sberry_toplevel *toplevel = found;
				focus_toplevel(server, toplevel);
				if (surface)
					wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
				wlr_seat_pointer_notify_button(server->seat, event->time_msec,
				                               event->button, event->state);
				return;
			}
			if (server->overview)
				return;
			server->panning = true;
			server->pan_start_x = server->cursor->x;
			server->pan_start_y = server->cursor->y;
			server->camera_start_cx = server->camera.cx;
			server->camera_start_cy = server->camera.cy;
			return;
		} else {
			server->panning = false;
			wlr_seat_pointer_notify_button(server->seat, event->time_msec,
			                               event->button, event->state);
			return;
		}
	}

	if (event->button == BTN_RIGHT) {
		if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
			double sx, sy;
			bool is_layer = false;
			struct wlr_surface *surface = NULL;
			void *found = surface_at(server, server->cursor->x, server->cursor->y,
			                         &surface, &sx, &sy, &is_layer);

			if (found && !is_layer && server->super_pressed && server->mode == MODE_CANVAS) {
				struct sberry_toplevel *toplevel = found;
				/* Default Super+RMB = bottom-right; edge from pointer if near border */
				uint32_t edges = WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT;
				{
					double z = server->camera.zoom;
					if (z < 0.01) z = 0.01;
					double wx, wy;
					toplevel_world_xy(server, toplevel, &wx, &wy);
					double cx = (server->cursor->x - server->screen_width / 2.0) / z
					            + server->camera.cx;
					double cy = (server->cursor->y - server->screen_height / 2.0) / z
					            + server->camera.cy;
					double lx = cx - wx, ly = cy - wy;
					const double band = 40.0;
					edges = 0;
					if (lx < band) edges |= WLR_EDGE_LEFT;
					else if (lx > toplevel->width - band) edges |= WLR_EDGE_RIGHT;
					if (ly < band) edges |= WLR_EDGE_TOP;
					else if (ly > toplevel->height - band) edges |= WLR_EDGE_BOTTOM;
					if (!edges)
						edges = WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT;
				}
				begin_interactive(server, toplevel, true, edges);
				return;
			}

			/* xdg_popup (context menus): never focus_toplevel — that keyboard_enter
			 * on the parent main surface dismisses the grab instantly. */
			if (surface && surface_is_xdg_popup(surface)) {
				wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
				server->last_pointer_surface = surface;
				wlr_seat_pointer_notify_button(server->seat, event->time_msec,
				                               event->button, event->state);
				return;
			}

			if (found && is_layer) {
				if (surface) {
					wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
					server->last_pointer_surface = surface;
					/* found == server → OR/popup token, not a real layer surface */
					if (found != (void *)server) {
						struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
						struct sberry_layer_surface *ls = found;
						if (kb && surface) {
							int layer = (ls && ls->layer_surface) ? (int)ls->layer : 2;
							if (layer >= 2) {
								wlr_seat_keyboard_notify_enter(server->seat, surface,
								                               kb->keycodes, kb->num_keycodes, &kb->modifiers);
							}
						}
					}
				}
				wlr_seat_pointer_notify_button(server->seat, event->time_msec,
				                               event->button, event->state);
				return;
			}

			if (found && !is_layer) {
				struct sberry_toplevel *toplevel = found;
				/* Avoid re-enter keyboard if already focused — helps menus */
				if (server->focused_toplevel != toplevel)
					focus_toplevel(server, toplevel);
				if (surface)
					wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
				wlr_seat_pointer_notify_button(server->seat, event->time_msec,
				                               event->button, event->state);
				return;
			}

			if (server->overview)
				return;
			server->panning = true;
			server->pan_start_x = server->cursor->x;
			server->pan_start_y = server->cursor->y;
			server->camera_start_cx = server->camera.cx;
			server->camera_start_cy = server->camera.cy;
			return;
		} else {
			if (server->resizing) {
				struct sberry_toplevel *rt = server->grabbed_toplevel;
				if (rt && rt->mapped) {
					if (server->mode == MODE_CANVAS) {
						rt->canvas_mx = rt->mx;
						rt->canvas_my = rt->my;
						rt->canvas_width  = rt->width;
						rt->canvas_height = rt->height;
						rt->has_canvas_pos = true;
					}
					interactive_commit_size(server, rt, true);
					toplevel_update_borders(rt);
					apply_camera(server, rt);
					publish_window_coords(server);
				}
				server->resizing = false;
				server->resize_edges = 0;
				server->grabbed_toplevel = NULL;
			}
			server->panning = false;
			wlr_seat_pointer_notify_button(server->seat, event->time_msec,
			                               event->button, event->state);
			return;
		}
	}

	if (event->button == BTN_LEFT) {
		if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
			if (server->overview) {
				int cell = overview_cell_at(server, server->cursor->x, server->cursor->y);
				overview_leave(server, cell >= 0 ? cell : -1);
				return;
			}
			double sx, sy;
			bool is_layer = false;
			struct wlr_surface *surface = NULL;
			void *found = surface_at(server, server->cursor->x, server->cursor->y,
			                         &surface, &sx, &sy, &is_layer);

			/* Context menus / tooltips: keep focus on the popup surface.
			 * Calling focus_toplevel → keyboard_enter(parent) closes them. */
			if (surface && surface_is_xdg_popup(surface)) {
				wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
				server->last_pointer_surface = surface;
				wlr_seat_pointer_notify_button(server->seat, event->time_msec,
				                               event->button, event->state);
				return;
			}

			if (found) {
				if (is_layer) {
					if (surface) {
						wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
						server->last_pointer_surface = surface;
						if (found != (void *)server) {
							struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
							struct sberry_layer_surface *ls = found;
							if (kb && surface) {
								int layer = (ls && ls->layer_surface) ? (int)ls->layer : 2;
								if (layer >= 2) {
									wlr_seat_keyboard_notify_enter(server->seat, surface,
									                               kb->keycodes, kb->num_keycodes, &kb->modifiers);
								}
							}
						}
					}
					wlr_seat_pointer_notify_button(server->seat, event->time_msec,
					                               event->button, event->state);
					return;
				}
				struct sberry_toplevel *toplevel = found;
				if (server->focused_toplevel != toplevel)
					focus_toplevel(server, toplevel);
				/* Must enter surface before button — Xwayland ignores clicks
				 * without prior pointer enter on that surface. */
				if (surface)
					wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
				/* Super+LMB: canvas free-move; tiling = drag then swap on release */
				if (server->super_pressed) {
					if (server->mode == MODE_TILING) {
						server->grabbed_toplevel = toplevel;
						server->resizing = false;
						server->tiling_drag = true;
						server->grab_x = (server->cursor->x - server->screen_width / 2.0)
						                 / server->camera.zoom + server->camera.cx;
						server->grab_y = (server->cursor->y - server->screen_height / 2.0)
						                 / server->camera.zoom + server->camera.cy;
						server->grab_toplevel_mx = toplevel->mx;
						server->grab_toplevel_my = toplevel->my;
					} else {
						begin_interactive(server, toplevel, false, 0);
					}
				}
				wlr_seat_pointer_notify_button(server->seat, event->time_msec,
				                               event->button, event->state);
				return;
			}
		} else {
			/* Client CSD resize uses LMB — finish interactive resize here */
			if (server->resizing && server->grabbed_toplevel) {
				struct sberry_toplevel *rt = server->grabbed_toplevel;
				if (rt->mapped) {
					if (server->mode == MODE_CANVAS) {
						rt->canvas_mx = rt->mx;
						rt->canvas_my = rt->my;
						rt->canvas_width  = rt->width;
						rt->canvas_height = rt->height;
						rt->has_canvas_pos = true;
					}
					interactive_commit_size(server, rt, true);
					toplevel_update_borders(rt);
					apply_camera(server, rt);
					publish_window_coords(server);
				}
				server->resizing = false;
				server->resize_edges = 0;
				server->grabbed_toplevel = NULL;
			} else if (server->grabbed_toplevel && !server->resizing) {
				if (server->tiling_drag && server->mode == MODE_TILING) {
					double sx2, sy2;
					bool is_layer2 = false;
					struct wlr_surface *surface2 = NULL;
					void *found2 = surface_at(server, server->cursor->x, server->cursor->y,
					                          &surface2, &sx2, &sy2, &is_layer2);
					struct sberry_toplevel *target = NULL;
					if (found2 && !is_layer2 && found2 != server->grabbed_toplevel)
						target = found2;
					if (target)
						clearing_link_swap(server->grabbed_toplevel, target);
					server->grabbed_toplevel = NULL;
					server->tiling_drag = false;
					server_arrange_tiles(server);
				} else {
					server->grabbed_toplevel = NULL;
					server->tiling_drag = false;
				}
			}
			wlr_seat_pointer_notify_button(server->seat, event->time_msec,
			                               event->button, event->state);
			return;
		}
	}

	wlr_seat_pointer_notify_button(server->seat, event->time_msec,
	                               event->button, event->state);
}

static void handle_cursor_axis(struct wl_listener *listener, void *data) {
    struct sberry_server *server = wl_container_of(listener, server, cursor_axis);
    if (server->overview)
        return;
    struct wlr_pointer_axis_event *event = data;
    
    double sx, sy;
    bool is_layer = false;
    struct wlr_surface *surface = NULL;
    void *found = surface_at(server, server->cursor->x, server->cursor->y,
                             &surface, &sx, &sy, &is_layer);
    
    if (found) {
        wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
                                     event->orientation, event->delta,
                                     event->delta_discrete, event->source,
                                     event->relative_direction);
        return;
    }
    
    uint32_t mods = 0;
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
    if (kb)
        mods = wlr_keyboard_get_modifiers(kb);
    
    bool super = (mods & WLR_MODIFIER_LOGO) != 0;
    bool alt   = (mods & WLR_MODIFIER_ALT)  != 0;
    
    if (super && alt) {
        int idx = server->current_clearing_idx;
        if (event->delta < 0) {
            if (idx < 8) idx++;
        } else {
            if (idx > 0) idx--;
        }
        sberry_switch_clearing(server, idx);
        return;
    }
    
    if (super) {
        if (event->delta < 0)
            camera_zoom_at_cursor(server, ZOOM_STEP);
        else
            camera_zoom_at_cursor(server, 1.0 / ZOOM_STEP);
        return;
    }
    
    wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
                                 event->orientation, event->delta,
                                 event->delta_discrete, event->source,
                                 event->relative_direction);
}

static void handle_cursor_frame(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_server *server = wl_container_of(listener, server, cursor_frame);
    wlr_seat_pointer_notify_frame(server->seat);
}

static void handle_output_frame(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_output *output = wl_container_of(listener, output, frame);
    struct sberry_server *server = output->server;

    /* Skip while VT inactive or output disabled (DRM master lost) */
    if (!server->session_is_active)
        return;
    if (!output->wlr_output || !output->wlr_output->enabled)
        return;

    struct wlr_scene_output *scene_output =
        wlr_scene_get_scene_output(server->scene, output->wlr_output);
    if (!scene_output)
        return;

    /* Overview: Electron/GTK/X11 re-commit buffers every frame and wipe
     * dest_size — re-apply visual_scale so windows stay shrunk. */
    if (server->overview) {
        for (int ci = 0; ci < 9; ci++) {
            struct sberry_toplevel *t;
            wl_list_for_each(t, &server->clearings[ci].toplevels, clearing_link) {
                if (t->mapped && !t->fullscreen
                        && t->visual_scale > 0.02 && t->visual_scale < 0.999)
                    toplevel_apply_visual_scale(t, t->visual_scale);
            }
        }
    }

    struct wlr_output_state state;
    wlr_output_state_init(&state);

    if (!wlr_scene_output_build_state(scene_output, &state, NULL)) {
        wlr_output_state_finish(&state);
        return;
    }

    /* Commit can fail with EPERM while logind is taking the seat —
     * that is expected and must not abort the compositor. */
    if (!wlr_output_commit_state(output->wlr_output, &state)) {
        wlr_output_state_finish(&state);
        return;
    }
    wlr_output_state_finish(&state);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void handle_toplevel_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_toplevel *t = wl_container_of(listener, t, destroy);

    /* Xwayland uses its own destroy handler */
    if (t->xwayland_surface)
        return;

    if (t->server->grabbed_toplevel == t)
        t->server->grabbed_toplevel = NULL;

    if (t->server->focused_toplevel == t) {
        t->server->focused_toplevel = NULL;

        struct sberry_clearing *curr_cl = &t->server->clearings[t->server->current_clearing_idx];
        struct sberry_toplevel *next;
        wl_list_for_each(next, &curr_cl->toplevels, clearing_link) {
            if (next != t && next->mapped) {
                focus_toplevel(t->server, next);
                break;
            }
        }
    }

    wl_list_remove(&t->destroy.link);
    wl_list_remove(&t->map.link);
    if (t->unmap.link.prev)
        wl_list_remove(&t->unmap.link);
    wl_list_remove(&t->commit.link);
    if (t->request_fullscreen.link.prev)
        wl_list_remove(&t->request_fullscreen.link);
    if (t->request_move.link.prev)
        wl_list_remove(&t->request_move.link);
    if (t->request_resize.link.prev)
        wl_list_remove(&t->request_resize.link);
    wl_list_remove(&t->link);
    wl_list_remove(&t->clearing_link);

    if (t->foreign_handle) {
        wlr_foreign_toplevel_handle_v1_destroy(t->foreign_handle);
        t->foreign_handle = NULL;
    }
    if (t->decoration_request_mode.link.prev) {
        wl_list_remove(&t->decoration_request_mode.link);
        wl_list_init(&t->decoration_request_mode.link);
    }
    if (t->decoration_destroy.link.prev) {
        wl_list_remove(&t->decoration_destroy.link);
        wl_list_init(&t->decoration_destroy.link);
    }
    t->decoration = NULL;

    if (t->scene_tree) {
        wlr_scene_node_destroy(&t->scene_tree->node);
        t->scene_tree = NULL;
    }

    if (t->server->mode == MODE_TILING) {
        server_arrange_tiles(t->server);
    }

    free(t);
}


/* Re-apply dest_size scale after client buffer commit (Electron/GTK/X11). */
static void handle_surface_commit_rescale(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_toplevel *t = wl_container_of(listener, t, commit);
    if (!t || !t->mapped || t->fullscreen)
        return;
    if (t->visual_scale > 0.02 && t->visual_scale < 0.999
            && !(t->animating && t->anim_zoom)) {
        toplevel_apply_visual_scale(t, t->visual_scale);
    }
}

/* ---------- Xwayland support (wlroots 0.19: associate/dissociate) ---------- */

static void handle_xwayland_surface_map(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_toplevel *t = wl_container_of(listener, t, map);
    struct sberry_server *server = t->server;
    struct wlr_xwayland_surface *xsurface = t->xwayland_surface;

    if (server->overview) {
        wlr_log(WLR_INFO, "overview: reject new xwayland window");
        if (xsurface)
            wlr_xwayland_surface_close(xsurface);
        return;
    }

    t->mapped = true;

    t->width  = xsurface->width  > 0 ? xsurface->width  : 400;
    t->height = xsurface->height > 0 ? xsurface->height : 300;

    if (server->mode == MODE_CANVAS && !t->has_canvas_pos) {
        struct sberry_clearing *cl =
            &server->clearings[server->current_clearing_idx];
        double z = server->camera.zoom;
        if (z < 0.01) z = 0.01;
        double wx = (server->cursor->x - server->screen_width  / 2.0) / z
                    + server->camera.cx;
        double wy = (server->cursor->y - server->screen_height / 2.0) / z
                    + server->camera.cy;
        /* world -> local */
        double nmx = wx - cl->base_mx - t->width  / 2.0;
        double nmy = wy - cl->base_my - t->height / 2.0;
        t->mx = nmx;
        t->my = nmy;
        toplevel_clamp_to_clearing(server, t);
        t->canvas_mx = t->mx;
        t->canvas_my = t->my;
        t->canvas_width  = t->width;
        t->canvas_height = t->height;
        t->has_canvas_pos = true;
        toplevel_anim_spawn(t, t->mx, t->my, t->width, t->height);
    } else if (t->mx == 0.0 && t->my == 0.0) {
        /* local center of current workspace */
        struct sberry_clearing *cl =
            &server->clearings[server->current_clearing_idx];
        t->mx = cl->width  / 2.0 - t->width  / 2.0;
        t->my = cl->height / 2.0 - t->height / 2.0;
    }

    if (!t->surface_tree && xsurface->surface) {
        t->surface_tree = wlr_scene_subsurface_tree_create(t->scene_tree, xsurface->surface);
        wlr_scene_node_set_position(&t->surface_tree->node, BORDER_WIDTH, BORDER_WIDTH);
    }

    toplevel_update_borders(t);
    if (server->mode == MODE_TILING) {
        t->needs_spawn_anim = true;
        server_arrange_tiles(server);
    } else
        apply_camera(server, t);

    /* Show only after coords are valid (no left-edge flash) */
    if (t->scene_tree)
        wlr_scene_node_set_enabled(&t->scene_tree->node, true);

    focus_toplevel(server, t);
    publish_window_coords(server);
}

static void handle_xwayland_surface_unmap(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_toplevel *t = wl_container_of(listener, t, unmap);
    struct sberry_server *server = t->server;
    t->mapped = false;
    t->animating = false;
    focus_fallback(server, t);
    if (t->surface_tree) {
        wlr_scene_node_destroy(&t->surface_tree->node);
        t->surface_tree = NULL;
    }
    if (server->mode == MODE_TILING)
        server_arrange_tiles(server);
}

static void handle_xwayland_associate(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_toplevel *t = wl_container_of(listener, t, associate);
    struct wlr_xwayland_surface *xsurface = t->xwayland_surface;
    if (!xsurface || !xsurface->surface)
        return;

    t->map.notify = handle_xwayland_surface_map;
    wl_signal_add(&xsurface->surface->events.map, &t->map);
    t->unmap.notify = handle_xwayland_surface_unmap;
    wl_signal_add(&xsurface->surface->events.unmap, &t->unmap);
    /* Re-apply visual_scale after every buffer commit (overview / spawn zoom) */
    t->commit.notify = handle_surface_commit_rescale;
    wl_signal_add(&xsurface->surface->events.commit, &t->commit);
}

static void handle_xwayland_dissociate(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_toplevel *t = wl_container_of(listener, t, dissociate);

    if (t->map.link.prev) {
        wl_list_remove(&t->map.link);
        wl_list_init(&t->map.link);
    }
    if (t->unmap.link.prev) {
        wl_list_remove(&t->unmap.link);
        wl_list_init(&t->unmap.link);
    }
    if (t->commit.link.prev) {
        wl_list_remove(&t->commit.link);
        wl_list_init(&t->commit.link);
    }
    t->mapped = false;
    if (t->surface_tree) {
        wlr_scene_node_destroy(&t->surface_tree->node);
        t->surface_tree = NULL;
    }
}

static void handle_xwayland_request_configure(struct wl_listener *listener, void *data) {
    struct sberry_toplevel *t = wl_container_of(listener, t, request_configure);
    struct wlr_xwayland_surface_configure_event *event = data;
    /* During overview keep logical geometry fixed — client redraws still land
     * via visual_scale. Accepting configure here makes .NET/X11 windows jump. */
    if (t->server && t->server->overview) {
        wlr_xwayland_surface_configure(t->xwayland_surface,
            (int16_t)t->mx, (int16_t)t->my, t->width, t->height);
        return;
    }
    /* While we drive interactive resize, ignore client size fights */
    if (t->server && t->server->resizing && t->server->grabbed_toplevel == t) {
        wlr_xwayland_surface_configure(t->xwayland_surface,
            (int16_t)t->mx, (int16_t)t->my, t->width, t->height);
        return;
    }
    wlr_xwayland_surface_configure(t->xwayland_surface,
        event->x, event->y, event->width, event->height);
    t->width  = event->width;
    t->height = event->height;
    if (t->mapped) {
        toplevel_update_borders(t);
        apply_camera(t->server, t);
    }
}

static void handle_xwayland_surface_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_toplevel *t = wl_container_of(listener, t, destroy);

    if (t->server->grabbed_toplevel == t)
        t->server->grabbed_toplevel = NULL;
    if (t->server->focused_toplevel == t)
        t->server->focused_toplevel = NULL;

    if (t->map.link.prev) { wl_list_remove(&t->map.link); wl_list_init(&t->map.link); }
    if (t->unmap.link.prev) { wl_list_remove(&t->unmap.link); wl_list_init(&t->unmap.link); }
    if (t->commit.link.prev) { wl_list_remove(&t->commit.link); wl_list_init(&t->commit.link); }
    if (t->associate.link.prev) { wl_list_remove(&t->associate.link); wl_list_init(&t->associate.link); }
    if (t->dissociate.link.prev) { wl_list_remove(&t->dissociate.link); wl_list_init(&t->dissociate.link); }
    if (t->request_configure.link.prev) { wl_list_remove(&t->request_configure.link); wl_list_init(&t->request_configure.link); }
    if (t->request_move.link.prev) { wl_list_remove(&t->request_move.link); wl_list_init(&t->request_move.link); }
    if (t->request_resize.link.prev) { wl_list_remove(&t->request_resize.link); wl_list_init(&t->request_resize.link); }
    if (t->destroy.link.prev) { wl_list_remove(&t->destroy.link); wl_list_init(&t->destroy.link); }
    wl_list_remove(&t->link);
    wl_list_remove(&t->clearing_link);

    if (t->scene_tree) {
        wlr_scene_node_destroy(&t->scene_tree->node);
        t->scene_tree = NULL;
    }

    if (t->server->mode == MODE_TILING)
        server_arrange_tiles(t->server);

    free(t);
}


/* ---------- Xwayland override-redirect (menus, Telegram media, tooltips) ---------- */
struct sberry_xwayland_or {
    struct sberry_server *server;
    struct wlr_xwayland_surface *xsurface;
    struct wlr_scene_tree *scene_tree;
    struct wlr_scene_tree *surface_tree;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener associate;
    struct wl_listener dissociate;
    struct wl_listener request_configure;
    struct wl_listener destroy;
};

static void xwayland_or_map(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_xwayland_or *orw = wl_container_of(listener, orw, map);
    struct wlr_xwayland_surface *xs = orw->xsurface;
    if (!xs || !xs->surface)
        return;
    if (!orw->surface_tree) {
        orw->surface_tree = wlr_scene_subsurface_tree_create(orw->scene_tree, xs->surface);
    }
    int x = xs->x, y = xs->y;
    int w = xs->width > 0 ? xs->width : 1;
    int h = xs->height > 0 ? xs->height : 1;
    wlr_scene_node_set_position(&orw->scene_tree->node, x, y);
    wlr_scene_node_set_enabled(&orw->scene_tree->node, true);
    wlr_scene_node_raise_to_top(&orw->scene_tree->node);
    /* Acccept geometry so client does not keep reconfiguring */
    wlr_xwayland_surface_configure(xs, x, y, w, h);
}

static void xwayland_or_unmap(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_xwayland_or *orw = wl_container_of(listener, orw, unmap);
    if (orw->scene_tree)
        wlr_scene_node_set_enabled(&orw->scene_tree->node, false);
    if (orw->surface_tree) {
        wlr_scene_node_destroy(&orw->surface_tree->node);
        orw->surface_tree = NULL;
    }
}

static void xwayland_or_associate(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_xwayland_or *orw = wl_container_of(listener, orw, associate);
    struct wlr_xwayland_surface *xs = orw->xsurface;
    if (!xs || !xs->surface)
        return;
    orw->map.notify = xwayland_or_map;
    wl_signal_add(&xs->surface->events.map, &orw->map);
    orw->unmap.notify = xwayland_or_unmap;
    wl_signal_add(&xs->surface->events.unmap, &orw->unmap);
}

static void xwayland_or_dissociate(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_xwayland_or *orw = wl_container_of(listener, orw, dissociate);
    if (orw->map.link.prev) { wl_list_remove(&orw->map.link); wl_list_init(&orw->map.link); }
    if (orw->unmap.link.prev) { wl_list_remove(&orw->unmap.link); wl_list_init(&orw->unmap.link); }
    if (orw->surface_tree) {
        wlr_scene_node_destroy(&orw->surface_tree->node);
        orw->surface_tree = NULL;
    }
}

static void xwayland_or_configure(struct wl_listener *listener, void *data) {
    struct sberry_xwayland_or *orw = wl_container_of(listener, orw, request_configure);
    struct wlr_xwayland_surface_configure_event *event = data;
    wlr_xwayland_surface_configure(orw->xsurface,
        event->x, event->y, event->width, event->height);
    if (orw->scene_tree)
        wlr_scene_node_set_position(&orw->scene_tree->node, event->x, event->y);
}

static void xwayland_or_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_xwayland_or *orw = wl_container_of(listener, orw, destroy);
    if (orw->map.link.prev) { wl_list_remove(&orw->map.link); wl_list_init(&orw->map.link); }
    if (orw->unmap.link.prev) { wl_list_remove(&orw->unmap.link); wl_list_init(&orw->unmap.link); }
    if (orw->associate.link.prev) { wl_list_remove(&orw->associate.link); wl_list_init(&orw->associate.link); }
    if (orw->dissociate.link.prev) { wl_list_remove(&orw->dissociate.link); wl_list_init(&orw->dissociate.link); }
    if (orw->request_configure.link.prev) { wl_list_remove(&orw->request_configure.link); wl_list_init(&orw->request_configure.link); }
    if (orw->destroy.link.prev) { wl_list_remove(&orw->destroy.link); wl_list_init(&orw->destroy.link); }
    if (orw->scene_tree) {
        wlr_scene_node_destroy(&orw->scene_tree->node);
        orw->scene_tree = NULL;
    }
    free(orw);
}

static void handle_new_xwayland_surface(struct wl_listener *listener, void *data) {
    struct sberry_server *server = wl_container_of(listener, server, new_xwayland_surface);
    struct wlr_xwayland_surface *xsurface = data;

    /* Override-redirect: menus, dropdowns, Telegram/X11 media overlay.
     * Must still be in the scene — silent return left them invisible and
     * some clients assert/spam configure until the compositor dies. */
    if (xsurface->override_redirect) {
        struct sberry_xwayland_or *orw = calloc(1, sizeof(*orw));
        if (!orw)
            return;
        orw->server = server;
        orw->xsurface = xsurface;
        /* Above normal windows so media overlays actually show */
        orw->scene_tree = wlr_scene_tree_create(
            server->fullscreen_tree ? server->fullscreen_tree : server->toplevel_tree);
        wlr_scene_node_set_enabled(&orw->scene_tree->node, false);
        wl_list_init(&orw->map.link);
        wl_list_init(&orw->unmap.link);

        orw->associate.notify = xwayland_or_associate;
        wl_signal_add(&xsurface->events.associate, &orw->associate);
        orw->dissociate.notify = xwayland_or_dissociate;
        wl_signal_add(&xsurface->events.dissociate, &orw->dissociate);
        orw->request_configure.notify = xwayland_or_configure;
        wl_signal_add(&xsurface->events.request_configure, &orw->request_configure);
        orw->destroy.notify = xwayland_or_destroy;
        wl_signal_add(&xsurface->events.destroy, &orw->destroy);
        xsurface->data = orw;
        return;
    }

    struct sberry_toplevel *t = calloc(1, sizeof(*t));
    t->server = server;
    t->xwayland_surface = xsurface;
    t->toplevel = NULL;
    wl_list_init(&t->map.link);
    wl_list_init(&t->unmap.link);
    wl_list_init(&t->associate.link);
    wl_list_init(&t->dissociate.link);
    wl_list_init(&t->request_configure.link);
    wl_list_init(&t->destroy.link);
    wl_list_init(&t->commit.link);

    t->scene_tree = wlr_scene_tree_create(server->toplevel_tree);
    t->scene_tree->node.data = t;
    wlr_scene_node_set_enabled(&t->scene_tree->node, false);

    for (int i = 0; i < 4; i++)
        t->border[i] = wlr_scene_rect_create(t->scene_tree, 0, 0, border_unfocused);

    t->mx = 0.0;
    t->my = 0.0;
    t->width  = xsurface->width  > 0 ? xsurface->width  : 400;
    t->height = xsurface->height > 0 ? xsurface->height : 300;

    wl_list_insert(&server->toplevels, &t->link);
    struct sberry_clearing *curr_cl = &server->clearings[server->current_clearing_idx];
    wl_list_insert(&curr_cl->toplevels, &t->clearing_link);

    t->associate.notify = handle_xwayland_associate;
    wl_signal_add(&xsurface->events.associate, &t->associate);

    t->dissociate.notify = handle_xwayland_dissociate;
    wl_signal_add(&xsurface->events.dissociate, &t->dissociate);

    t->request_configure.notify = handle_xwayland_request_configure;
    wl_signal_add(&xsurface->events.request_configure, &t->request_configure);

    t->request_move.notify = handle_request_move;
    wl_signal_add(&xsurface->events.request_move, &t->request_move);
    t->request_resize.notify = handle_request_resize;
    wl_signal_add(&xsurface->events.request_resize, &t->request_resize);

    t->destroy.notify = handle_xwayland_surface_destroy;
    wl_signal_add(&xsurface->events.destroy, &t->destroy);

    xsurface->data = t;
}

static void handle_xwayland_ready(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_server *server = wl_container_of(listener, server, xwayland_ready);
    if (!server->xwayland || !server->xwayland->display_name)
        return;

    const char *dpy = server->xwayland->display_name;
    setenv("DISPLAY", dpy, true);

    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (rt) {
        char path[256];
        snprintf(path, sizeof(path), "%s/sberry-display", rt);
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "%s\n", dpy);
            fclose(f);
        }
    }

    wlr_log(WLR_INFO, "Xwayland ready DISPLAY=%s (exported)", dpy);
}


/* xdg popups (menus, tooltips). Without this → xdg_surface error 3. */
struct sberry_popup {
    struct wl_list link;
    struct sberry_server *server;
    struct wlr_xdg_popup *popup;
    struct wlr_scene_tree *scene_tree;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener map;
    bool configured;
};

/* Hide every xdg_popup while overview is up — Zen/Chromium command palettes
 * and menus stay at full size and "stick out" of scaled cells otherwise. */
static void overview_set_popups_visible(struct sberry_server *server, bool on) {
    if (!server)
        return;
    struct sberry_popup *p;
    wl_list_for_each(p, &server->popups, link) {
        if (p->scene_tree)
            wlr_scene_node_set_enabled(&p->scene_tree->node, on);
    }
}

/* Walk surface → parent chain: is this (or an ancestor) an xdg_popup? */
static bool surface_is_xdg_popup(struct wlr_surface *surface) {
    while (surface) {
        struct wlr_xdg_surface *xdg = wlr_xdg_surface_try_from_wlr_surface(surface);
        if (xdg && xdg->role == WLR_XDG_SURFACE_ROLE_POPUP)
            return true;
        struct wlr_subsurface *sub = wlr_subsurface_try_from_wlr_surface(surface);
        if (!sub)
            break;
        surface = sub->parent;
    }
    return false;
}

/* Activate parent toplevel without keyboard_enter on its main surface —
 * that would dismiss the popup grab. Keyboard stays on the popup. */
static void popup_activate_parent(struct sberry_server *server,
                                  struct sberry_toplevel *t) {
    if (!t || !t->mapped)
        return;
    struct sberry_toplevel *old = server->focused_toplevel;
    server->focused_toplevel = t;
    if (old && old != t) {
        toplevel_update_borders(old);
        if (old->foreign_handle)
            wlr_foreign_toplevel_handle_v1_set_activated(old->foreign_handle, false);
        if (old->xwayland_surface)
            wlr_xwayland_surface_activate(old->xwayland_surface, false);
    }
    toplevel_update_borders(t);
    if (t->foreign_handle)
        wlr_foreign_toplevel_handle_v1_set_activated(t->foreign_handle, true);
    if (t->scene_tree)
        wlr_scene_node_raise_to_top(&t->scene_tree->node);
    if (t->xwayland_surface) {
        wlr_xwayland_surface_activate(t->xwayland_surface, true);
        wlr_xwayland_surface_restack(t->xwayland_surface, NULL, XCB_STACK_MODE_ABOVE);
    }
}

/* Like tinywl/dwl/mango: raise popup, activate parent frame, no seat steal.
 * pointer_enter(1,1) + keyboard_enter on map kills GTK/Qt/Chromium menus. */
static void popup_raise(struct sberry_popup *p) {
    if (!p)
        return;
    if (!p->popup || !p->popup->parent) {
        if (p->scene_tree)
            wlr_scene_node_raise_to_top(&p->scene_tree->node);
        return;
    }
    struct wlr_xdg_surface *px =
        wlr_xdg_surface_try_from_wlr_surface(p->popup->parent);
    if (px && px->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL && px->data) {
        struct sberry_toplevel *t = px->data;
        /* Activate parent (borders/foreign/xwayland) without keyboard_enter */
        if (t && t->mapped && p->server)
            popup_activate_parent(p->server, t);
        if (t && t->scene_tree)
            wlr_scene_node_raise_to_top(&t->scene_tree->node);
        toplevel_borders_raise(t);
    } else if (px && px->role == WLR_XDG_SURFACE_ROLE_POPUP && px->data) {
        struct sberry_popup *pp = px->data;
        if (pp && pp->scene_tree)
            wlr_scene_node_raise_to_top(&pp->scene_tree->node);
    }
    if (p->scene_tree)
        wlr_scene_node_raise_to_top(&p->scene_tree->node);
}

static void handle_popup_map(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_popup *p = wl_container_of(listener, p, map);
    popup_raise(p);
}

static void handle_popup_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_popup *p = wl_container_of(listener, p, commit);
    if (!p || !p->popup || !p->popup->base)
        return;
    struct wlr_xdg_surface *base = p->popup->base;
    if (!base->initialized)
        return;
    /* Keep noctalia menus above everything else in their tree */
    if (p->scene_tree)
        wlr_scene_node_raise_to_top(&p->scene_tree->node);
    if (base->initial_commit || !p->configured) {
        if (p->server && p->server->output_layout) {
            struct wlr_output *out = wlr_output_layout_output_at(
                p->server->output_layout,
                p->server->cursor->x, p->server->cursor->y);
            if (out) {
                struct wlr_box box = {0};
                wlr_output_layout_get_box(p->server->output_layout, out, &box);
                wlr_xdg_popup_unconstrain_from_box(p->popup, &box);
            } else {
                wlr_xdg_surface_schedule_configure(base);
            }
        } else {
            wlr_xdg_surface_schedule_configure(base);
        }
        p->configured = true;
        popup_raise(p);
    }
}

static void handle_popup_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_popup *p = wl_container_of(listener, p, destroy);
    if (p->link.prev)
        wl_list_remove(&p->link);
    if (p->commit.link.prev) wl_list_remove(&p->commit.link);
    if (p->map.link.prev) wl_list_remove(&p->map.link);
    if (p->destroy.link.prev) wl_list_remove(&p->destroy.link);
    free(p);
}

static struct wlr_scene_tree *
find_layer_scene_tree_for_surface(struct sberry_server *server,
                                  struct wlr_surface *surf) {
    if (!server || !surf)
        return NULL;
    /* Walk up subsurface chain to the role surface */
    struct wlr_surface *walk = surf;
    for (int guard = 0; walk && guard < 16; guard++) {
        struct wlr_layer_surface_v1 *pl =
            wlr_layer_surface_v1_try_from_wlr_surface(walk);
        if (pl) {
            for (int i = 0; i < 4; i++) {
                struct sberry_layer_surface *ls;
                wl_list_for_each(ls, &server->layer_surfaces[i], link) {
                    if (ls->layer_surface == pl && ls->scene_layer)
                        return ls->scene_layer->tree;
                }
            }
            return NULL;
        }
        struct wlr_subsurface *sub = wlr_subsurface_try_from_wlr_surface(walk);
        if (!sub)
            break;
        walk = sub->parent;
    }
    return NULL;
}

static void handle_new_xdg_popup(struct wl_listener *listener, void *data) {
    struct sberry_server *server = wl_container_of(listener, server, new_xdg_popup);
    struct wlr_xdg_popup *popup = data;

    struct wlr_surface *parent_surf = popup->parent;
    struct wlr_scene_tree *parent_tree = NULL;

    /* 1) xdg toplevel / nested popup */
    struct wlr_xdg_surface *parent_xdg = parent_surf
        ? wlr_xdg_surface_try_from_wlr_surface(parent_surf) : NULL;
    if (parent_xdg) {
        if (parent_xdg->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL && parent_xdg->data) {
            struct sberry_toplevel *t = parent_xdg->data;
            parent_tree = t->surface_tree ? t->surface_tree : t->scene_tree;
        } else if (parent_xdg->role == WLR_XDG_SURFACE_ROLE_POPUP && parent_xdg->data) {
            struct sberry_popup *pp = parent_xdg->data;
            parent_tree = pp->scene_tree;
        }
    }

    /* 2) layer-shell */
    if (!parent_tree)
        parent_tree = find_layer_scene_tree_for_surface(server, parent_surf);

    /* Mango: popups of BACKGROUND/BOTTOM go to OVERLAY so menus aren't under tiles */
    if (parent_tree && server->layer_tree[0]
            && scene_node_is_desc(&parent_tree->node, &server->layer_tree[0]->node)
            && server->layer_tree[3])
        parent_tree = server->layer_tree[3];
    else if (parent_tree && server->layer_tree[1]
            && scene_node_is_desc(&parent_tree->node, &server->layer_tree[1]->node)
            && server->layer_tree[3])
        parent_tree = server->layer_tree[3];

    if (!parent_tree)
        parent_tree = server->toplevel_tree;

    if (server->layer_tree[3])
        wlr_scene_node_raise_to_top(&server->layer_tree[3]->node);
    if (server->layer_tree[2])
        wlr_scene_node_raise_to_top(&server->layer_tree[2]->node);

    struct sberry_popup *p = calloc(1, sizeof(*p));
    if (!p)
        return;
    p->server = server;
    p->popup = popup;
    p->configured = false;
    p->scene_tree = wlr_scene_xdg_surface_create(parent_tree, popup->base);
    popup->base->data = p;
    wl_list_insert(&server->popups, &p->link);

    if (p->scene_tree) {
        wlr_scene_node_raise_to_top(&p->scene_tree->node);
        if (parent_tree)
            wlr_scene_node_raise_to_top(&parent_tree->node);
        /* Overview: never show full-size palettes/menus over the grid */
        if (server->overview || server->overview_leaving)
            wlr_scene_node_set_enabled(&p->scene_tree->node, false);
    }

    p->commit.notify = handle_popup_commit;
    wl_signal_add(&popup->base->surface->events.commit, &p->commit);
    p->map.notify = handle_popup_map;
    wl_signal_add(&popup->base->surface->events.map, &p->map);
    p->destroy.notify = handle_popup_destroy;
    wl_signal_add(&popup->events.destroy, &p->destroy);
}

static void handle_new_toplevel(struct wl_listener *listener, void *data) {
    struct sberry_server *server = wl_container_of(listener, server, new_toplevel);
    struct wlr_xdg_toplevel *toplevel = data;

    struct sberry_toplevel *t = calloc(1, sizeof(*t));
    t->server = server;
    t->toplevel = toplevel;
    t->xwayland_surface = NULL;

    t->scene_tree = wlr_scene_tree_create(server->toplevel_tree);
    t->scene_tree->node.data = t;
    wlr_scene_node_set_enabled(&t->scene_tree->node, false);

    t->surface_tree = wlr_scene_xdg_surface_create(t->scene_tree, toplevel->base);
    wlr_scene_node_set_position(&t->surface_tree->node, BORDER_WIDTH, BORDER_WIDTH);

    for (int i = 0; i < 4; i++) {
        t->border[i] = wlr_scene_rect_create(t->scene_tree, 0, 0, border_unfocused);
    }
    /* Borders above content from the start (sibling order + raise) */
    toplevel_borders_raise(t);

    wl_list_init(&t->decoration_request_mode.link);
    wl_list_init(&t->decoration_destroy.link);
    t->decoration = NULL;

    toplevel->base->data = t;
    t->mx = 0.0;
    t->my = 0.0;
    t->width = 300;
    t->height = 200;

    wl_list_insert(&server->toplevels, &t->link);

    struct sberry_clearing *curr_cl = &server->clearings[server->current_clearing_idx];
    wl_list_insert(&curr_cl->toplevels, &t->clearing_link);

    // Register window in foreign-toplevel manager for Discord, Electron, taskbars
    if (server->foreign_toplevel_manager) {
        t->foreign_handle = wlr_foreign_toplevel_handle_v1_create(server->foreign_toplevel_manager);
        const char *title = toplevel->title ? toplevel->title : "";
        const char *app_id = toplevel->app_id ? toplevel->app_id : "";
        wlr_foreign_toplevel_handle_v1_set_title(t->foreign_handle, title);
        wlr_foreign_toplevel_handle_v1_set_app_id(t->foreign_handle, app_id);
    }

    t->commit.notify = handle_toplevel_commit;
    wl_signal_add(&toplevel->base->surface->events.commit, &t->commit);

    t->map.notify = handle_map;
    wl_signal_add(&toplevel->base->surface->events.map, &t->map);

    t->unmap.notify = handle_unmap;
    wl_signal_add(&toplevel->base->surface->events.unmap, &t->unmap);

    t->destroy.notify = handle_toplevel_destroy;
    wl_signal_add(&toplevel->events.destroy, &t->destroy);

    t->request_fullscreen.notify = handle_request_fullscreen;
    wl_signal_add(&toplevel->events.request_fullscreen, &t->request_fullscreen);

    t->request_move.notify = handle_request_move;
    wl_signal_add(&toplevel->events.request_move, &t->request_move);
    t->request_resize.notify = handle_request_resize;
    wl_signal_add(&toplevel->events.request_resize, &t->request_resize);
}

static void handle_output_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_output *output = wl_container_of(listener, output, destroy);

    /* Safe if already detached in session-inactive handler */
    if (output->frame.link.prev) {
        wl_list_remove(&output->frame.link);
        wl_list_init(&output->frame.link);
    }
    if (output->destroy.link.prev) {
        wl_list_remove(&output->destroy.link);
        wl_list_init(&output->destroy.link);
    }
    if (output->link.prev) {
        wl_list_remove(&output->link);
        wl_list_init(&output->link);
    }
    free(output);
}


/* Auto-size 3×3 clearings from monitor: 16:9, ~CLEARING_SCALE× screen.
 * Nice overview zoom without fixed 2500×1500 guess. */
static void server_layout_clearings(struct sberry_server *server) {
    int sw = server->screen_width > 0 ? server->screen_width : 1920;
    int sh = server->screen_height > 0 ? server->screen_height : 1080;
    double scale = CLEARING_SCALE;
    if (scale < 1.2) scale = 1.2;
    if (scale > 4.0) scale = 4.0;

    int cw = (int)lround((double)sw * scale);
    int ch = (int)lround((double)cw * 9.0 / 16.0);
    int ch_min = (int)lround((double)sh * scale);
    if (ch < ch_min) {
        ch = ch_min;
        cw = (int)lround((double)ch * 16.0 / 9.0);
    }
    if (cw < 800) cw = 800;
    if (ch < 450) ch = 450;

    /* Small world gap so overview cells don't glue together */
    int gap = (int)lround((double)(cw < ch ? cw : ch) * 0.04);
    if (gap < 40) gap = 40;
    if (gap > 200) gap = 200;

    /* Center 3×3 on world origin (0,0):
     *   col/row 0,1,2 → -1,0,+1
     *   middle cell (ws5, idx 4) center == (0,0)
     * Plane spans -x/-y … +x/+y, not only the positive quadrant. */
    double stride_x = (double)cw + (double)gap;
    double stride_y = (double)ch + (double)gap;
    int idx = 0;
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            struct sberry_clearing *cl = &server->clearings[idx];
            double center_x = (double)(col - 1) * stride_x;
            double center_y = (double)(row - 1) * stride_y;
            double base_mx = center_x - (double)cw * 0.5;
            double base_my = center_y - (double)ch * 0.5;
            cl->base_mx = base_mx;
            cl->base_my = base_my;
            cl->width = cw;
            cl->height = ch;
            if (!cl->visited) {
                cl->saved_cx = center_x;
                cl->saved_cy = center_y;
                cl->saved_zoom = 1.0;
                cl->canvas_cx = center_x;
                cl->canvas_cy = center_y;
                cl->canvas_zoom = 1.0;
            } else {
                double min_x = base_mx, max_x = base_mx + cw;
                double min_y = base_my, max_y = base_my + ch;
                if (cl->saved_cx < min_x) cl->saved_cx = min_x;
                if (cl->saved_cx > max_x) cl->saved_cx = max_x;
                if (cl->saved_cy < min_y) cl->saved_cy = min_y;
                if (cl->saved_cy > max_y) cl->saved_cy = max_y;
            }
            idx++;
        }
    }
    wlr_log(WLR_INFO,
            "clearings: %dx%d 16:9 scale=%.2f gap=%d origin=(0,0) from %dx%d",
            cw, ch, scale, gap, sw, sh);
}

static void handle_new_output(struct wl_listener *listener, void *data) {
    struct sberry_server *server = wl_container_of(listener, server, new_output);
    struct wlr_output *output = data;

    wlr_output_init_render(output, server->allocator, server->renderer);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    
    if (!wl_list_empty(&output->modes)) {
        struct wlr_output_mode *mode, *best = NULL;
        wl_list_for_each(mode, &output->modes, link) {
            if (!best) { best = mode; continue; }
            int best_px = best->width * best->height;
            int cur_px = mode->width * mode->height;
            if (cur_px > best_px || (cur_px == best_px && mode->refresh > best->refresh)) {
                best = mode;
            }
        }
        if (best) {
            wlr_output_state_set_mode(&state, best);
        }
    }
    
    wlr_output_state_set_enabled(&state, true);
    wlr_output_commit_state(output, &state);
    wlr_output_state_finish(&state);

    server->screen_width = output->width;
    server->screen_height = output->height;
    if (server->usable_w <= 0 || server->usable_h <= 0) {
        server->usable_x = 0;
        server->usable_y = 0;
        server->usable_w = output->width;
        server->usable_h = output->height;
    }
    server_layout_clearings(server);
    /* Re-center camera on current clearing after size change */
    {
        struct sberry_clearing *cl = &server->clearings[server->current_clearing_idx];
        if (!cl->visited) {
            server->camera.cx = cl->base_mx + cl->width / 2.0;
            server->camera.cy = cl->base_my + cl->height / 2.0;
            double zx = (double)server->screen_width / cl->width;
            double zy = (double)server->screen_height / cl->height;
            server->camera.zoom = (zx < zy ? zx : zy) * 0.95;
            if (server->camera.zoom < MIN_ZOOM) server->camera.zoom = MIN_ZOOM;
            if (server->camera.zoom > MAX_ZOOM) server->camera.zoom = MAX_ZOOM;
        }
    }
    server_update_usable_area(server);

    wlr_output_layout_add_auto(server->output_layout, output);

    struct sberry_output *sberry_out = calloc(1, sizeof(*sberry_out));
    sberry_out->wlr_output = output;
    sberry_out->server = server;
    sberry_out->frame.notify = handle_output_frame;
    wl_signal_add(&output->events.frame, &sberry_out->frame);

    sberry_out->destroy.notify = handle_output_destroy;
    wl_signal_add(&output->events.destroy, &sberry_out->destroy);

    wl_list_insert(&server->outputs, &sberry_out->link);

    wlr_scene_output_create(server->scene, output);
    struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();
    wlr_output_configuration_head_v1_create(config, output);
    wlr_output_manager_v1_set_configuration(server->output_manager, config);
}

/* Recompute exclusive zones from layer-shell (waybar / noctalia / …).
 * usable_* = desktop area for windows & overview (Hyprland-style reserved). */
static void server_update_usable_area(struct sberry_server *server) {
    int sw = server->screen_width > 0 ? server->screen_width : 1920;
    int sh = server->screen_height > 0 ? server->screen_height : 1080;
    struct wlr_box full = { 0, 0, sw, sh };
    struct wlr_box usable = full;

    for (int layer = 0; layer < 4; layer++) {
        struct sberry_layer_surface *ls;
        wl_list_for_each(ls, &server->layer_surfaces[layer], link) {
            if (!ls->scene_layer || !ls->layer_surface)
                continue;
            if (!ls->layer_surface->initialized)
                continue;
            ls->layer = ls->layer_surface->current.layer;
            /* BACKGROUND display bars (waybar): layout them but do NOT let their
             * exclusive-zone shrink the desktop — user can also set
             * "exclusive-zone": false in waybar config. */
            if (layer == 0) {
                struct wlr_box discard = usable;
                wlr_scene_layer_surface_v1_configure(ls->scene_layer, &full, &discard);
            } else {
                wlr_scene_layer_surface_v1_configure(ls->scene_layer, &full, &usable);
            }
        }
    }

    server->usable_x = usable.x;
    server->usable_y = usable.y;
    server->usable_w = usable.width;
    server->usable_h = usable.height;

    if (server->overview) {
        overview_apply_windows(server);
        for (int i = 0; i < 9; i++)
            overview_place_border_screen(server, i);
        overview_update_hover(server);
    }
}

/* waybar stays under noctalia (same layer-shell layer, different scene z). */
static int layer_ns_rank(const char *ns) {
    if (!ns || !ns[0])
        return 1;
    if (strcmp(ns, "waybar") == 0 || strcmp(ns, "eww-bar") == 0
            || strcmp(ns, "ironbar") == 0 || strcmp(ns, "polybar") == 0)
        return 0;
    if (strstr(ns, "noctalia") != NULL || strstr(ns, "ags") != NULL
            || strstr(ns, "quickshell") != NULL || strstr(ns, "walker") != NULL
            || strstr(ns, "launcher") != NULL || strstr(ns, "notification") != NULL)
        return 2;
    return 1;
}

static void layer_apply_z_order(struct sberry_server *server, int layer) {
    if (layer < 0 || layer > 3)
        return;
    /* Stable order: rank0 (waybar) → rank1 → rank2 (noctalia).
     * raise_to_top in ascending rank so highest rank ends on top. */
    for (int rank = 0; rank <= 2; rank++) {
        struct sberry_layer_surface *ls;
        wl_list_for_each(ls, &server->layer_surfaces[layer], link) {
            if (!ls->scene_layer || !ls->layer_surface)
                continue;
            const char *ns = ls->layer_surface->namespace;
            if (layer_ns_rank(ns) != rank)
                continue;
            wlr_scene_node_raise_to_top(&ls->scene_layer->tree->node);
        }
    }
}

static void handle_layer_surface_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_layer_surface *ls = wl_container_of(listener, ls, commit);
    struct wlr_layer_surface_v1 *wlr_ls = ls->layer_surface;

    if (!wlr_ls->initialized)
        return;
    if (!wlr_ls->output || !ls->scene_layer)
        return;

    int want = (int)wlr_ls->current.layer;
    if (want < 0 || want > 3)
        want = 2;
    const char *ns = wlr_ls->namespace ? wlr_ls->namespace : "";
    if (ns[0] && (strcmp(ns, "waybar") == 0 || strcmp(ns, "eww-bar") == 0
            || strcmp(ns, "ironbar") == 0 || strcmp(ns, "polybar") == 0)) {
        want = 1;
    } else if (want != 0 && ns[0] && (strstr(ns, "wallpaper")
            || strstr(ns, "backdrop") || strstr(ns, "swaybg")
            || strstr(ns, "hyprpaper") || strstr(ns, "mpvpaper")
            || strstr(ns, "wbg") || strcmp(ns, "noctalia-wallpaper") == 0)) {
        want = 0;
    }

    bool layer_moved = (want != (int)ls->layer);
    if (layer_moved && ls->server && !ls->server->overview) {
        wl_list_remove(&ls->link);
        wl_list_init(&ls->link);
        ls->layer = want;
        wlr_scene_node_reparent(&ls->scene_layer->tree->node,
                                ls->server->layer_tree[want]);
        wl_list_insert(&ls->server->layer_surfaces[want], &ls->link);
        if (want >= 2)
            wlr_scene_node_raise_to_top(&ls->scene_layer->tree->node);
    } else {
        ls->layer = want;
    }

    /* Only reconfigure exclusive zones when the committed state can change them.
     * Doing this on every buffer commit (noctalia animates a lot) thrashes
     * pointer focus — hover lasts ~1 frame then dies. */
    bool layout_commit = wlr_ls->current.committed != 0 || layer_moved;
    if (ls->server && layout_commit)
        server_update_usable_area(ls->server);
}

static void handle_layer_surface_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct sberry_layer_surface *ls = wl_container_of(listener, ls, destroy);
    struct sberry_server *server = ls->server;
    wl_list_remove(&ls->destroy.link);
    wl_list_remove(&ls->commit.link);
    wl_list_remove(&ls->link);
    free(ls);
    if (server)
        server_update_usable_area(server);
}

static void handle_new_layer_surface(struct wl_listener *listener, void *data) {
    struct sberry_server *server = wl_container_of(listener, server, new_layer_surface);
    struct wlr_layer_surface_v1 *wlr_ls = data;

    if (!wlr_ls->output) {
        struct wlr_output *o = wlr_output_layout_get_center_output(server->output_layout);
        if (!o) {
            wlr_layer_surface_v1_destroy(wlr_ls);
            return;
        }
        wlr_ls->output = o;
    }

    struct sberry_layer_surface *ls = calloc(1, sizeof(*ls));
    ls->server = server;
    ls->layer_surface = wlr_ls;
    ls->layer = wlr_ls->pending.layer;

    {
        /* Mango-style: trust client layer. Only demote bars/wallpapers. */
        int layer = (int)wlr_ls->pending.layer;
        if (layer < 0 || layer > 3)
            layer = 2;
        const char *ns = wlr_ls->namespace ? wlr_ls->namespace : "";

        if (ns[0] && (strcmp(ns, "waybar") == 0 || strcmp(ns, "eww-bar") == 0
                || strcmp(ns, "ironbar") == 0 || strcmp(ns, "polybar") == 0)) {
            layer = 1;
        } else if (layer != 0 && ns[0] && (strstr(ns, "wallpaper")
                || strstr(ns, "backdrop") || strstr(ns, "swaybg")
                || strstr(ns, "hyprpaper") || strstr(ns, "mpvpaper")
                || strstr(ns, "wbg") || strcmp(ns, "noctalia-wallpaper") == 0)) {
            layer = 0;
        }

        ls->layer = layer;
        ls->scene_layer = wlr_scene_layer_surface_v1_create(server->layer_tree[layer], wlr_ls);
        ls->scene_layer->tree->node.data = ls;
        wlr_log(WLR_INFO, "layer-shell ns='%s' → layer %d", ns, layer);
        wlr_log(WLR_INFO, "layer-shell ns='%s' → layer %d", ns, layer);
    }

    ls->destroy.notify = handle_layer_surface_destroy;
    wl_signal_add(&wlr_ls->events.destroy, &ls->destroy);

    ls->commit.notify = handle_layer_surface_commit;
    wl_signal_add(&wlr_ls->surface->events.commit, &ls->commit);

    wl_list_insert(&server->layer_surfaces[(int)ls->layer], &ls->link);
    layer_apply_z_order(server, (int)ls->layer);
    /* Newest TOP/OVERLAY panel above siblings — never raise BACKGROUND */
    if ((int)ls->layer >= 2 && ls->scene_layer)
        wlr_scene_node_raise_to_top(&ls->scene_layer->tree->node);
    if ((int)ls->layer == 3 && server->layer_tree[3])
        wlr_scene_node_raise_to_top(&server->layer_tree[3]->node);
    /* Keep BACKGROUND tree at the bottom of the scene stack */
    if (server->layer_tree[0]) {
        /* lower = first among siblings under scene root */
        wlr_scene_node_lower_to_bottom(&server->layer_tree[0]->node);
    }
}


static int layer_refresh_timer(void *data) {
    struct sberry_server *server = data;
    if (!server->session_is_active)
        return 500;

    /* External commands for noctalia cycle_command / scripts:
     *   echo layout-cycle > $XDG_RUNTIME_DIR/sberrywm.cmd
     */
    {
        const char *rt = getenv("XDG_RUNTIME_DIR");
        if (rt) {
            char path[256];
            snprintf(path, sizeof(path), "%s/sberrywm.cmd", rt);
            FILE *f = fopen(path, "r");
            if (f) {
                char cmd[64] = {0};
                if (fgets(cmd, sizeof(cmd), f)) {
                    if (strncmp(cmd, "layout-cycle", 12) == 0)
                        sberry_cycle_layout(server);
                    else if (strncmp(cmd, "layout-publish", 14) == 0) {
                        struct wlr_keyboard *k = wlr_seat_get_keyboard(server->seat);
                        if (k)
                            publish_keyboard_layout(server, k);
                    } else if (strncmp(cmd, "workspace-move ", 15) == 0) {
                        int n = atoi(cmd + 15);
                        if (n >= 1 && n <= 9)
                            sberry_move_to_clearing(server, n - 1);
                    } else if (strncmp(cmd, "workspace ", 10) == 0) {
                        int n = atoi(cmd + 10);
                        if (n >= 1 && n <= 9)
                            sberry_switch_clearing(server, n - 1);
                    } else if (strncmp(cmd, "workspace-next", 14) == 0) {
                        sberry_ws_next(server);
                    } else if (strncmp(cmd, "workspace-prev", 14) == 0) {
                        sberry_ws_prev(server);
                    } else if (strncmp(cmd, "publish", 7) == 0) {
                        publish_window_coords(server);
                    }
                }
                fclose(f);
                unlink(path);
            }
        }
    }

    struct sberry_output *out;
    wl_list_for_each(out, &server->outputs, link) {
        if (out->wlr_output && out->wlr_output->enabled)
            wlr_output_schedule_frame(out->wlr_output);
    }
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
    if (kb)
        wlr_seat_keyboard_notify_modifiers(server->seat, &kb->modifiers);

    return 500;
}

static void run_autostart(void) {
    for (size_t i = 0; i < autostart_len; i++) {
        if (autostart[i] && autostart[i][0]) {
            wlr_log(WLR_INFO, "autostart: %s", autostart[i]);
            spawn(autostart[i]);
        }
    }
}

void sberry_quit(struct sberry_server *server) {
    wl_display_terminate(server->display);
}

int main(void) {
    wlr_log_init(WLR_DEBUG, NULL);
    struct sberry_server server = {0};

    server.display = wl_display_create();
    server.backend = wlr_backend_autocreate(wl_display_get_event_loop(server.display), &server.session);
    if (!server.backend) {
        fprintf(stderr, "Failed to create backend\n");
        return 1;
    }

    server.renderer = wlr_renderer_autocreate(server.backend);
    wlr_renderer_init_wl_display(server.renderer, server.display);
    server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);

    server.compositor = wlr_compositor_create(server.display, 6, server.renderer);
    wlr_subcompositor_create(server.display);
    /* moved after seat: wlr_data_device_manager_create(server.display); */

    /* Clipboard: data-control is what wl-clipboard / cliphist need */
    /* moved after seat: wlr_data_control_manager_v1_create(server.display); */
    /* moved after seat: wlr_primary_selection_v1_device_manager_create(server.display); */

    /* Screen capture (grim, wf-recorder, xdg-desktop-portal-wlr) */
    wlr_screencopy_manager_v1_create(server.display);
    wlr_gamma_control_manager_v1_create(server.display);

    /* DMA-BUF + viewporter — required for efficient capture */
    wlr_linux_dmabuf_v1_create_with_renderer(server.display, 4, server.renderer);
    wlr_viewporter_create(server.display);
    wlr_presentation_create(server.display, server.backend, 2);

    server.seat = wlr_seat_create(server.display, "seat0");

    /* Selection / clipboard managers (after seat so clients see a complete seat) */
    wlr_data_device_manager_create(server.display);
    wlr_data_control_manager_v1_create(server.display);
    wlr_primary_selection_v1_device_manager_create(server.display);

    /* Relative pointer + constraints — required for Flatpak/games cursor lock */
    server.relative_pointer_manager =
        wlr_relative_pointer_manager_v1_create(server.display);
    server.pointer_constraints = wlr_pointer_constraints_v1_create(server.display);
    server.active_constraint = NULL;
    server.new_pointer_constraint.notify = handle_new_pointer_constraint;
    wl_signal_add(&server.pointer_constraints->events.new_constraint,
                  &server.new_pointer_constraint);
    wl_list_init(&server.constraint_commit.link);
    wl_list_init(&server.constraint_destroy.link);

    /* Approve client set_selection requests (required — see tinywl) */
    server.request_set_selection.notify = handle_request_set_selection;
    wl_signal_add(&server.seat->events.request_set_selection,
        &server.request_set_selection);
    server.request_set_primary_selection.notify = handle_request_set_primary_selection;
    wl_signal_add(&server.seat->events.request_set_primary_selection,
        &server.request_set_primary_selection);

    server.request_cursor.notify = handle_request_cursor;
    wl_signal_add(&server.seat->events.request_set_cursor, &server.request_cursor);

    server.camera.cx = 0.0;
    server.camera.cy = 0.0;
    server.camera.zoom = 1.0;
    server.canvas_cx = server.camera.cx;
    server.canvas_cy = server.camera.cy;
    server.canvas_zoom = server.camera.zoom;
    server.canvas_saved = true;
    server.mode = MODE_TILING;
    server.tiling_layout = TILING_DWINDLE; // Default layout is Dwindle[cite: 2]
    server.focused_toplevel = NULL;
    server.screen_width = 1920;
    server.screen_height = 1080;
    server.usable_x = 0;
    server.usable_y = 0;
    server.usable_w = 1920;
    server.usable_h = 1080;

    /* Init clearing metadata; geometry filled by server_layout_clearings */
    for (int idx = 0; idx < 9; idx++) {
        server.clearings[idx].visited = false;
        server.clearings[idx].mode = MODE_TILING;
        server.clearings[idx].canvas_saved = false;
        server.clearings[idx].saved_zoom = 1.0;
        server.clearings[idx].canvas_zoom = 1.0;
        wl_list_init(&server.clearings[idx].toplevels);
    }
    server_layout_clearings(&server);

    server.current_clearing_idx = 0;
    struct sberry_clearing *initial_cl = &server.clearings[0];
    server.camera.cx = initial_cl->base_mx + (double)initial_cl->width  / 2.0;
    server.camera.cy = initial_cl->base_my + (double)initial_cl->height / 2.0;

    {
        double zoom_x = (double)server.screen_width  / initial_cl->width;
        double zoom_y = (double)server.screen_height / initial_cl->height;
        server.camera.zoom = (zoom_x < zoom_y ? zoom_x : zoom_y) * 0.95;
        if (server.camera.zoom < MIN_ZOOM) server.camera.zoom = MIN_ZOOM;
        if (server.camera.zoom > MAX_ZOOM) server.camera.zoom = MAX_ZOOM;
    }

    server.output_manager = wlr_output_manager_v1_create(server.display);
    server.output_layout = wlr_output_layout_create(server.display);
    server.xdg_output_manager = wlr_xdg_output_manager_v1_create(server.display, server.output_layout);
    server.scene = wlr_scene_create();
    /* z-order: wallpaper < bottom < windows < TOP(bar) < fullscreen < OVERLAY */
    server.layer_tree[0] = wlr_scene_tree_create(&server.scene->tree);
    server.layer_tree[1] = wlr_scene_tree_create(&server.scene->tree);
    server.toplevel_tree = wlr_scene_tree_create(&server.scene->tree);
    server.layer_tree[2] = wlr_scene_tree_create(&server.scene->tree);
    server.fullscreen_tree = wlr_scene_tree_create(&server.scene->tree);
    server.layer_tree[3] = wlr_scene_tree_create(&server.scene->tree);

    wl_list_init(&server.keyboards);
    server.cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server.cursor, server.output_layout);
    {
        /* Theme from config.h; XCURSOR_* env overrides when set. */
        const char *theme = getenv("XCURSOR_THEME");
        const char *size_s = getenv("XCURSOR_SIZE");
        int csize = XCURSOR_THEME_SIZE;
        if (!theme || !theme[0])
            theme = XCURSOR_THEME_NAME;
        if (size_s && size_s[0]) {
            int v = atoi(size_s);
            if (v > 0 && v < 512)
                csize = v;
        }
        /* Help libxcursor find themes in home */
        {
            const char *home = getenv("HOME");
            if (home && home[0]) {
                char path[1024];
                snprintf(path, sizeof(path),
                    "%s/.icons:%s/.local/share/icons:/usr/share/icons",
                    home, home);
                setenv("XCURSOR_PATH", path, 0);
            }
        }
        server.cursor_mgr = wlr_xcursor_manager_create(theme, csize);
        bool loaded = wlr_xcursor_manager_load(server.cursor_mgr, 1);
        if (loaded)
            wlr_xcursor_manager_load(server.cursor_mgr, 2); /* HiDPI */
        if (!loaded) {
            wlr_log(WLR_ERROR,
                "xcursor theme '%s' size %d failed — check folder name under ~/.icons",
                theme, csize);
            wlr_xcursor_manager_destroy(server.cursor_mgr);
            /* last resort: still try default, but log loudly */
            server.cursor_mgr = wlr_xcursor_manager_create(NULL, csize);
            wlr_xcursor_manager_load(server.cursor_mgr, 1);
        } else {
            wlr_log(WLR_INFO, "xcursor theme: '%s' size %d", theme, csize);
        }
        /* Force env so clients inherit the same theme (overwrite) */
        setenv("XCURSOR_THEME", theme, 1);
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", csize);
            setenv("XCURSOR_SIZE", buf, 1);
        }
        server.cursor_size_loaded = csize;
        cursor_set_themed(&server, "left_ptr");
    }

    server.cursor_shape_manager = wlr_cursor_shape_manager_v1_create(server.display, 1);
    if (server.cursor_shape_manager) {
        server.request_set_cursor_shape.notify = handle_request_set_cursor_shape;
        wl_signal_add(&server.cursor_shape_manager->events.request_set_shape,
                      &server.request_set_cursor_shape);
    }
    server.last_pointer_surface = NULL;

    server.cursor_motion.notify = handle_cursor_motion;
    wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
    server.cursor_motion_absolute.notify = handle_cursor_motion_absolute;
    wl_signal_add(&server.cursor->events.motion_absolute, &server.cursor_motion_absolute);
    server.cursor_button.notify = handle_cursor_button;
    wl_signal_add(&server.cursor->events.button, &server.cursor_button);
    server.cursor_axis.notify = handle_cursor_axis;
    wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
    server.cursor_frame.notify = handle_cursor_frame;
    wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

    server.new_input.notify = handle_new_input;
    wl_signal_add(&server.backend->events.new_input, &server.new_input);

    server.xdg_shell = wlr_xdg_shell_create(server.display, 6);
    server.new_toplevel.notify = handle_new_toplevel;
    wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_toplevel);
    server.new_xdg_popup.notify = handle_new_xdg_popup;
    wl_signal_add(&server.xdg_shell->events.new_popup, &server.new_xdg_popup);

    server.xdg_decoration_manager = wlr_xdg_decoration_manager_v1_create(server.display);
    server.new_xdg_decoration.notify = handle_new_xdg_decoration;
    wl_signal_add(&server.xdg_decoration_manager->events.new_toplevel_decoration, &server.new_xdg_decoration);

    // Required for Discord, Electron apps, and taskbars (Noctalia)[cite: 2]
    server.foreign_toplevel_manager = wlr_foreign_toplevel_manager_v1_create(server.display);

    server.new_output.notify = handle_new_output;
    wl_signal_add(&server.backend->events.new_output, &server.new_output);

    wl_list_init(&server.toplevels);
    wl_list_init(&server.outputs);
    wl_list_init(&server.popups);

    server.layer_shell = wlr_layer_shell_v1_create(server.display, 4);
    server.new_layer_surface.notify = handle_new_layer_surface;
    wl_signal_add(&server.layer_shell->events.new_surface, &server.new_layer_surface);

    for (int i = 0; i < 4; i++)
        wl_list_init(&server.layer_surfaces[i]);

    const char *socket = wl_display_add_socket_auto(server.display);
    if (!socket) {
        fprintf(stderr, "Failed to open socket\n");
        return 1;
    }

    if (!wlr_backend_start(server.backend)) {
        fprintf(stderr, "Failed to start backend\n");
        return 1;
    }

    server.session_is_active = true; /* assume active until told otherwise */
    if (server.session) {
        server.session_active.notify = handle_session_active;
        wl_signal_add(&server.session->events.active, &server.session_active);
        wlr_log(WLR_INFO, "libseat/session available — VT switch enabled");
    } else {
        wlr_log(WLR_ERROR,
            "No session (seatd/logind). VT switch and chvt will crash or no-op. "
            "Start seatd or run under a logind graphical session.");
    }

        /* Xwayland after socket+backend. Non-lazy so DISPLAY is ready for X11 apps. */
    server.xwayland = wlr_xwayland_create(server.display, server.compositor, false);
    if (server.xwayland) {
        wlr_xwayland_set_seat(server.xwayland, server.seat);
        server.new_xwayland_surface.notify = handle_new_xwayland_surface;
        wl_signal_add(&server.xwayland->events.new_surface, &server.new_xwayland_surface);
        server.xwayland_ready.notify = handle_xwayland_ready;
        wl_signal_add(&server.xwayland->events.ready, &server.xwayland_ready);
        wlr_log(WLR_INFO, "Xwayland starting (eager)");
    } else {
        wlr_log(WLR_ERROR, "Failed to create Xwayland");
    }
    wlr_log(WLR_INFO, "sberrywm build: v39 (overview 3x3 hover edges)");

    setenv("WAYLAND_DISPLAY", socket, true);
    printf("strawberrywm started on WAYLAND_DISPLAY=%s\n", socket);

    server.kb_layout_index = 0;
    server.layer_timer = wl_event_loop_add_timer(
        wl_display_get_event_loop(server.display),
        layer_refresh_timer, &server);
    if (server.layer_timer)
        wl_event_source_timer_update(server.layer_timer, 500);

    server.animating_count = 0;
    server.anim_timer = wl_event_loop_add_timer(
        wl_display_get_event_loop(server.display),
        anim_timer_cb, &server);

    /* Pump events until Xwayland ready sets DISPLAY (max ~3s) */
    if (server.xwayland) {
        struct wl_event_loop *loop = wl_display_get_event_loop(server.display);
        for (int n = 0; n < 60; n++) {
            const char *d = getenv("DISPLAY");
            if (d && d[0])
                break;
            wl_event_loop_dispatch(loop, 50);
            wl_display_flush_clients(server.display);
        }
        if (getenv("DISPLAY"))
            wlr_log(WLR_INFO, "DISPLAY ready before autostart: %s", getenv("DISPLAY"));
        else
            wlr_log(WLR_ERROR, "DISPLAY not ready yet — X11 apps may need manual export");
    }

    run_autostart();

    wl_display_run(server.display);

    /* Detach ALL seat/signal listeners BEFORE wl_display_destroy.
     * Otherwise wlr_seat_destroy asserts:
     *   wl_list_empty(&seat->events.request_set_selection.listener_list) */
    if (server.request_set_selection.link.prev)
        wl_list_remove(&server.request_set_selection.link);
    if (server.request_set_primary_selection.link.prev)
        wl_list_remove(&server.request_set_primary_selection.link);
    if (server.request_cursor.link.prev)
        wl_list_remove(&server.request_cursor.link);

    if (server.new_toplevel.link.prev)
        wl_list_remove(&server.new_toplevel.link);
    if (server.new_xdg_popup.link.prev)
        wl_list_remove(&server.new_xdg_popup.link);
    if (server.new_output.link.prev)
        wl_list_remove(&server.new_output.link);
    if (server.new_input.link.prev)
        wl_list_remove(&server.new_input.link);
    if (server.new_layer_surface.link.prev)
        wl_list_remove(&server.new_layer_surface.link);
    if (server.new_xdg_decoration.link.prev)
        wl_list_remove(&server.new_xdg_decoration.link);
    if (server.new_xwayland_surface.link.prev)
        wl_list_remove(&server.new_xwayland_surface.link);
    if (server.xwayland_ready.link.prev)
        wl_list_remove(&server.xwayland_ready.link);

    if (server.cursor_motion.link.prev)
        wl_list_remove(&server.cursor_motion.link);
    if (server.cursor_motion_absolute.link.prev)
        wl_list_remove(&server.cursor_motion_absolute.link);
    if (server.cursor_button.link.prev)
        wl_list_remove(&server.cursor_button.link);
    if (server.cursor_axis.link.prev)
        wl_list_remove(&server.cursor_axis.link);
    if (server.cursor_frame.link.prev)
        wl_list_remove(&server.cursor_frame.link);

    struct sberry_output *output, *tmp_output;
    wl_list_for_each_safe(output, tmp_output, &server.outputs, link) {
        if (output->frame.link.prev)
            wl_list_remove(&output->frame.link);
        if (output->destroy.link.prev)
            wl_list_remove(&output->destroy.link);
        if (output->link.prev)
            wl_list_remove(&output->link);
        free(output);
    }

    struct sberry_keyboard *kb, *tmp_kb;
    wl_list_for_each_safe(kb, tmp_kb, &server.keyboards, link) {
        if (kb->key.link.prev)
            wl_list_remove(&kb->key.link);
        if (kb->modifiers.link.prev)
            wl_list_remove(&kb->modifiers.link);
        if (kb->destroy.link.prev)
            wl_list_remove(&kb->destroy.link);
        if (kb->link.prev)
            wl_list_remove(&kb->link);
        free(kb);
    }

    if (server.session && server.session_active.link.prev)
        wl_list_remove(&server.session_active.link);
    if (server.anim_timer)
        wl_event_source_remove(server.anim_timer);
    if (server.layer_timer)
        wl_event_source_remove(server.layer_timer);

    wl_display_destroy(server.display);
    return 0;
}
