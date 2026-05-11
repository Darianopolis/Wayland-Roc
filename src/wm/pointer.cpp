#include "internal.hpp"

#include <core/math.hpp>

static vec2f32 cursor_size = {16, 16};

static
void update_visual(WmSeat* seat)
{
    scene_tree_set_translation(seat->pointer.tree.get(), seat->pointer.position);
}

void wm_init_pointer(WmSeat* seat)
{
    seat->pointer.tree = scene_tree_create();

    scene_tree_place_above(wm_get_layer(seat->server, WmLayer::overlay), nullptr, seat->pointer.tree.get());

    wm_pointer_set_xcursor(seat, "default");

    update_visual(seat);
}

static
void post_pointer_event(WmSeat* seat, WmSurface* focus, WmEvent* event)
{
    auto* wm = seat->server;

    if (wm_filter_event(wm, event) == WmEventFilterResult::capture) return;

    if (!focus || seat->pointer.focus != focus) return;

    wm_client_post_event_unfiltered(focus->client, event);
}

static
void pointer_button(WmSeat* seat, u32 button, bool pressed)
{
    wm_keyboard_focus(seat, seat->pointer.focus.get());

    post_pointer_event(seat, seat->pointer.focus.get(), ptr_to(WmEvent {
        .pointer = {
            .type = WmEventType::pointer_button,
            .seat = seat,
            .button = {
                .code = button,
                .pressed = pressed,
            }
        }
    }));
}

void wm_pointer_press(WmSeat* seat, u32 button)
{
    if (seat->pointer.pressed.inc(button)) {
        pointer_button(seat, button, true);
    }
}

void wm_pointer_release(WmSeat* seat, u32 button)
{
    if (seat->pointer.pressed.dec(button)) {
        pointer_button(seat, button, false);
    }
}

static
auto find_surface_under_pointer(WmSeat* seat) -> WmSurface*
{
    auto* wm = seat->server;

    auto region = scene_find_input_region_at(scene_get_root(wm_get_scene(wm)), seat->pointer.position);
    if (!region) return nullptr;

    for (auto* client : wm->clients) {
        for (auto* surface : client->surfaces) {
            if (surface->input_region.get() == region) return surface;
        }
    }

    return nullptr;
}

static
void update_focus(WmSeat* seat, WmSurface* new_focus)
{
    if (seat->pointer.focus == new_focus) return;

    // Don't change focus during implicit grab
    if (!seat->pointer.pressed.empty()) return;

    auto* old_focus = seat->pointer.focus.get();
    seat->pointer.focus = new_focus;

    auto old_client = old_focus ? old_focus->client : nullptr;
    auto new_client = new_focus ? new_focus->client : nullptr;

    if (old_client && old_client != new_client) {
        wm_client_post_event(old_client, ptr_to(WmEvent {
            .pointer = {
                .type = WmEventType::pointer_leave,
                .seat = seat,
            }
        }));
    }

    if (new_client) {
        wm_client_post_event(new_client, ptr_to(WmEvent {
            .pointer = {
                .type = WmEventType::pointer_enter,
                .seat = seat,
                .focus = new_focus,
            }
        }));
    }

    if (!new_focus) {
        wm_pointer_set_xcursor(seat, "default");
    }
}

void wm_pointer_move(WmSeat* seat, vec2f32 rel_accel, vec2f32 rel_unaccel)
{
    seat->pointer.position = wm_pointer_constraint_apply(seat->server, seat->pointer.position, rel_accel);
    seat->pointer.position = wm_find_output_at(seat->server, seat->pointer.position).position;

    update_visual(seat);
    update_focus(seat, find_surface_under_pointer(seat));

    post_pointer_event(seat, seat->pointer.focus.get(), ptr_to(WmEvent {
        .pointer = {
            .type = WmEventType::pointer_motion,
            .seat = seat,
            .motion = {
                .rel_accel = rel_accel,
                .rel_unaccel = rel_unaccel,
            }
        }
    }));
}

void wm_pointer_scroll(WmSeat* seat, vec2f32 delta)
{
    post_pointer_event(seat, seat->pointer.focus.get(), ptr_to(WmEvent {
        .pointer = {
            .type = WmEventType::pointer_scroll,
            .seat = seat,
            .scroll = {
                .delta = delta,
            }
        }
    }));
}

auto wm_pointer_get_pressed(WmSeat* seat) -> std::span<const u32>
{
    return seat->pointer.pressed;
}

auto wm_pointer_get_position(WmSeat* seat) -> vec2f32
{
    return seat->pointer.position;
}

auto wm_pointer_get_focus(WmSeat* seat) -> WmSurface*
{
    return seat->pointer.focus.get();
}
