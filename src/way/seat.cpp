#include "internal.hpp"

static
auto get_keymap_file(xkb_keymap* keymap) -> way::Keymap
{
    auto string = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    defer { free(string); };
    u32 size = strlen(string) + 1;

    auto fd = core::fd::adopt(core::check<memfd_create>(PROGRAM_NAME "-keymap", MFD_ALLOW_SEALING | MFD_CLOEXEC).value);
    core::check<ftruncate>(fd.get(), size);

    auto mapped = core::check<mmap>(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0).value;
    memcpy(mapped, string, size);
    core::check<munmap>(mapped, size);

    // Seal file to prevent further writes
    core::check<fcntl>(fd.get(), F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_SHRINK | F_SEAL_GROW);

    return { .fd = std::move(fd), .size = size };
}

void way::seat::init(way::Server* server)
{
    auto& kb_info = scene::keyboard::get_info(scene::get_keyboard(server->scene));

    server->keyboard.keymap = get_keymap_file(kb_info.keymap);
}

// -----------------------------------------------------------------------------

static
void get_keyboard(wl_client* wl_client, wl_resource* resource, u32 id)
{
    auto* client = way::get_userdata<way::Client>(resource);
    auto* server = client->server;

    auto* kb = way_resource_create_unsafe(wl_keyboard, wl_client, resource, id, client);
    client->keyboards.emplace_back(kb);

    way_send(server, wl_keyboard_send_keymap, kb,
        WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
        server->keyboard.keymap.fd.get(), server->keyboard.keymap.size);

    auto& kb_info = scene::keyboard::get_info(scene::get_keyboard(server->scene));

    if (wl_resource_get_version(kb) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION) {
        way_send(server, wl_keyboard_send_repeat_info, kb, kb_info.rate, kb_info.delay);
    }
}

WAY_INTERFACE(wl_keyboard) = {
    .release = way::simple_destroy,
};

// -----------------------------------------------------------------------------

static
void get_pointer(wl_client* wl_client, wl_resource* resource, u32 id)
{
    auto* client = way::get_userdata<way::Client>(resource);

    client->pointers.emplace_back(way_resource_create_unsafe(wl_pointer, wl_client, resource, id, client));
}

static
void set_cursor(wl_client* wl_client, wl_resource* resource, u32 serial, wl_resource* wl_surface, int hot_x, int hot_y)
{
    auto* client = way::get_userdata<way::Client>(resource);
    auto* surface = wl_surface ? way::get_userdata<way::Surface>(wl_surface) : nullptr;

    if (surface) {
        scene::tree::set_translation(surface->scene.tree.get(), {-hot_x, -hot_y});

        if (surface->role != way::SurfaceRole::cursor) {
            surface->role = way::SurfaceRole::cursor;
            scene::node::unparent(surface->scene.input_region.get());
        }
    }

    if (client->server->pointer.scene) {
        scene::pointer::set_cursor(client->server->pointer.scene, surface ? surface->scene.tree.get() : nullptr);
    }
}

WAY_INTERFACE(wl_pointer) = {
    .set_cursor = set_cursor,
    .release = way::simple_destroy,
};

// -----------------------------------------------------------------------------

WAY_INTERFACE(wl_seat) = {
    .get_pointer = get_pointer,
    .get_keyboard = get_keyboard,
    WAY_STUB(get_touch),
    .release = way::simple_destroy,
};

WAY_BIND_GLOBAL(wl_seat, bind)
{
    auto* client = way::client::from(bind.server, bind.client);

    auto* resource = way_resource_create_unsafe(wl_seat, bind.client, bind.version, bind.id, client);

    way_send(bind.server, wl_seat_send_capabilities, resource, WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);

    if (bind.version >= WL_SEAT_NAME_SINCE_VERSION) {
        way_send(bind.server, wl_seat_send_name, resource, "seat0");
    }
}

// -----------------------------------------------------------------------------

static
auto find_surface(way::Client* client, scene::InputRegion* region) -> way::Surface*
{
    if (!region) return nullptr;
    if (region->client != client->scene.get()) return nullptr;
    for (auto* surface : client->surfaces) {
        if (surface->scene.input_region.get() == region) return surface;
    }
    return nullptr;
}

static
auto elapsed_ms(way::Server* server) -> u32
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(way::get_elapsed(server)).count();
}

// -----------------------------------------------------------------------------

static
void keyboard_leave(way::Client* client)
{
    auto* server = client->server;
    auto* surface = server->focus.keyboard.get();
    if (!surface) return;

    server->keyboard.scene = nullptr;
    u32 serial = way::next_serial(server);

    if (surface->wl_surface) {
        for (auto* resource : client->keyboards) {
            way_send(server, wl_keyboard_send_leave, resource, serial, surface->wl_surface);
        }
    }

    core_assert(server->focus.keyboard.get() == surface, "Keyboard left surface that did not have focus");
    server->focus.keyboard = nullptr;
}

void way::seat::on_keyboard_leave(way::Client* client, scene::Event* event)
{
    keyboard_leave(client);
}

static
auto find_root_toplevel(way::Surface* surface) -> way::Surface*
{
    if (surface->role == way::SurfaceRole::xdg_toplevel) return surface;
    core_assert(surface->parent);
    return find_root_toplevel(surface->parent.get());
}

