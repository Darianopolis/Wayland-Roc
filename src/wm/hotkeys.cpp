
#include "internal.hpp"

static
auto close_focused(WmServer* wm, WmSeat* seat, WmSurface* focus) -> WmEventFilterResult
{
    auto mods = wm_keyboard_get_modifiers(seat);
    if (!mods.contains(wm->main_mod)) return {};

    if (focus) {
        for (auto* window : wm->windows) {
            if (window->surface == focus) {
                wm_window_request_close(window);
                break;
            }
        }
    }

    return WmEventFilterResult::capture;
}

static
auto filter_event(WmServer* wm, WmEvent* event) -> WmEventFilterResult
{
    switch (event->type) {
        break;case WmEventType::keyboard_key:
            if (!event->keyboard.key.pressed) return {};
            if (event->keyboard.key.code == KEY_Q) {
                return close_focused(wm, event->keyboard.seat, wm_keyboard_get_focus(event->keyboard.seat));
            }
            if (event->keyboard.key.code == KEY_S) {
                auto mods = wm_keyboard_get_modifiers(event->keyboard.seat);
                if (mods.contains(wm->main_mod)) {
                    wm_keyboard_focus(event->keyboard.seat, nullptr);
                }
            }
        break;case WmEventType::pointer_button:
            if (!event->pointer.button.pressed) return {};
            if (event->pointer.button.code == BTN_MIDDLE) {
                return close_focused(wm, event->pointer.seat, wm_pointer_get_focus(event->pointer.seat));
            }
        break;default:
            ;
    }

    return {};
}

void wm_init_hotkeys(WmServer* wm)
{
    wm_add_event_filter(wm, [wm](WmEvent* event) {
        return filter_event(wm, event);
    });
}
