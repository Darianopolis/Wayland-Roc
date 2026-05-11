#include "shell.hpp"

void handle_key(Shell* shell, WmSeat* seat, bool quiet, IoInputChannel channel)
{
    // TODO: Hacky fix to remap mouse side button to super key
    if (channel.code == BTN_SIDE) {
        channel.code = KEY_LEFTMETA;
    }

    switch (channel.code) {
        break;case BTN_MOUSE ... BTN_TASK:
            if (channel.value) {
                wm_pointer_press(seat, channel.code);
            } else {
                wm_pointer_release(seat, channel.code);
            }
        break;case KEY_ESC      ... KEY_MICMUTE:
            case KEY_OK         ... KEY_LIGHTS_TOGGLE:
            case KEY_ALS_TOGGLE ... KEY_PERFORMANCE: {
            if (channel.value) {
                wm_keyboard_press(seat, channel.code);
            } else {
                wm_keyboard_release(seat, channel.code);
            }
        }
        break;default:
            ;
    }
};

auto apply_accel(vec2f32 delta) -> vec2f32
{
    static constexpr f32 offset     = 2.f;
    static constexpr f32 rate       = 0.05f;
    static constexpr f32 multiplier = 0.3;

    // Apply a linear mouse acceleration curve
    //
    // Offset     - speed before acceleration is applied.
    // Accel      - rate that sensitivity increases with motion.
    // Multiplier - total multplier for sensitivity.
    //
    //      /
    //     / <- Accel
    // ___/
    //  ^-- Offset

    f32 speed = glm::length(delta);
    vec2f32 sens = vec2f32(multiplier * (1 + (std::max(speed, offset) - offset) * rate));

    return delta * sens;
};

auto handle_input_events(Shell* shell, bool quiet, std::span<IoInputChannel const> events)
{
    auto* seat = wm_get_seats(shell->wm).front();

    vec2f32 motion = {};
    vec2f32 scroll = {};

    for (auto& channel : events) {
        switch (channel.type) {
            break;case EV_KEY:
                handle_key(shell, seat, quiet, channel);
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

    if (motion.x || motion.y) wm_pointer_move(seat, apply_accel(motion), motion);
    if (scroll.x || scroll.y) wm_pointer_scroll(seat, scroll);
};

struct ShellOutput
{
    IoOutput* io;
    Ref<WmOutput> wm;
};

auto find_output(std::vector<ShellOutput> outputs, IoOutput* output) -> WmOutput* {
    for (auto& o : outputs) {
        if (o.io == output) return o.wm.get();
    }
    return nullptr;
};

static
void handle_io_event(Shell* shell, GpuImagePool* image_pool, std::vector<ShellOutput>& outputs, IoEvent* event)
{
    switch (event->type) {
        // shutdown
        break;case IoEventType::shutdown_requested:
            io_stop(shell->io);

        // input
        break;case IoEventType::input_added:
        break;case IoEventType::input_removed:
        break;case IoEventType::input_event:
            handle_input_events(shell, event->input.quiet, event->input.channels);

        // output
        break;case IoEventType::output_added:
            outputs.emplace_back(event->output.output, wm_output_create(shell->wm, event->output.output, WmOutputInterface {
                .request_frame = [](void* data) {
                    static_cast<IoOutput*>(data)->request_frame();
                },
            }));
        break;case IoEventType::output_configure:
            wm_output_set_pixel_size(find_output(outputs, event->output.output), event->output.output->info().size);
        break;case IoEventType::output_removed:
            std::erase_if(outputs, [&](const auto& o) { return o.io == event->output.output; });
        break;case IoEventType::output_frame: {
            auto io_output = event->output.output;
            auto output = find_output(outputs, io_output);

            wm_output_frame(output);

            // TODO: Only redraw with damage

            auto format = gpu_format_from_drm(DRM_FORMAT_ABGR8888);
            auto usage = GpuImageUsage::render;

            auto target = image_pool->acquire({
                .extent = io_output->info().size,
                .format = format,
                .usage = usage,
                .modifiers = ptr_to(gpu_intersect_format_modifiers({{
                    &gpu_get_format_properties(shell->gpu, format, usage)->mods,
                    &io_output->info().formats->get(format),
                }}))
            });

            scene_render(wm_get_scene(shell->wm), target.get(), wm_output_get_viewport(output));

            io_output->commit(target.get(), gpu_flush(shell->gpu), IoOutputCommitFlag::vsync);
        }
    }
}

void shell_run_io(Shell* shell)
{
    auto image_pool = gpu_image_pool_create(shell->gpu);
    std::vector<ShellOutput> outputs;

    io_set_event_handler(shell->io, [&](IoEvent* event) {
        handle_io_event(shell, image_pool.get(), outputs, event);
    });

    // Run

    io_run(shell->io);
}
