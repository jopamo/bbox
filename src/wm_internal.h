/*
 * wm_internal.h - Private shared definitions for the Window Manager module.
 *
 * This header exposes internal functions that are shared between `wm.c`
 * and its helper modules (wm_dirty.c, wm_input.c, etc.) but should not
 * be exposed to the rest of the application (main, event loop).
 */

#ifndef WM_INTERNAL_H
#define WM_INTERNAL_H

#include <math.h>
#include <xcb/randr.h>

#include "event.h"
#include "hxm.h"

#define MIN_FRAME_SIZE 32
#define MAX_FRAME_SIZE 65535

/* RandR refresh helpers */
static inline double randr_mode_hz(const xcb_randr_mode_info_t* mi) {
    if (!mi) return 0.0;
    if (mi->htotal == 0 || mi->vtotal == 0 || mi->dot_clock == 0) return 0.0;

    double hz = ((double)mi->dot_clock * 1000.0) / ((double)mi->htotal * (double)mi->vtotal);

    if (mi->mode_flags & XCB_RANDR_MODE_FLAG_INTERLACE) hz *= 2.0;
    if (mi->mode_flags & XCB_RANDR_MODE_FLAG_DOUBLE_SCAN) hz *= 0.5;
    // vscan field not available in this version of xcb-randr
    // if (mi->vscan > 1) hz /= (double)mi->vscan;

    return hz;
}

static inline uint64_t hz_to_refresh_ns(double hz) {
    if (!(hz > 1.0 && hz < 2000.0)) return 0;

    double ns = 1e9 / hz;

    // clamp to stay sane under weird EDID/mode tables
    ns = HXM_CLAMP(ns, 1000000.0, 50000000.0);

    return (uint64_t)(ns + 0.5);
}

static inline const xcb_randr_mode_info_t* randr_cache_find_mode(const server_t* s, xcb_randr_mode_t mode) {
    if (!s || !s->randr_cache.modes) return NULL;
    for (uint32_t i = 0; i < s->randr_cache.modes_len; i++) {
        if (s->randr_cache.modes[i].id == mode) return &s->randr_cache.modes[i];
    }
    return NULL;
}

static inline bool rect_contains_point(const rect_t* r, int16_t x, int16_t y) {
    if (!r) return false;
    int32_t rx2 = r->x + (int32_t)r->w;
    int32_t ry2 = r->y + (int32_t)r->h;
    return x >= r->x && y >= r->y && x < rx2 && y < ry2;
}

static inline uint64_t server_refresh_ns_for_point(server_t* s, int16_t px, int16_t py) {
    if (!s || !s->monitors || s->monitor_count == 0) return 8ull * 1000ull * 1000ull;

    for (uint32_t i = 0; i < s->monitor_count; i++) {
        if (!rect_contains_point(&s->monitors[i].geom, px, py)) continue;
        return s->monitors[i].refresh_ns ? s->monitors[i].refresh_ns : 8ull * 1000ull * 1000ull;
    }

    return 8ull * 1000ull * 1000ull;
}

static inline uint64_t interaction_spacing_ns(uint64_t refresh_ns) {
    uint64_t half = refresh_ns / 2;
    return HXM_CLAMP(half, 1000000ull, 8000000ull);
}

uint32_t wm_clean_mods(uint16_t state);

// Exposed interaction logic
void wm_start_interaction(server_t* s, handle_t h, client_hot_t* hot, bool start_move, int resize_dir, int16_t root_x,
                          int16_t root_y, uint32_t time);

void wm_client_set_maximize(server_t* s, client_hot_t* hot, bool max_horz, bool max_vert);

void wm_publish_workarea(server_t* s, const rect_t* wa);
void wm_set_showing_desktop(server_t* s, bool show);
void wm_install_client_colormap(server_t* s, client_hot_t* hot);
void wm_update_monitors(server_t* s);
void wm_get_monitor_geometry(server_t* s, client_hot_t* hot, rect_t* out_geom);
void wm_set_frame_extents_for_window(server_t* s, xcb_window_t win, bool undecorated);
void randr_request_snapshot(server_t* s);

#endif
