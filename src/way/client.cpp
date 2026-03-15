#include "internal.hpp"

static
auto find_surface(way::Client* client, scene::Window* window) -> way::Surface*
{
    for (auto* surface : client->surfaces) {
        if (surface->toplevel.window.get() == window) {
            return surface;
        }
    }

    return nullptr;
}

static
void handle_event(way::Client* client, scene::Event* event)
{
    switch (event->type) {
        break;case scene::EventType::window_reposition: {
            auto* surface = find_surface(client, event->window.window);
            way::toplevel::on_reposition(surface, event->window.reposition.frame, event->window.reposition.gravity);
        }
        break;case scene::EventType::output_frame: {
            for (auto* surface : client->surfaces) {
                way::surface::on_redraw(surface);
            }
        }

        break;case scene::EventType::keyboard_enter:    way::seat::on_keyboard_enter(client, event);
        break;case scene::EventType::keyboard_leave:    way::seat::on_keyboard_leave(client, event);
        break;case scene::EventType::keyboard_key:      way::seat::on_key(           client, event);
        break;case scene::EventType::keyboard_modifier: way::seat::on_modifier(      client, event);

        break;case scene::EventType::pointer_enter:     way::seat::on_pointer_enter( client, event);
        break;case scene::EventType::pointer_leave:     way::seat::on_pointer_leave( client, event);
        break;case scene::EventType::pointer_motion:    way::seat::on_motion(        client, event);
        break;case scene::EventType::pointer_button:    way::seat::on_button(        client, event);
        break;case scene::EventType::pointer_scroll:    way::seat::on_scroll(        client, event);

        break;case scene::EventType::output_added:
              case scene::EventType::output_removed:
              case scene::EventType::output_configured:
              case scene::EventType::output_layout:
              case scene::EventType::output_frame_request:
            ;

        break;case scene::EventType::hotkey:
            ;

        break;case scene::EventType::selection:
            way::data_offer::selection(client);
    }
}

// -----------------------------------------------------------------------------

void way::on_client_create(wl_listener* listener, void* data)
{
    auto* server = way::get_userdata<way::Server>(listener);
    auto* wl_client = static_cast<struct wl_client*>(data);

    auto client = core::create<way::Client>();
    client->server = server;
    client->wl_client = wl_client;

    wl_client_set_user_data(wl_client, core::add_ref(client.get()), [](void* data) {
        core::remove_ref(way::get_userdata<way::Client>(data));
    });

    client->scene = scene::client::create(server->scene);
    scene::client::set_event_handler(client->scene.get(), [client = client.get()](scene::Event* event) {
        handle_event(client, event);
    });
}

way::Client* way::client::from(way::Server* server, const wl_client* client)
{
    // NOTE: `wl_client_get_user_data` does not actually require a non-const client.
    return way::get_userdata<way::Client>(wl_client_get_user_data(const_cast<wl_client*>(client)));
}

auto way::client::is_behind(way::Client* client) -> bool
{
    return poll(core::ptr_to(pollfd {
        .fd = wl_client_get_fd(client->wl_client),
        .events = POLLOUT,
    }), 1, 0) != 1;
}
