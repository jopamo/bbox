#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/randr.h>

#include "event.h"
#include "xcb_utils.h"

extern void xcb_stubs_reset(void);
extern void atoms_init(xcb_connection_t* conn);
extern bool xcb_stubs_enqueue_queued_event(xcb_generic_event_t* ev);
extern bool xcb_stubs_enqueue_event(xcb_generic_event_t* ev);
extern size_t xcb_stubs_queued_event_len(void);
extern size_t xcb_stubs_event_len(void);

static xcb_generic_event_t* make_event(uint8_t type) {
    xcb_generic_event_t* ev = calloc(1, sizeof(*ev));
    ev->response_type = type;
    return ev;
}

static void setup_server(server_t* s) {
    memset(s, 0, sizeof(*s));
    s->conn = xcb_connect(NULL, NULL);
    atoms_init(s->conn);
    arena_init(&s->tick_arena, 1024);

    small_vec_init(&s->buckets.map_requests);
    small_vec_init(&s->buckets.unmap_notifies);
    small_vec_init(&s->buckets.destroy_notifies);
    small_vec_init(&s->buckets.key_presses);
    small_vec_init(&s->buckets.button_events);
    small_vec_init(&s->buckets.client_messages);

    hash_map_init(&s->buckets.expose_regions);
    hash_map_init(&s->buckets.configure_requests);
    hash_map_init(&s->buckets.configure_notifies);
    hash_map_init(&s->buckets.destroyed_windows);
    small_vec_init(&s->buckets.property_fifo);
    hash_map_init(&s->buckets.property_lww);
    hash_map_init(&s->buckets.motion_notifies);
    small_vec_init(&s->buckets.pointer_events);
    small_vec_init(&s->buckets.restack_requests);
}

static void cleanup_server(server_t* s) {
    small_vec_destroy(&s->buckets.map_requests);
    small_vec_destroy(&s->buckets.unmap_notifies);
    small_vec_destroy(&s->buckets.destroy_notifies);
    small_vec_destroy(&s->buckets.key_presses);
    small_vec_destroy(&s->buckets.button_events);
    small_vec_destroy(&s->buckets.client_messages);

    hash_map_destroy(&s->buckets.expose_regions);
    hash_map_destroy(&s->buckets.configure_requests);
    hash_map_destroy(&s->buckets.configure_notifies);
    hash_map_destroy(&s->buckets.destroyed_windows);
    small_vec_destroy(&s->buckets.property_fifo);
    hash_map_destroy(&s->buckets.property_lww);
    hash_map_destroy(&s->buckets.motion_notifies);
    small_vec_destroy(&s->buckets.pointer_events);
    small_vec_destroy(&s->buckets.restack_requests);

    arena_destroy(&s->tick_arena);
    xcb_disconnect(s->conn);
}

static void test_event_ingest_bounded(void) {
    server_t s;
    setup_server(&s);
    xcb_stubs_reset();

    const size_t extra = 4;
    for (size_t i = 0; i < MAX_EVENTS_PER_TICK + extra; i++) {
        assert(xcb_stubs_enqueue_queued_event(make_event(XCB_KEY_PRESS)));
    }

    event_ingest(&s, false);

    assert(s.buckets.ingested == MAX_EVENTS_PER_TICK);
    assert(s.x_poll_immediate == true);
    assert(xcb_stubs_queued_event_len() == extra);

    printf("test_event_ingest_bounded passed\n");
    xcb_stubs_reset();
    cleanup_server(&s);
}

static void test_event_ingest_drains_all_when_ready(void) {
    server_t s;
    setup_server(&s);
    xcb_stubs_reset();

    assert(xcb_stubs_enqueue_queued_event(make_event(XCB_KEY_PRESS)));
    assert(xcb_stubs_enqueue_event(make_event(XCB_BUTTON_PRESS)));

    event_ingest(&s, true);

    assert(s.buckets.ingested == 2);
    assert(s.x_poll_immediate == false);
    assert(xcb_stubs_queued_event_len() == 0);
    assert(xcb_stubs_event_len() == 0);

    printf("test_event_ingest_drains_all_when_ready passed\n");
    xcb_stubs_reset();
    cleanup_server(&s);
}

static void test_event_ingest_coalesces_configure_request(void) {
    server_t s;
    setup_server(&s);
    xcb_stubs_reset();

    xcb_window_t win = 0x12345;

    // Event 1: X, Y, WIDTH
    xcb_configure_request_event_t* ev1 = calloc(1, sizeof(*ev1));
    ev1->response_type = XCB_CONFIGURE_REQUEST;
    ev1->window = win;
    ev1->value_mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH;
    ev1->x = 100;
    ev1->y = 200;
    ev1->width = 300;

    // Event 2: HEIGHT (same window)
    xcb_configure_request_event_t* ev2 = calloc(1, sizeof(*ev2));
    ev2->response_type = XCB_CONFIGURE_REQUEST;
    ev2->window = win;
    ev2->value_mask = XCB_CONFIG_WINDOW_HEIGHT;
    ev2->height = 400;

    assert(xcb_stubs_enqueue_queued_event((xcb_generic_event_t*)ev1));
    assert(xcb_stubs_enqueue_queued_event((xcb_generic_event_t*)ev2));

    event_ingest(&s, false);

    assert(hash_map_size(&s.buckets.configure_requests) == 1);

    pending_config_t* pc = hash_map_get(&s.buckets.configure_requests, win);
    assert(pc != NULL);
    assert(pc->mask ==
           (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT));
    assert(pc->x == 100);
    assert(pc->y == 200);
    assert(pc->width == 300);
    assert(pc->height == 400);
    assert(s.buckets.coalesced == 1);

    printf("test_event_ingest_coalesces_configure_request passed\n");
    xcb_stubs_reset();
    cleanup_server(&s);
}

