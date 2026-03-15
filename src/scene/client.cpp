#include "internal.hpp"

scene::Client::~Client()
{
    core_assert(input_regions == 0);

    // All client windows must be destroyed before the client
    for (auto* window : ctx->windows) {
        core_assert(window->client != this);
    }

    if (auto* keyboard = scene::get_keyboard(ctx); keyboard->focus.client == this) {
        scene::keyboard::set_focus(keyboard, {});
    }

    if (auto* pointer = scene::get_pointer(ctx); pointer->focus.client == this) {
        scene::pointer::set_focus(pointer, {});
    }

    std::erase(ctx->clients, this);
}

auto scene::client::create(scene::Context* ctx) -> core::Ref<scene::Client>
{
    auto client = core::create<scene::Client>();
    client->ctx = ctx;
    ctx->clients.emplace_back(client.get());
    return client;
}

void scene::client::set_event_handler(scene::Client* client, std::move_only_function<scene::EventHandlerFn>&& event_handler)
{
    client->event_handler = std::move(event_handler);
}

void scene_client_post_event(scene::Client* client, scene::Event* event)
{
    client->event_handler(event);
}
