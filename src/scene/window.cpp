#include "internal.hpp"

#include "core/math.hpp"

scene::Window::~Window()
{
    tree->userdata = nullptr;
    scene::window::unmap(this);
    std::erase(client->ctx->windows, this);
}

auto scene::window::create(scene::Client* client) -> core::Ref<scene::Window>
{
    auto window = core::create<scene::Window>();
    window->client = client;

    auto* ctx = client->ctx;

    ctx->windows.emplace_back(window.get());

    window->tree = scene::tree::create(ctx);

    window->tree->system = client->ctx->window_system;
    window->tree->userdata = window.get();

    return window;
}

auto scene::window::get_tree(scene::Window* window) -> scene::Tree*
{
    return window->tree.get();
}

void scene::window::set_title(scene::Window* window, std::string_view title)
{
    window->title = title;
}

void scene::window::request_reposition(scene::Window* window, rect2f32 frame, vec2f32 gravity)
{
    scene_client_post_event(window->client, core::ptr_to(scene::Event {
        .type = scene::EventType::window_reposition,
        .window = {
            .window = window,
            .reposition = {
                .frame = frame,
                .gravity = gravity,
            },
        }
    }));
}

void scene::window::set_frame(scene::Window* window, rect2f32 frame)
{
    window->extent = frame.extent;
    scene::tree::set_translation(window->tree.get(), frame.origin);
}

auto scene::window::get_frame(scene::Window* window) -> rect2f32
{
    return {
        scene::tree::get_position(window->tree.get()),
        window->extent,
        core::xywh
    };
}

void scene::window::map(scene::Window* window)
{
    if (window->mapped) return;

    scene::tree::place_above(scene::get_layer(window->client->ctx, scene::Layer::window), nullptr, window->tree.get());

    window->mapped = true;
}

void scene::window::raise(scene::Window* window)
{
    if (!window->mapped) return;

    scene::tree::place_above(scene::get_layer(window->client->ctx, scene::Layer::window), nullptr, window->tree.get());
}

void scene::window::unmap(scene::Window* window)
{
    if (!window->mapped) return;

    scene::node::unparent(window->tree.get());

    window->mapped = false;
}

// -----------------------------------------------------------------------------

auto scene::find_window_at(scene::Context* ctx, vec2f32 point) -> scene::Window*
{
    // TODO: This will ignore any `input_plane`s currently.
    //       Should we provide (optional) mappings from `input_plane` back to windows
    //       and then intersect against both `input_plane`s and `window` frames?

    scene::Window* window = nullptr;

    scene::iterate(ctx->root_tree.get(),
        scene::IterateDirection::front_to_back,
        scene::iterate_default,
        scene::iterate_default,
        [&](scene::Tree* tree) {
            if (tree->system == ctx->window_system) {
                auto w = static_cast<scene::Window*>(tree->userdata);
                if (core::rect::contains(scene::window::get_frame(w), point)) {
                    window = w;
                    return scene::IterateAction::stop;
                }
            }
            return scene::IterateAction::next;
        });

    return window;
}
