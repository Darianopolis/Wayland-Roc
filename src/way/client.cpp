#include "internal.hpp"

static
auto find_surface(way_client* client, scene::Window* window) -> way_surface*
{
    for (auto* surface : client->surfaces) {
        if (surface->toplevel.window.get() == window) {
            return surface;
        }
    }

    return nullptr;
}

static
void handle_event(way_client* client, scene::Event* event)
{
    switch (event->type) {
        break;case scene::EventType::window_reposition: {
            auto* surface = find_surface(client, event->window.window);
            way_toplevel_on_reposition(surface, event->window.reposition.frame, event->window.reposition.gravity);
        }
        break;case scene::EventType::output_frame: {
            for (auto* surface : client->surfaces) {
                way_surface_on_redraw(surface);
            }
        }

        break;case scene::EventType::keyboard_enter:    way_seat_on_keyboard_enter(client, event);
        break;case scene::EventType::keyboard_leave:    way_seat_on_keyboard_leave(client, event);
        break;case scene::EventType::keyboard_key:      way_seat_on_key(           client, event);
        break;case scene::EventType::keyboard_modifier: way_seat_on_modifier(      client, event);

        break;case scene::EventType::pointer_enter:     way_seat_on_pointer_enter( client, event);
        break;case scene::EventType::pointer_leave:     way_seat_on_pointer_leave( client, event);
        break;case scene::EventType::pointer_motion:    way_seat_on_motion(        client, event);
        break;case scene::EventType::pointer_button:    way_seat_on_button(        client, event);
        break;case scene::EventType::pointer_scroll:    way_seat_on_scroll(        client, event);

        break;case scene::EventType::output_added:
              case scene::EventType::output_removed:
              case scene::EventType::output_configured:
              case scene::EventType::output_layout:
              case scene::EventType::output_frame_request:
            ;

        break;case scene::EventType::hotkey:
            ;

        break;case scene::EventType::selection:
            way_data_offer_selection(client);
    }
}

// -----------------------------------------------------------------------------

void way_on_client_create(wl_listener* listener, void* data)
{
    auto* server = way_get_userdata<way_server>(listener);
    auto* wl_client = static_cast<struct wl_client*>(data);

    auto client = core::create<way_client>();
    client->server = server;
    client->wl_client = wl_client;

    wl_client_set_user_data(wl_client, core::add_ref(client.get()), [](void* data) {
        core::remove_ref(way_get_userdata<way_client>(data));
    });

    client->scene = scene::client::create(server->scene);
    scene::client::set_event_handler(client->scene.get(), [client = client.get()](scene::Event* event) {
        handle_event(client, event);
    });
}

way_client* way_client_from(way_server* server, const wl_client* client)
{
    // NOTE: `wl_client_get_user_data` does not actually require a non-const client.
    return way_get_userdata<way_client>(wl_client_get_user_data(const_cast<wl_client*>(client)));
}

auto way_client_is_behind(way_client* client) -> bool
{
    return poll(core::ptr_to(pollfd {
        .fd = wl_client_get_fd(client->wl_client),
        .events = POLLOUT,
    }), 1, 0) != 1;
}
