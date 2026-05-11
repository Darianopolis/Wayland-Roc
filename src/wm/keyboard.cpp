#include "internal.hpp"

void wm_init_keyboard(WmSeat* seat)
{
    auto& keyboard = seat->keyboard;

    keyboard.rate = 25;
    keyboard.delay = 600;

    // Init XKB

    keyboard.context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    keyboard.keymap = xkb_keymap_new_from_names(keyboard.context, ptr_to(xkb_rule_names {
        .layout = "gb",
    }), XKB_KEYMAP_COMPILE_NO_FLAGS);

    keyboard.state = xkb_state_new(keyboard.keymap);

    // Get XKB modifier masks

    keyboard.mod_masks[WmModifier::shift] = xkb_keymap_mod_get_mask(keyboard.keymap, XKB_MOD_NAME_SHIFT);
    keyboard.mod_masks[WmModifier::ctrl]  = xkb_keymap_mod_get_mask(keyboard.keymap, XKB_MOD_NAME_CTRL);
    keyboard.mod_masks[WmModifier::caps]  = xkb_keymap_mod_get_mask(keyboard.keymap, XKB_MOD_NAME_CAPS);
    keyboard.mod_masks[WmModifier::super] = xkb_keymap_mod_get_mask(keyboard.keymap, XKB_VMOD_NAME_SUPER);
    keyboard.mod_masks[WmModifier::alt]   = xkb_keymap_mod_get_mask(keyboard.keymap, XKB_VMOD_NAME_ALT)
                                          | xkb_keymap_mod_get_mask(keyboard.keymap, XKB_VMOD_NAME_LEVEL3);
    keyboard.mod_masks[WmModifier::num]   = xkb_keymap_mod_get_mask(keyboard.keymap, XKB_VMOD_NAME_NUM);
}

WmKeyboard::~WmKeyboard()
{
    xkb_state_unref(state);
    xkb_context_unref(context);
    xkb_keymap_unref(keymap);
}

static
auto evdev_to_xkb(u32 code) -> xkb_keycode_t
{
    return code + 8;
}

static
void post_keyboard_event(WmSeat* seat, WmSurface* focus, WmEvent* event)
{
    auto* wm = seat->server;

    if (wm_filter_event(wm, event) == WmEventFilterResult::capture) return;

    if (!focus || seat->keyboard.focus != focus) return;

    wm_client_post_event_unfiltered(focus->client, event);
}

static
void keyboard_key(WmSeat* seat, u32 key, bool pressed)
{
    auto code = evdev_to_xkb(key);

    auto* focus = seat->keyboard.focus.get();

    {
        auto sym = xkb_state_key_get_one_sym(seat->keyboard.state, code);

        std::string utf8;
        utf8.resize(xkb_state_key_get_utf8(seat->keyboard.state, code, nullptr, 0));
        xkb_state_key_get_utf8(seat->keyboard.state, code, utf8.data(), utf8.size() + 1);

        post_keyboard_event(seat, focus, ptr_to(WmEvent {
            .keyboard = {
                .type = WmEventType::keyboard_key,
                .seat = seat,
                .key = {
                    .code = key,
                    .sym = sym,
                    .utf8 = utf8.c_str(),
                    .pressed = pressed,
                },
            }
        }));
    }

    Flags<xkb_state_component> changed = xkb_state_update_key(seat->keyboard.state, code, pressed ? XKB_KEY_DOWN : XKB_KEY_UP);

    if (changed.contains(XKB_STATE_MODS_EFFECTIVE)) {
        post_keyboard_event(seat, focus, ptr_to(WmEvent {
            .keyboard = {
                .type = WmEventType::keyboard_modifier,
                .seat = seat,
            }
        }));
    }
}

void wm_keyboard_press(WmSeat* seat, u32 key)
{
    if (seat->keyboard.pressed.inc(key)) {
        keyboard_key(seat, key, true);
    }
}

void wm_keyboard_release(WmSeat* seat, u32 key)
{
    if (seat->keyboard.pressed.dec(key)) {
        keyboard_key(seat, key, false);
    }
}

auto wm_keyboard_get_pressed(WmSeat* seat) -> std::span<const u32>
{
    return seat->keyboard.pressed;
}

auto wm_keyboard_get_info(WmSeat* seat) -> const WmKeyboardInfo&
{
    return seat->keyboard;
}

static
auto get_modifiers(WmKeyboard& keyboard, xkb_state_component component) -> Flags<WmModifier>
{
    Flags<WmModifier> down = {};
    auto xkb_mods = xkb_state_serialize_mods(keyboard.state, component);
    for (auto mod : enum_values<WmModifier>()) {
        if (xkb_mods & keyboard.mod_masks[mod]) down |= mod;
    }
    return down;
}

auto wm_keyboard_get_modifiers(WmSeat* seat) -> Flags<WmModifier>
{
    return get_modifiers(seat->keyboard, XKB_STATE_MODS_EFFECTIVE);
}

auto wm_keyboard_get_focus(WmSeat* seat) -> WmSurface*
{
    return seat->keyboard.focus.get();
}

void wm_keyboard_focus(WmSeat* seat, WmSurface* new_focus)
{
    if (seat->keyboard.focus == new_focus) return;

    auto* old_focus = seat->keyboard.focus.get();
    seat->keyboard.focus = new_focus;

    auto old_client = old_focus ? old_focus->client : nullptr;
    auto new_client = new_focus ? new_focus->client : nullptr;

    if (old_client && (old_client != new_client)) {
        wm_client_post_event(old_client, ptr_to(WmEvent {
            .keyboard = {
                .type = WmEventType::keyboard_leave,
                .seat = seat,
            }
        }));
    }

    if (new_client) {
        wm_client_post_event(new_client, ptr_to(WmEvent {
            .keyboard = {
                .type = WmEventType::keyboard_enter,
                .seat = seat,
                .focus = new_focus,
            }
        }));
    }
}
