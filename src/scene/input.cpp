#include "internal.hpp"

#include "core/math.hpp"

auto scene::input_device::get_type(scene::InputDevice* device) -> scene::InputDeviceType
{
    return device->type;
}

auto scene::input_device::get_pointer(scene::InputDevice* device) -> scene::Pointer*
{
    return device->type == scene::InputDeviceType::pointer
        ? static_cast<scene::Pointer*>(device)
        : nullptr;
}

auto scene::input_device::get_keyboard(scene::InputDevice* device) -> scene::Keyboard*
{
    return device->type == scene::InputDeviceType::keyboard
        ? static_cast<scene::Keyboard*>(device)
        : nullptr;
}

// -----------------------------------------------------------------------------

scene::Keyboard::~Keyboard()
{
    xkb_keymap_unref(keymap);
    xkb_state_unref(state);
    xkb_context_unref(context);
}

auto scene::keyboard::create(scene::Context* ctx) -> core::Ref<scene::Keyboard>
{
    auto keyboard = core::create<scene::Keyboard>();
    keyboard->type = scene::InputDeviceType::keyboard;
    keyboard->ctx = ctx;

    keyboard->rate = 25;
    keyboard->delay = 600;

    // Init XKB

    keyboard->context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    keyboard->keymap = xkb_keymap_new_from_names(keyboard->context, core::ptr_to(xkb_rule_names {
        .layout = "gb",
    }), XKB_KEYMAP_COMPILE_NO_FLAGS);

    keyboard->state = xkb_state_new(keyboard->keymap);

    // Get XKB modifier masks

    keyboard->mod_masks[scene::Modifier::shift] = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_MOD_NAME_SHIFT);
    keyboard->mod_masks[scene::Modifier::ctrl]  = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_MOD_NAME_CTRL);
    keyboard->mod_masks[scene::Modifier::caps]  = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_MOD_NAME_CAPS);
    keyboard->mod_masks[scene::Modifier::super] = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_VMOD_NAME_SUPER);
    keyboard->mod_masks[scene::Modifier::alt]   = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_VMOD_NAME_ALT)
                                                | xkb_keymap_mod_get_mask(keyboard->keymap, XKB_VMOD_NAME_LEVEL3);
    keyboard->mod_masks[scene::Modifier::num]   = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_VMOD_NAME_NUM);

    return keyboard;
}

static
auto get_modifiers(scene::Keyboard* keyboard, core::Flags<xkb_state_component> component) -> core::Flags<scene::Modifier>
{
    core::Flags<scene::Modifier> down = {};
    auto xkb_mods = xkb_state_serialize_mods(keyboard->state, component.get());
    for (auto mod : keyboard->mod_masks.enum_values) {
        if (xkb_mods & keyboard->mod_masks[mod]) down |= mod;
    }
    return down;
}

auto scene::keyboard::get_modifiers(scene::Keyboard* keyboard, core::Flags<scene::ModifierFlags> flags) -> core::Flags<scene::Modifier>
{
    auto mods = keyboard->depressed | keyboard->latched;
    if (!flags.contains(scene::ModifierFlags::ignore_locked)) mods |= keyboard->locked;
    return mods;
}

auto scene::get_modifiers(scene::Context* ctx, core::Flags<scene::ModifierFlags> flags) -> core::Flags<scene::Modifier>
{
    return scene::keyboard::get_modifiers(scene::get_keyboard(ctx), flags);
}

static
bool try_send_hotkey(scene::InputDevice* device, scene::Scancode code, bool pressed)
{
    auto* ctx = device->ctx;
    auto& hotkeys = device->hotkeys;

    if (pressed) {
        // Ignore LOCKED modifier state like CAPS and NUMLOCK, as hotkyes require an exact match.
        scene::Hotkey hotkey { scene::get_modifiers(ctx, scene::ModifierFlags::ignore_locked), code };

        auto iter = hotkeys.registered.find(hotkey);
        if (iter != hotkeys.registered.end()) {
            hotkeys.pressed.insert({code, {hotkey.mod, iter->second}});
            scene_client_post_event(iter->second, core::ptr_to(scene::Event {
                .type = scene::EventType::hotkey,
                .hotkey = {
                    .input_device = device,
                    .hotkey  = hotkey,
                    .pressed = true,
                }
            }));
            return true;
        }
    } else {
        auto iter = hotkeys.pressed.find(code);
        if (iter != hotkeys.pressed.end()) {
            auto[mods, client] = iter->second;
            hotkeys.pressed.erase(code);
            scene_client_post_event(client, core::ptr_to(scene::Event {
                .type = scene::EventType::hotkey,
                .hotkey = {
                    .input_device = device,
                    .hotkey  = {mods, code},
                    .pressed = false,
                }
            }));
            return true;
        }
    }

    return false;
}