static void test_event_ingest_coalesces_configure_request_split(void) {
    server_t s;
    setup_server(&s);
    xcb_stubs_reset();

    xcb_window_t win = 0x12345;

    // Event with both geometry and stacking
    xcb_configure_request_event_t* ev1 = calloc(1, sizeof(*ev1));
    ev1->response_type = XCB_CONFIGURE_REQUEST;
    ev1->window = win;
    ev1->value_mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_STACK_MODE;
    ev1->x = 100;
    ev1->stack_mode = XCB_STACK_MODE_ABOVE;

    assert(xcb_stubs_enqueue_queued_event((xcb_generic_event_t*)ev1));

    event_ingest(&s, false);

    // Geometry part should be in configure_requests
    assert(hash_map_size(&s.buckets.configure_requests) == 1);
    pending_config_t* pc = hash_map_get(&s.buckets.configure_requests, win);
    assert(pc != NULL);
    assert(pc->mask == XCB_CONFIG_WINDOW_X);
    assert(pc->x == 100);

    // Stacking part should be in restack_requests
    assert(s.buckets.restack_requests.length == 1);
    pending_restack_t* pr = s.buckets.restack_requests.items[0];
    assert(pr->window == win);
    assert(pr->mask == XCB_CONFIG_WINDOW_STACK_MODE);
    assert(pr->stack_mode == XCB_STACK_MODE_ABOVE);

    printf("test_event_ingest_coalesces_configure_request_split passed\n");
    xcb_stubs_reset();
    cleanup_server(&s);
}

static void test_event_ingest_coalesces_randr(void) {
    server_t s;
    setup_server(&s);
    xcb_stubs_reset();

    s.randr_supported = true;
    s.randr_event_base = 100;

    // Event 1: Width=800, Height=600
    xcb_randr_screen_change_notify_event_t* ev1 = calloc(1, sizeof(*ev1));
    ev1->response_type = (uint8_t)(s.randr_event_base + XCB_RANDR_SCREEN_CHANGE_NOTIFY);
    ev1->width = 800;
    ev1->height = 600;

    // Event 2: Width=1024, Height=768
    xcb_randr_screen_change_notify_event_t* ev2 = calloc(1, sizeof(*ev2));
    ev2->response_type = (uint8_t)(s.randr_event_base + XCB_RANDR_SCREEN_CHANGE_NOTIFY);
    ev2->width = 1024;
    ev2->height = 768;

    assert(xcb_stubs_enqueue_queued_event((xcb_generic_event_t*)ev1));
    assert(xcb_stubs_enqueue_queued_event((xcb_generic_event_t*)ev2));

    event_ingest(&s, false);

    assert(s.buckets.randr_dirty == true);
    assert(s.buckets.randr_width == 1024);
    assert(s.buckets.randr_height == 768);
    assert(s.buckets.coalesced == 1);

    printf("test_event_ingest_coalesces_randr passed\n");
    xcb_stubs_reset();
    cleanup_server(&s);
}

static void test_event_ingest_coalesces_pointer_notify(void) {
    server_t s;
    setup_server(&s);
    xcb_stubs_reset();

    // Two EnterNotify
    xcb_enter_notify_event_t* e1 = calloc(1, sizeof(*e1));
    e1->response_type = XCB_ENTER_NOTIFY;
    e1->event = 0x111;

    xcb_enter_notify_event_t* e2 = calloc(1, sizeof(*e2));
    e2->response_type = XCB_ENTER_NOTIFY;
    e2->event = 0x222;

    // Two LeaveNotify
    xcb_leave_notify_event_t* l1 = calloc(1, sizeof(*l1));
    l1->response_type = XCB_LEAVE_NOTIFY;
    l1->event = 0x333;

    xcb_leave_notify_event_t* l2 = calloc(1, sizeof(*l2));
    l2->response_type = XCB_LEAVE_NOTIFY;
    l2->event = 0x444;

    assert(xcb_stubs_enqueue_queued_event((xcb_generic_event_t*)e1));
    assert(xcb_stubs_enqueue_queued_event((xcb_generic_event_t*)e2));
    assert(xcb_stubs_enqueue_queued_event((xcb_generic_event_t*)l1));
    assert(xcb_stubs_enqueue_queued_event((xcb_generic_event_t*)l2));

    event_ingest(&s, false);

    assert(s.buckets.pointer_events.length == 4);
    assert(((xcb_enter_notify_event_t*)s.buckets.pointer_events.items[0])->event == 0x111);
    assert(((xcb_enter_notify_event_t*)s.buckets.pointer_events.items[1])->event == 0x222);
    assert(((xcb_leave_notify_event_t*)s.buckets.pointer_events.items[2])->event == 0x333);
    assert(((xcb_leave_notify_event_t*)s.buckets.pointer_events.items[3])->event == 0x444);

    printf("test_event_ingest_coalesces_pointer_notify passed\n");
    xcb_stubs_reset();
    cleanup_server(&s);
}

