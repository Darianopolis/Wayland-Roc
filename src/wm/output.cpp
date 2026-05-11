
#include "internal.hpp"

#include <core/math.hpp>

// -----------------------------------------------------------------------------
//      Outputs
// -----------------------------------------------------------------------------

static
void reflow_outputs(WmServer* wm, bool any_changed = false)
{
    enum class LayoutDir { LeftToRight, RightToLeft };
    static constexpr LayoutDir dir = LayoutDir::RightToLeft;

    f32 x = 0;
    bool first = true;
    for (auto* output : wm->io.outputs) {
        auto size = output->pixel_size;
        if constexpr (dir == LayoutDir::RightToLeft) {
            if (!std::exchange(first, false)) {
                x -= f32(size.x);
            }
        }
        auto last = output->viewport;
        output->viewport = {{x, 0.f}, size, xywh};
        if constexpr (dir == LayoutDir::LeftToRight) {
            x += f32(size.x);
        }

        if (last != output->viewport) {
            any_changed = true;
            wm_broadcast_event(wm, ptr_to(WmEvent {
                .output = {
                    .type = WmEventType::output_configured,
                    .output = output,
                }
            }));
        }
    }

    if (any_changed) {
        wm_broadcast_event(wm, ptr_to(WmEvent {
            .output = {
                .type = WmEventType::output_layout,
            }
        }));
        for (auto* output : wm->io.outputs) {
            output->interface.request_frame(output->userdata);
        }
    }
}

auto wm_output_create(WmServer* wm, void* userdata, WmOutputInterface interface) -> Ref<WmOutput>
{
    auto output = ref_create<WmOutput>();
    output->server = wm;
    output->userdata = userdata;
    output->interface = interface;

    wm->io.outputs.emplace_back(output.get());
    wm_broadcast_event(wm, ptr_to(WmEvent {
        .output = {
            .type = WmEventType::output_added,
            .output = output.get(),
        }
    }));
    reflow_outputs(wm, true);

    return output;
}

WmOutput::~WmOutput()
{
    std::erase(server->io.outputs, this);
    wm_broadcast_event(server, ptr_to(WmEvent {
        .output = {
            .type = WmEventType::output_removed,
            .output = this,
        }
    }));
    reflow_outputs(server, true);
}

void wm_output_set_pixel_size(WmOutput* output, vec2i32 pixel_size)
{
    output->pixel_size = pixel_size;
    reflow_outputs(output->server);
}

void wm_output_frame(WmOutput* output)
{
    auto* wm = output->server;

    wm_broadcast_event(wm, ptr_to(WmEvent {
        .output = {
            .type = WmEventType::output_frame,
            .output = output,
        }
    }));
}

auto wm_output_get_viewport(WmOutput* output) -> rect2f32
{
    return output->viewport;
}

void wm_request_frame(WmServer* wm)
{
    for (auto* output : wm->io.outputs) {
        output->interface.request_frame(output->userdata);
    }
}

auto wm_list_outputs(WmServer* wm) -> std::span<WmOutput* const>
{
    return wm->io.outputs;
}

auto wm_find_output_at(WmServer* wm, vec2f32 point) -> WmFindOutputResult
{
    vec2f32   best_position = point;
    f32       best_distance = INFINITY;
    WmOutput* best_output   = nullptr;
    for (auto* output : wm->io.outputs) {
        auto clamped = rect_clamp_point(output->viewport, point);
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