static
xkb_keycode_t evdev_to_xkb(scene::Scancode code)
{
    return code + 8;
}

static
core::Flags<xkb_state_component> handle_key(scene::Keyboard* keyboard, scene::Scancode code, bool pressed, bool quiet)
{
    if (pressed ? keyboard->pressed.inc(code) : keyboard->pressed.dec(code)) {
        auto changed_components = xkb_state_update_key(keyboard->state, evdev_to_xkb(code), pressed ? XKB_KEY_DOWN : XKB_KEY_UP);

        if (!try_send_hotkey(keyboard, code, pressed)) {
            if (auto* client = keyboard->focus.client) {
                scene_client_post_event(client, core::ptr_to(scene::Event {
                    .type = scene::EventType::keyboard_key,
                    .keyboard = {
                        .keyboard = keyboard,
                        .key = {
                            .code = code,
                            .pressed = pressed,
                            .quiet = quiet,
                        },
                    }
                }));
            }
        }

        return changed_components;
    }

    return {};
}

static
auto get_keyboard_leds(scene::Keyboard* keyboard) -> core::Flags<libinput_led>
{
    core::Flags<libinput_led> leds = {};
    if (xkb_state_led_name_is_active(keyboard->state, XKB_LED_NAME_NUM)    > 0) leds |= LIBINPUT_LED_NUM_LOCK;
    if (xkb_state_led_name_is_active(keyboard->state, XKB_LED_NAME_CAPS)   > 0) leds |= LIBINPUT_LED_CAPS_LOCK;
    if (xkb_state_led_name_is_active(keyboard->state, XKB_LED_NAME_SCROLL) > 0) leds |= LIBINPUT_LED_SCROLL_LOCK;
    return leds;
}

static
void update_leds(scene::Context* ctx)
{
    if (ctx->seat.led_devices.empty()) return;

    // TODO: How to manage LED output across multiple keyboards
    auto leds = get_keyboard_leds(scene::get_keyboard(ctx));

    for (auto& device : ctx->seat.led_devices) {
        device->update_leds(leds);
    }
}

static
void handle_xkb_component_updates(scene::Keyboard* keyboard, core::Flags<xkb_state_component> changed)
{
    if (changed & XKB_STATE_MODS_DEPRESSED) keyboard->depressed = get_modifiers(keyboard, XKB_STATE_MODS_DEPRESSED);
    if (changed & XKB_STATE_MODS_LATCHED)   keyboard->depressed = get_modifiers(keyboard, XKB_STATE_MODS_LATCHED);
    if (changed & XKB_STATE_MODS_LOCKED)    keyboard->depressed = get_modifiers(keyboard, XKB_STATE_MODS_LOCKED);

    if (changed & XKB_STATE_MODS_EFFECTIVE) {
        if (keyboard->focus.client) {
            scene_client_post_event(keyboard->focus.client, core::ptr_to(scene::Event {
                .type = scene::EventType::keyboard_modifier,
                .keyboard = {
                    .keyboard = keyboard,
                },
            }));
        }
    }

    if (changed & XKB_STATE_LEDS) update_leds(keyboard->ctx);
}

void scene::keyboard::set_focus(scene::Keyboard* keyboard, scene::Focus new_focus)
{
    auto old_focus = keyboard->focus;

    keyboard->focus = new_focus;

    if (old_focus == new_focus) return;

    if (old_focus.client && (new_focus.client != old_focus.client)) {
        scene_client_post_event(old_focus.client, core::ptr_to(scene::Event {
            .type = scene::EventType::keyboard_leave,
            .keyboard = {
                .keyboard = keyboard,
            },
        }));
    }

    if (new_focus.client) {
        scene_client_post_event(new_focus.client, core::ptr_to(scene::Event {
            .type = scene::EventType::keyboard_enter,
            .keyboard = {
                .keyboard = keyboard,
                .focus = {
                    .region = new_focus.region,
                },
            },
        }));
    }
}

void scene::keyboard::clear_focus(scene::Keyboard* keyboard)
{
    scene::keyboard::set_focus(keyboard, {});
}

auto scene::keyboard::get_pressed(scene::Keyboard* keyboard) -> std::span<const scene::Scancode>
{
    return keyboard->pressed;
}

auto scene::keyboard::get_sym(scene::Keyboard* keyboard, scene::Scancode code) -> xkb_keysym_t
{
    return xkb_state_key_get_one_sym(keyboard->state, evdev_to_xkb(code));
}

auto scene::keyboard::get_utf8(scene::Keyboard* keyboard, scene::Scancode code) -> std::string
{
    std::string utf8;
    utf8.resize(xkb_state_key_get_utf8(keyboard->state, evdev_to_xkb(code), nullptr, 0));
    xkb_state_key_get_utf8(keyboard->state, evdev_to_xkb(code), utf8.data(), utf8.size() + 1);
    return utf8;
}