static void test_event_ingest_dispatches_colormap_notify(void) {
    server_t s;
    setup_server(&s);
    xcb_stubs_reset();

    xcb_colormap_notify_event_t* ev = calloc(1, sizeof(*ev));
    ev->response_type = XCB_COLORMAP_NOTIFY;
    ev->window = 0x123;
    ev->colormap = 0x456;

    assert(xcb_stubs_enqueue_queued_event((xcb_generic_event_t*)ev));

    event_ingest(&s, false);

    assert(s.buckets.ingested == 1);

    printf("test_event_ingest_dispatches_colormap_notify passed\n");
    xcb_stubs_reset();
    cleanup_server(&s);
}

static void test_event_ingest_coalesces_motion_notify(void) {
    server_t s;
    setup_server(&s);
    xcb_stubs_reset();

    xcb_window_t win = 0x999;

    // Create 10 MotionNotify events
    for (int i = 0; i < 10; i++) {
        xcb_motion_notify_event_t* ev = calloc(1, sizeof(*ev));
        ev->response_type = XCB_MOTION_NOTIFY;
        ev->event = win;  // Same window
        ev->event_x = i * 10;
        ev->event_y = i * 10;
        ev->time = 1000 + i;
        assert(xcb_stubs_enqueue_queued_event((xcb_generic_event_t*)ev));
    }

    event_ingest(&s, false);

    // Should have 1 entry in hash map
    assert(hash_map_size(&s.buckets.motion_notifies) == 1);

    // Should have 9 coalesced events
    assert(s.buckets.coalesced == 9);

    // The one kept should be the last one (x=90, y=90)
    xcb_motion_notify_event_t* final_ev = hash_map_get(&s.buckets.motion_notifies, win);
    assert(final_ev != NULL);
    assert(final_ev->event_x == 90);
    assert(final_ev->event_y == 90);

    printf("test_event_ingest_coalesces_motion_notify passed\n");
    xcb_stubs_reset();
    cleanup_server(&s);
}

static void test_event_ingest_property_notify_split(void) {
    server_t s;
    setup_server(&s);
    xcb_stubs_reset();

    atoms.WM_HINTS = 100;
    atoms.WM_NAME = 101;
    xcb_window_t win = 0xABC;

    // Must-queue atom
    xcb_property_notify_event_t* ev1 = calloc(1, sizeof(*ev1));
    ev1->response_type = XCB_PROPERTY_NOTIFY;
    ev1->window = win;
    ev1->atom = atoms.WM_HINTS;

    // LWW atom
    xcb_property_notify_event_t* ev2 = calloc(1, sizeof(*ev2));
    ev2->response_type = XCB_PROPERTY_NOTIFY;
    ev2->window = win;
    ev2->atom = atoms.WM_NAME;
    ev2->state = 0;

    // Another LWW atom (same win/atom)
    xcb_property_notify_event_t* ev3 = calloc(1, sizeof(*ev3));
    ev3->response_type = XCB_PROPERTY_NOTIFY;
    ev3->window = win;
    ev3->atom = atoms.WM_NAME;
    ev3->state = 1;

    assert(xcb_stubs_enqueue_queued_event((xcb_generic_event_t*)ev1));
    assert(xcb_stubs_enqueue_queued_event((xcb_generic_event_t*)ev2));
    assert(xcb_stubs_enqueue_queued_event((xcb_generic_event_t*)ev3));

    event_ingest(&s, false);

    assert(s.buckets.property_fifo.length == 1);
    assert(((xcb_property_notify_event_t*)s.buckets.property_fifo.items[0])->atom == atoms.WM_HINTS);

    assert(hash_map_size(&s.buckets.property_lww) == 1);
    uint64_t key = ((uint64_t)win << 32) | atoms.WM_NAME;
    xcb_property_notify_event_t* final_prop = hash_map_get(&s.buckets.property_lww, key);
    assert(final_prop != NULL);
    assert(final_prop->state == 1);
    assert(s.buckets.coalesced == 1);

    printf("test_event_ingest_property_notify_split passed\n");
    xcb_stubs_reset();
    cleanup_server(&s);
}

int main(void) {
    test_event_ingest_bounded();
    test_event_ingest_drains_all_when_ready();
    test_event_ingest_coalesces_configure_request();
    test_event_ingest_coalesces_configure_request_split();
    test_event_ingest_coalesces_randr();
    test_event_ingest_coalesces_pointer_notify();
    test_event_ingest_dispatches_colormap_notify();
    test_event_ingest_coalesces_motion_notify();
    test_event_ingest_property_notify_split();
    return 0;
}