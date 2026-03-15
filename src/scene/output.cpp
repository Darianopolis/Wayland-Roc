#include "internal.hpp"

#include "core/math.hpp"

scene::Output::~Output()
{
    auto* ctx = client->ctx;
    scene_broadcast_event(ctx, core::ptr_to(scene::Event { .type = scene::EventType::output_removed, .output = this }));
    std::erase(ctx->outputs, this);
    scene_broadcast_event(ctx, core::ptr_to(scene::Event { .type = scene::EventType::output_layout }));
}

auto scene::output::create(scene::Client* client) -> core::Ref<scene::Output>
{
    auto* ctx = client->ctx;
    auto output = core::create<scene::Output>();
    output->client = client;
    ctx->outputs.push_back(output.get());
    scene_broadcast_event(ctx, core::ptr_to(scene::Event { .type = scene::EventType::output_added, .output = output.get() }));
    return output;
}

void scene::output::request_frame(scene::Output* output)
{
    scene_client_post_event(output->client, core::ptr_to(scene::Event {
        .type = scene::EventType::output_frame_request,
        .output = output,
    }));
}

void scene::output::set_viewport(scene::Output* output, rect2f32 viewport)
{
    if (output->viewport == viewport) return;
    auto* ctx = output->client->ctx;
    output->viewport = viewport;
    scene_broadcast_event(ctx, core::ptr_to(scene::Event { .type = scene::EventType::output_configured, .output = output }));
    scene_broadcast_event(ctx, core::ptr_to(scene::Event { .type = scene::EventType::output_layout }));
    scene::output::request_frame(output);
}

auto scene::list_outputs(scene::Context* ctx) -> std::span<scene::Output* const>
{
    return ctx->outputs;
}

auto scene::output::get_viewport(scene::Output* out) -> rect2f32
{
    return out->viewport;
}

auto scene::find_output_for_point(scene::Context* ctx, vec2f32 point) -> scene::FindOutputResult
{
    vec2f32       best_position = point;
    f32           best_distance = INFINITY;
    scene::Output* best_output   = nullptr;
    for (auto* output : scene::list_outputs(ctx)) {
        auto clamped = core::rect::clamp_point(scene::output::get_viewport(output), point);
        if (point == clamped) {
            best_position = point;
            best_output = output;
            break;
        } else if (f32 dist = glm::distance(clamped, point); dist < best_distance) {
            best_position = clamped;
            best_distance = dist;
            best_output = output;
        }
    }
    return { best_output, best_position };
}