auto scene::keyboard::get_info(scene::Keyboard* keyboard) -> const scene::KeyboardInfo&
{
    return *keyboard;
}

// -----------------------------------------------------------------------------

auto scene::find_input_region_at(scene::Tree* tree, vec2f32 pos) -> scene::InputRegion*
{
    scene::InputRegion* region = nullptr;

    scene::iterate(tree,
        scene::IterateDirection::front_to_back,
        scene::iterate_default,
        [&](scene::Node* node) {
            if (node->type == scene::NodeType::input_region) {
                auto* input_region = static_cast<scene::InputRegion*>(node);
                if (input_region->region.contains(pos - scene::tree::get_position(input_region->parent))) {
                    region = input_region;
                    return scene::IterateAction::stop;
                }
            }
            return scene::IterateAction::next;
        },
        scene::iterate_default);

    return region;
}

void scene::pointer::set_focus(scene::Pointer* pointer, scene::Focus new_focus)
{
    scene::Focus old_focus = pointer->focus;

    if (old_focus == new_focus) return;

    pointer->focus = new_focus;

    if (old_focus.client && old_focus.client != new_focus.client) {
        scene_client_post_event(old_focus.client, core::ptr_to(scene::Event {
            .type = scene::EventType::pointer_leave,
            .pointer = {
                .pointer = pointer,
            },
        }));
    }

    if (new_focus.client) {
        scene_client_post_event(new_focus.client, core::ptr_to(scene::Event {
            .type = scene::EventType::pointer_enter,
            .pointer = {
                .pointer = pointer,
                .focus = {
                    .region = new_focus.region,
                }
            }
        }));
    }

    if (!new_focus.client) {
        scene::pointer::set_xcursor(pointer, "default");
    }
}

static
void update_pointer_focus(scene::Pointer* pointer)
{
    scene::Focus new_focus;

    if (!scene::pointer::get_pressed(pointer).empty()) {
        // Pointer retains old focus while any pointer buttons pressed
        new_focus = pointer->focus;

    } else if (auto* region = scene::find_input_region_at(pointer->ctx->root_tree.get(), scene::pointer::get_position(pointer))) {
        new_focus = {region->client, region};
    }

    scene::pointer::set_focus(pointer, new_focus);
}

void scene::update_pointer_focus(scene::Context* ctx)
{
    if (auto* pointer = scene::get_pointer(ctx)) {
        ::update_pointer_focus(pointer);
    }
}

auto scene::pointer::get_position(scene::Pointer* pointer) -> vec2f32
{
    return scene::tree::get_position(pointer->tree.get());
}

auto scene::pointer::get_pressed(scene::Pointer* pointer) -> std::span<const scene::Scancode>
{
    return pointer->pressed;
}

// -----------------------------------------------------------------------------

auto scene::pointer::create(scene::Context* ctx) -> core::Ref<scene::Pointer>
{
    auto pointer = core::create<scene::Pointer>();
    pointer->ctx = ctx;
    pointer->type = scene::InputDeviceType::pointer;

    pointer->tree = scene::tree::create(ctx);
    scene::tree::place_above(scene::get_layer(ctx, scene::Layer::overlay), nullptr, pointer->tree .get());

    return pointer;
}

static
void handle_button(scene::Pointer* pointer, scene::Scancode code, bool pressed, bool quiet)
{
    if (pressed ? pointer->pressed.inc(code) : pointer->pressed.dec(code)) {
        if (!try_send_hotkey(pointer, code, pressed)) {
            if (auto* focus = pointer->focus.client) {
                if (pressed) {
                    scene::keyboard::set_focus(pointer->ctx->seat.keyboard.get(), {focus, pointer->focus.region});
                }
                scene_client_post_event(focus, core::ptr_to(scene::Event {
                    .type = scene::EventType::pointer_button,
                    .pointer = {
                        .pointer = pointer,
                        .button = {
                            .code    = code,
                            .pressed = pressed,
                            .quiet   = quiet,
                        },
                    }
                }));
            } else if (pressed) {
                scene::keyboard::set_focus(pointer->ctx->seat.keyboard.get(), {});
            }
        }
        if (!pressed) {
            update_pointer_focus(pointer);
        }
    }
}

static
void handle_motion(scene::Pointer* pointer, vec2f32 delta)
{
    auto cur = scene::pointer::get_position(pointer);

    auto res = pointer->driver({
        .position = cur,
        .delta    = delta,
    });

    scene::tree::set_translation(pointer->tree.get(), res.position);

    update_pointer_focus(pointer);

    if (auto* focus = pointer->focus.client) {
        scene_client_post_event(focus, core::ptr_to(scene::Event {
            .type = scene::EventType::pointer_motion,
            .pointer = {
                .pointer = pointer,
                .motion = {
                    .rel_accel   = res.accel,
                    .rel_unaccel = res.unaccel,
                },
            },
        }));
    }
}

