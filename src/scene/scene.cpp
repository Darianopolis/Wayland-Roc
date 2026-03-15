#include "internal.hpp"

scene::Context::~Context()
{
    core_assert(outputs.empty());
    core_assert(clients.empty());
    core_assert(windows.empty());
}

void scene::push_io_event(scene::Context* ctx, io::Event* event)
{
    switch (event->type) {
        break;case io::EventType::shutdown_requested:
            ;

        break;case io::EventType::input_added:
            scene::handle_input_added(ctx, event->input.device);
        break;case io::EventType::input_removed:
            scene::handle_input_removed(ctx, event->input.device);
        break;case io::EventType::input_event:
            scene::handle_input(ctx, event->input);

        break;case io::EventType::output_configure:
              case io::EventType::output_frame:
              case io::EventType::output_added:
              case io::EventType::output_removed:
            ;
    }
}

auto scene::create(gpu::Context* gpu, io::Context* io) -> core::Ref<scene::Context>
{
    auto scene = core::create<scene::Context>();

    scene->gpu = gpu;

    scene->window_system = scene::register_system(scene.get());

    scene->root_tree = scene::tree::create(scene.get());

    for (auto layer : magic_enum::enum_values<scene::Layer>()) {
        auto* tree = (scene->layers[layer] = scene::tree::create(scene.get())).get();
        scene::tree::place_above(scene->root_tree.get(), nullptr, tree);
    }

    scene_render_init(scene.get());

    scene_cursor_manager_init(scene.get());

    scene->seat.keyboard = scene::keyboard::create(scene.get());
    scene->seat.pointer = scene::pointer::create(scene.get());

    return scene;
}

auto scene::get_layer(scene::Context* ctx, scene::Layer layer) -> scene::Tree*
{
    return ctx->layers[layer].get();
}

void scene::request_frame(scene::Context* ctx)
{
    for (auto* output : ctx->outputs) {
        scene::output::request_frame(output);
    }
}

void scene_broadcast_event(scene::Context* ctx, scene::Event* event)
{
    for (auto* client : ctx->clients) {
        scene_client_post_event(client, event);
    }
}

// -----------------------------------------------------------------------------

auto scene::register_system(scene::Context* ctx) -> scene::SystemId
{
    ctx->prev_system_id = scene::SystemId(std::to_underlying(ctx->prev_system_id) + 1);
    return ctx->prev_system_id;
}

// -----------------------------------------------------------------------------

auto scene::get_pointer(scene::Context* ctx) -> scene::Pointer*
{
    return ctx->seat.pointer.get();
}

auto scene::get_keyboard(scene::Context* ctx) -> scene::Keyboard*
{
    return ctx->seat.keyboard.get();
}
