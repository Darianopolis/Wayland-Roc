
#include "internal.hpp"

static
void begin_interaction(WmServer* wm, WmSeat* seat, WmInteractionMode initial_mode)
{
    wm->movesize.seat = seat;

    auto pos = wm_pointer_get_position(seat);
    auto* window = wm_find_window_at(wm, pos);
    if (!window) return;
    auto frame = wm_window_get_frame(window);

    wm->mode = initial_mode;
    wm->movesize.window = window;
    wm->movesize.frame = frame;
    wm->movesize.grab = pos;

    auto dirs = (vec2i32(pos - frame.origin) * 3 / vec2i32(frame.extent)) - 1;

    wm->movesize.relative = {
        dirs.x || !dirs.y,
        dirs.y || !dirs.x,
    };

    if (initial_mode == WmInteractionMode::move && dirs.y < 0) {
        wm->movesize.relative.x = 1;
    } else if (initial_mode == WmInteractionMode::size) {
        if (!dirs.x && !dirs.y) {
            wm->mode = WmInteractionMode::move;
        } else {
            wm->movesize.relative = dirs;
        }
    }
}

static
void end_interaction(WmServer* wm)
{
    wm->movesize.seat = nullptr;
    wm->mode = WmInteractionMode::none;
}

// -----------------------------------------------------------------------------

static
void handle_motion(WmServer* wm)
{
    if (!wm->movesize.window) {
        return;
    }

    auto pos = wm_pointer_get_position(wm->movesize.seat);
    auto delta = (pos - wm->movesize.grab) * wm->movesize.relative;
    auto frame = wm->movesize.frame;

    if (wm->mode == WmInteractionMode::move) {
        frame.origin += delta;

    } else if (wm->mode == WmInteractionMode::size) {
        delta = glm::max(delta, 100.f - frame.extent);
        frame.origin += glm::min(wm->movesize.relative, {0,0}) * delta;
        frame.extent += delta;
    }

    wm_window_request_reposition(wm->movesize.window.get(), frame, wm->movesize.relative);
}

static
auto filter_event_movesize(WmServer* wm, WmEvent* event) -> WmEventFilterResult
{
    switch (event->type) {
        break;case WmEventType::pointer_motion:
            if (event->pointer.seat == wm->movesize.seat) handle_motion(wm);
        break;case WmEventType::pointer_button:
            if (event->pointer.seat == wm->movesize.seat) {
                if (event->pointer.button.pressed) return WmEventFilterResult::capture;
                if (wm_pointer_get_pressed(wm->movesize.seat).empty()) {
                    end_interaction(wm);
                }
            }
        break;case WmEventType::pointer_scroll:
            if (event->pointer.seat == wm->movesize.seat) return WmEventFilterResult::capture;
        break;default:
            ;
    }

    return {};
}

static
auto filter_event_default(WmServer* wm, WmEvent* event) -> WmEventFilterResult
{
    if (event->type != WmEventType::pointer_button) return {};

    auto button = event->pointer.button;
    if (!button.pressed) return {};

    auto mods = wm_keyboard_get_modifiers(event->pointer.seat);
    if (!mods.contains(wm->main_mod)) return {};

    if (button.code == BTN_LEFT && mods.contains(WmModifier::shift)) {
        begin_interaction(wm, event->pointer.seat, WmInteractionMode::move);
        return WmEventFilterResult::capture;

    } else if (button.code == BTN_RIGHT) {
        begin_interaction(wm, event->pointer.seat, WmInteractionMode::size);
        return WmEventFilterResult::capture;
    }

    return {};
}
static

auto filter_event(WmServer* wm, WmEvent* event) -> WmEventFilterResult
{
    switch (wm->mode) {
        break;case WmInteractionMode::none:
            return filter_event_default(wm, event);
        break;case WmInteractionMode::move:
              case WmInteractionMode::size:
            return filter_event_movesize(wm, event);
        break;default:
            ;
    }

    return WmEventFilterResult::passthrough;
}

void wm_init_movesize(WmServer* wm)
{
    wm_add_event_filter(wm, [wm](WmEvent* event) {
        return filter_event(wm, event);
    });
}