void way::seat::on_keyboard_enter(way::Client* client, scene::Event* event)
{
    auto* server = client->server;

    auto* surface = find_surface(client, event->keyboard.focus.region);

    // xdg_popup and wl_subsurface cannot have keyboard focus
    surface = find_root_toplevel(surface);
    if (surface == server->focus.keyboard.get()) return;

    // Leave previous window
    keyboard_leave(client);

    if (surface->toplevel.window) {
        scene::window::raise(surface->toplevel.window.get());
    }

    server->keyboard.scene = event->keyboard.keyboard;

    u32 serial = way::next_serial(server);

    if (surface->wl_surface) {
        auto pressed = way::to_wl_array<const u32>(scene::keyboard::get_pressed(server->keyboard.scene));
        for (auto* resource : client->keyboards) {
            way_send(server, wl_keyboard_send_enter, resource, serial, surface->wl_surface, &pressed);
        }
    } else {
        log_error("Keyboard enter failed: wl_surface is destroyed for {}", (void*)surface);
    }

    server->focus.keyboard = surface;

    way::data_offer::selection(client);
}

// -----------------------------------------------------------------------------

static
auto get_fixed_pos(way::Surface* surface, scene::Pointer* pointer) -> std::pair<wl_fixed_t, wl_fixed_t>
{
    auto local = scene::pointer::get_position(pointer) - scene::tree::get_position(surface->scene.tree.get());

    return {wl_fixed_from_double(local.x), wl_fixed_from_double(local.y)};
}

static
void pointer_leave_surface(way::Client* client, way::Surface* surface, u32 serial)
{
    auto* server = client->server;

    if (!surface->wl_surface) return;
    for (auto* resource : client->pointers) {
        way_send(server, wl_pointer_send_leave, resource, serial, surface->wl_surface);
    }
}

void way::seat::on_pointer_leave(way::Client* client, scene::Event* event)
{
    auto* server = client->server;

    if (auto* surface = server->focus.pointer.get()) {
        pointer_leave_surface(client, surface, way::next_serial(server));
    }

    server->focus.pointer = nullptr;
    server->pointer.scene = nullptr;
}

void way::seat::on_pointer_enter(way::Client* client, scene::Event* event)
{
    auto* server = client->server;

    server->pointer.scene = event->pointer.pointer;

    u32 serial = way::next_serial(server);

    auto* old_surface = server->focus.pointer.get();
    auto* new_surface = find_surface(client, event->pointer.focus.region);

    if (old_surface && new_surface && old_surface != new_surface) {
        pointer_leave_surface(client, old_surface, serial);
    }

    server->focus.pointer = new_surface;

    if (!new_surface->wl_surface) return;
    auto[x, y] = get_fixed_pos(new_surface, server->pointer.scene);
    for (auto* resource : client->pointers) {
        way_send(server, wl_pointer_send_enter, resource, serial, new_surface->wl_surface, x, y);
    }
}

// -----------------------------------------------------------------------------

void way::seat::on_key(way::Client* client, scene::Event* event)
{
    auto* server = client->server;
    u32 serial = way::next_serial(server);
    u32 time = elapsed_ms(server);
    auto key = event->keyboard.key;
    auto state = key.pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED;

    for (auto* resource : client->keyboards) {
        way_send(server, wl_keyboard_send_key, resource, serial, time, key.code, state);
    }
}

void way::seat::on_modifier(way::Client* client, scene::Event* event)
{
    auto* server = client->server;
    u32 serial = way::next_serial(server);
    auto kb = scene::keyboard::get_info(server->keyboard.scene);

    for (auto* resource : client->keyboards) {
        way_send(server, wl_keyboard_send_modifiers, resource, serial,
            xkb_state_serialize_mods(  kb.state, XKB_STATE_MODS_DEPRESSED),
            xkb_state_serialize_mods(  kb.state, XKB_STATE_MODS_LATCHED),
            xkb_state_serialize_mods(  kb.state, XKB_STATE_MODS_LOCKED),
            xkb_state_serialize_layout(kb.state, XKB_STATE_LAYOUT_EFFECTIVE));
    }
}

// -----------------------------------------------------------------------------

static
void pointer_frame(way::Server* server, wl_resource* resource)
{
    if (wl_resource_get_version(resource) >= WL_POINTER_FRAME_SINCE_VERSION) {
        way_send(server, wl_pointer_send_frame, resource);
    }
}

void way::seat::on_motion(way::Client* client, scene::Event* event)
{
    auto* server = client->server;
    auto* surface = server->focus.pointer.get();
    if (!surface) return;

    u32 time = elapsed_ms(server);
    auto[x, y] = get_fixed_pos(surface, server->pointer.scene);

    for (auto* resource : client->pointers) {
        way_send(server, wl_pointer_send_motion, resource, time, x, y);
        pointer_frame(server, resource);
    }
}

void way::seat::on_button(way::Client* client, scene::Event* event)
{
    auto* server = client->server;
    u32 serial = way::next_serial(server);
    u32 time = elapsed_ms(server);
    auto button = event->pointer.button;
    auto state = button.pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED;

    for (auto* resource : client->pointers) {
        way_send(server, wl_pointer_send_button, resource, serial, time, button.code, state);
        pointer_frame(server, resource);
    }
}

void way::seat::on_scroll(way::Client* client, scene::Event* event)
{
    auto* server = client->server;
    u32 time = elapsed_ms(server);
    auto delta = event->pointer.scroll.delta;

    static constexpr f32 axis_pixel_rate = 15.f;

    for (auto* resource : client->pointers) {
        if (delta.x) {
            way_send(server, wl_pointer_send_axis, resource, time,
                WL_POINTER_AXIS_HORIZONTAL_SCROLL, wl_fixed_from_double(delta.x * axis_pixel_rate));
        }
        if (delta.y) {
            way_send(server, wl_pointer_send_axis, resource, time,
                WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_double(delta.y * axis_pixel_rate));
        }
        pointer_frame(server, resource);
    }
}