static
void handle_scroll(scene::Pointer* pointer, vec2f32 delta)
{
    if (auto* focus = pointer->focus.client) {
        scene_client_post_event(focus, core::ptr_to(scene::Event {
            .type = scene::EventType::pointer_scroll,
            .pointer = {
                .pointer = pointer,
                .scroll = {
                    .delta = delta,
                }
            },
        }));
    }
}

void scene::pointer::focus(scene::Pointer* pointer, scene::Client* client, scene::InputRegion* region)
{
    scene::pointer::set_focus(pointer, {client, region});
}

void scene::pointer::set_driver(scene::Pointer* pointer, std::move_only_function<scene::PointerDriverFn>&& driver)
{
    pointer->driver = std::move(driver);
    handle_motion(pointer, {});
}

// -----------------------------------------------------------------------------

void scene::handle_input_added(scene::Context* ctx, io::InputDevice* device)
{
    if (device->info().capabilities.contains(io::InputDeviceCapability::libinput_led)) {
        ctx->seat.led_devices.emplace_back(device);
    }
}

void scene::handle_input_removed(scene::Context* ctx, io::InputDevice* device)
{
    std::erase(ctx->seat.led_devices, device);
}

static
auto categorize_key(scene::Scancode code) -> scene::InputDeviceType
{
    switch (code) {
        break;case BTN_MOUSE ... BTN_TASK:
            return scene::InputDeviceType::pointer;
        break;case KEY_ESC        ... KEY_MICMUTE:
              case KEY_OK         ... KEY_LIGHTS_TOGGLE:
              case KEY_ALS_TOGGLE ... KEY_PERFORMANCE:
            return scene::InputDeviceType::keyboard;
        break;default:
            return scene::InputDeviceType::invalid;
    }
}

void scene::handle_input(scene::Context* ctx, const io::InputEvent& event)
{
    vec2f32 motion = {};
    vec2f32 scroll = {};
    core::Flags<xkb_state_component> xkb_updates = {};

    // TODO: Multiple input devices
    auto pointer = scene::get_pointer(ctx);
    auto keyboard  = scene::get_keyboard(ctx);

    for (auto& channel : event.channels) {
        switch (channel.type) {
            break;case EV_KEY:
                switch (categorize_key(channel.code)) {
                    break;case scene::InputDeviceType::pointer:
                        handle_button(pointer, channel.code, channel.value, event.quiet);
                    break;case scene::InputDeviceType::keyboard:
                        xkb_updates |= handle_key(keyboard, channel.code, channel.value, event.quiet);
                    break;case scene::InputDeviceType::invalid:
                        log_warn("Unknown  {} = {}", libevdev_event_code_get_name(channel.type, channel.code), channel.value);
                }
            break;case EV_REL:
                switch (channel.code) {
                    break;case REL_X: motion.x += channel.value;
                    break;case REL_Y: motion.y += channel.value;
                    break;case REL_HWHEEL: scroll.x += channel.value;
                    break;case REL_WHEEL:  scroll.y += channel.value;
                }
            break;case EV_ABS:
                log_warn("Unknown  {} = {}", libevdev_event_code_get_name(channel.type, channel.code), channel.value);
        }
    }

    if (motion.x || motion.y) handle_motion(pointer, motion);
    if (scroll.x || scroll.y) handle_scroll(pointer, scroll);

    if (xkb_updates) handle_xkb_component_updates(keyboard, xkb_updates);
}

// -----------------------------------------------------------------------------

static
auto get_map_for_code(scene::Context* ctx, scene::Scancode code) -> scene::HotkeyMap*
{
    switch (categorize_key(code)) {
        break;case scene::InputDeviceType::pointer:
            return &scene::get_pointer(ctx)->hotkeys;
        break;case scene::InputDeviceType::keyboard:
            return &scene::get_keyboard(ctx)->hotkeys;
        break;default:
            return nullptr;
    }
}

auto scene::client::hotkey_register(scene::Client* client, scene::Hotkey hotkey) -> bool
{
    auto* map = get_map_for_code(client->ctx, hotkey.code);
    if (!map) return false;
    auto& slot = map->registered[hotkey];
    return !slot && (slot = client);
}

void scene::client::hotkey_unregister(scene::Client* client, scene::Hotkey hotkey)
{
    auto* map = get_map_for_code(client->ctx, hotkey.code);
    if (!map) return;
    auto iter = map->registered.find(hotkey);
    if (iter == map->registered.end()) return;
    if (iter->second != client) return;
    map->registered.erase(hotkey);
    map->pressed.erase(hotkey.code);
}
