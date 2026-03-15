#include "wm.hpp"

#include "scene/scene.hpp"

#include "io/io.hpp"

#include "imui/imui.hpp"
#include "way/way.hpp"
#include "way/internal.hpp"

int main()
{
    // Systems

    auto event_loop = core::event_loop::create();
    auto gpu = gpu::create({}, event_loop.get());
    auto io = io::create(event_loop.get(), gpu.get());
    auto scene = scene::create(gpu.get(), io.get());
    auto wm = wm_create(scene.get());

    // I/O event plumbing

    auto image_pool = gpu::image_pool::create(gpu.get());
    auto output_client = scene::client::create(scene.get());
    struct output {
        core::Ref<scene::Output> scene;
        io::Output* io;
    };
    std::vector<output> outputs;
    auto reflow_outputs = [&] {
        f32 x = 0;
        for (auto& output : outputs) {
            auto size = output.io->info().size;
            scene::output::set_viewport(output.scene.get(), {{x, 0.f}, size, core::xywh});
            x += f32(size.x);
        }
    };
    io::set_event_handler(io.get(), [&](io::Event* event) {
        switch (event->type) {
            // shutdown
            break;case io::EventType::shutdown_requested:
                io::stop(io.get());

            // input
            break;case io::EventType::input_added:
                  case io::EventType::input_removed:
                  case io::EventType::input_event:
                scene::push_io_event(scene.get(), event);

            // output
            break;case io::EventType::output_added:
                outputs.emplace_back(scene::output::create(output_client.get()), event->output.output);
                reflow_outputs();
            break;case io::EventType::output_configure:
                reflow_outputs();
            break;case io::EventType::output_removed:
                std::erase_if(outputs, [&](auto& p) { return p.io == event->output.output; });
                reflow_outputs();
            break;case io::EventType::output_frame: {
                auto output = std::ranges::find_if(outputs, [&](auto& p) { return p.io == event->output.output; });
                scene::frame(scene.get(), output->scene.get(), output->io, image_pool.get());
            }
        }
    });
    scene::client::set_event_handler(output_client.get(), [&](scene::Event* event) {
        switch (event->type) {
            break;case scene::EventType::output_frame_request: {
                auto output = std::ranges::find_if(outputs, [&](auto& p) { return p.scene.get() == event->output; });
                output->io->request_frame();
            }
            break;default:
                ;
        }
    });

    // Pointer

    scene::pointer::set_xcursor(scene::get_pointer(scene.get()), "default");

    scene::pointer::set_driver(scene::get_pointer(scene.get()), [scene = scene.get()](scene::PointerDriverIn in) -> scene::PointerDriverOut {

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

        static constexpr f32 offset     = 2.f;
        static constexpr f32 rate       = 0.05f;
        static constexpr f32 multiplier = 0.3f;

        f32 speed = glm::length(in.delta);
        vec2f32 sens = vec2f32(multiplier * (1 + (std::max(speed, offset) - offset) * rate));
        vec2f32 delta = in.delta * sens;

        vec2f32 new_pos = in.position + delta;

        return {
            .position = scene::find_output_for_point(scene, new_pos).position,
            .accel    = delta,
            .unaccel  = in.delta
        };
    });

    // Background

    auto sampler = gpu::sampler::create(gpu.get(), {
        .mag = VK_FILTER_NEAREST,
        .min = VK_FILTER_LINEAR,
    });

    auto background_image = [&] {
        std::filesystem::path path = getenv("WALLPAPER");
        int w, h;
        int num_channels;
        stbi_uc* data = stbi_load(path.c_str(), &w, &h, &num_channels, STBI_rgb_alpha);
        defer { stbi_image_free(data); };
        log_info("Loaded background ({}, width = {}, height = {})", path.c_str(), w, h);

        // Create background texture node
        auto image = gpu::image::create(gpu.get(), {
            .extent = {w, h},
            .format = gpu::format::from_drm(DRM_FORMAT_XBGR8888),
            .usage = gpu::ImageUsage::texture | gpu::ImageUsage::transfer
        });
        gpu::copy_memory_to_image(image.get(), {data, usz(w * h * 4)}, {{{image->extent()}}});
        return image;
    }();

    auto background_client = scene::client::create(scene.get());

    core::Ref<scene::Tree> background_layer = {};
    auto unparent_background = [&] {
        if (background_layer) scene::node::unparent(background_layer.get());
    };
    defer { unparent_background(); };
    auto update_backgrounds = [&] {
        unparent_background();
        background_layer = scene::tree::create(scene.get());
        scene::tree::place_above(scene::get_layer(scene.get(), scene::Layer::background), nullptr, background_layer.get());

        for (auto* output : scene::list_outputs(scene.get())) {
            vec2f32 image_size = background_image->extent();
            auto viewport = scene::output::get_viewport(output);

            // Create texture node
            auto texture = scene::texture::create(scene.get());
            scene::texture::set_image(texture.get(), background_image.get(), sampler.get(), gpu::BlendMode::premultiplied);
            auto src = core::rect::fit<f32>(image_size, viewport.extent);
            scene::texture::set_src(texture.get(), {src.origin / image_size, src.extent / image_size, core::xywh});
            scene::texture::set_dst(texture.get(), viewport);
            scene::tree::place_above(background_layer.get(), nullptr, texture.get());
        }
    };

    scene::client::set_event_handler(background_client.get(), [scene = scene.get(), &update_backgrounds](scene::Event* event) {
        switch (event->type) {
            break;case scene::EventType::output_layout:
                update_backgrounds();
            break;default:
                ;
        }
    });

    // Test client

    auto client = scene::client::create(scene.get());

    auto window = scene::window::create(client.get());
    auto initial_size = vec2f32{256, 256};
    scene::window::set_frame(window.get(), {{64, 64}, initial_size, core::xywh});

    auto canvas = scene::texture::create(scene.get());
    scene::texture::set_tint(canvas.get(), {255, 0, 255, 255});
    scene::texture::set_dst(canvas.get(), {{}, initial_size, core::xywh});
    scene::tree::place_below(scene::window::get_tree(window.get()), nullptr, canvas.get());

    auto input = scene::input_region::create(client.get());
    scene::input_region::set_region(input.get(), {{{}, initial_size, core::xywh}});
    scene::tree::place_above(scene::window::get_tree(window.get()), nullptr, input.get());

    auto inner = scene::tree::create(scene.get());
    scene::tree::set_translation(inner.get(), {64, 64});
    scene::tree::place_above(scene::window::get_tree(window.get()), nullptr, inner.get());

    auto square = scene::texture::create(scene.get());
    scene::texture::set_tint(square.get(), {0, 255, 255, 255});
    scene::texture::set_dst(square.get(), {{}, {128, 128}, core::xywh});
    scene::tree::place_above(inner.get(), nullptr, square.get());

    scene::client::set_event_handler(client.get(), [&](scene::Event* event) {
        switch (event->type) {
            break;case scene::EventType::keyboard_key:
                log_trace("keyboard_key({}, {})",
                    libevdev_event_code_get_name(EV_KEY, event->keyboard.key.code),
                    event->keyboard.key.pressed ? "pressed" : "released");
            break;case scene::EventType::keyboard_modifier:
                log_trace("keyboard_modifier({})", core::to_string(scene::keyboard::get_modifiers(event->keyboard.keyboard)));
            break;case scene::EventType::pointer_motion:
                log_trace("pointer_motion(accel: {}, unaccel: {}, pos: {})",
                    core::to_string(event->pointer.motion.rel_accel),
                    core::to_string(event->pointer.motion.rel_unaccel),
                    core::to_string(scene::pointer::get_position(event->pointer.pointer)));
            break;case scene::EventType::pointer_button:
                log_trace("pointer_button({}, {})",
                    libevdev_event_code_get_name(EV_KEY, event->pointer.button.code),
                    event->pointer.button.pressed ? "pressed" : "released");
                scene::window::raise(window.get());
            break;case scene::EventType::pointer_scroll:
                log_trace("pointer_scroll(delta: {})", core::to_string(event->pointer.scroll.delta));
            break;case scene::EventType::pointer_enter:
                log_trace("pointer_enter({}, region: {})", (void*)event->pointer.pointer, (void*)event->pointer.focus.region);
            break;case scene::EventType::pointer_leave:
                log_trace("pointer_leave({})", (void*)event->pointer.pointer);
            break;case scene::EventType::keyboard_enter:
                log_trace("keyboard_enter({})", (void*)event->keyboard.keyboard);
            break;case scene::EventType::keyboard_leave:
                log_trace("keyboard_leave({})", (void*)event->keyboard.keyboard);
            break;case scene::EventType::window_reposition: {
                auto frame = event->window.reposition.frame;
                scene::texture::set_dst(       canvas.get(), {{}, frame.extent, core::xywh});
                scene::input_region::set_region(input.get(), {{{}, frame.extent, core::xywh}});
                scene::window::set_frame(event->window.window, frame);
            }
            break;case scene::EventType::output_frame:
                  case scene::EventType::output_added:
                  case scene::EventType::output_removed:
                  case scene::EventType::output_configured:
                  case scene::EventType::output_frame_request:
                  case scene::EventType::output_layout:
                ;
            break;case scene::EventType::hotkey:
                ;
            break;case scene::EventType::selection:
                log_trace("selection({})", (void*)event->data.source);
        }
    });

    scene::window::map(window.get());

    // Wayland

    auto way = way_create(event_loop.get(), gpu.get(), scene.get());

    // ImGui

    std::string imui_text_edit = "Hello, world!";
    auto imui = imui_create(gpu.get(), scene.get());
    imui_add_frame_handler(imui.get(), [&] {
        ImGui::ShowDemoWindow();

        defer { ImGui::End(); };
        if (ImGui::Begin("Roc")) {
            if (ImGui::Button("New Output")) {
                io::add_output(io.get());
            }

            if (ImGui::Button("Reposition")) {
                if (auto* window = imui_get_window(ImGui::GetCurrentWindow())) {
                    scene::window::request_reposition(window, {{}, {512, 512}, core::xywh}, {});
                }
            }

            {
                defer {  ImGui::EndDisabled(); };
                ImGui::BeginDisabled(!gpu->renderdoc);
                if (ImGui::Button("Capture")) {
                    static u32 capture = 0;
                    gpu->renderdoc->StartFrameCapture(nullptr, nullptr);
                    gpu->renderdoc->SetCaptureTitle(std::format("Roc capture {}", ++capture).c_str());
                    for (auto* output : scene::list_outputs(scene.get())) {
                        auto viewport =  scene::output::get_viewport(output);
                        auto texture = gpu::image::create(gpu.get(), {
                            .extent = viewport.extent,
                            .format = gpu::format::from_drm(DRM_FORMAT_ABGR8888),
                            .usage = gpu::ImageUsage::render
                        });
                        scene::render(scene.get(), texture.get(), viewport);
                    }
                    gpu->renderdoc->EndFrameCapture(nullptr, nullptr);
                }
            }

            if (ImGui::Button("Print Scene Graph")) {
                u32 depth = 0;
                auto indent = [&] { return std::string(depth, ' '); };
                scene::iterate(scene::get_layer(scene.get(), scene::Layer::window)->parent,
                    scene::IterateDirection::back_to_front,
                    [&](scene::Tree* tree) {
                        way_surface* surface;
                        if (tree->system == way->scene_system
                                && (surface = way_get_userdata<way_surface>(tree))) {
                            log_warn("{}tree({}{}) {{", indent(),
                                core::to_string(surface->role),
                                tree->enabled ? "": ", disabled");
                        } else {
                            log_warn("{}tree{} {{", indent(), tree->enabled ? "": "(disabled)");
                        }
                        depth += 2;
                        return scene::IterateAction::next;
                    },
                    [&](scene::Node* node) {
                        log_warn("{}{}", indent(), core::to_string(node->type));
                        return scene::IterateAction::next;
                    },
                    [&](scene::Tree* tree) {
                        depth -= 2;
                        log_warn("{}}}", indent());
                        return scene::IterateAction::next;
                    });
            }

            ImGui::InputText("Text", &imui_text_edit);
        }
    });
    imui_request_frame(imui.get());

    // Hotkey

    auto main_mod = scene::Modifier::alt;
    auto hotkey_client = scene::client::create(scene.get());
    core::Map<scene::Hotkey, std::string> hotkeys;
    hotkeys[{ main_mod,                         KEY_SPACE  }] = "launcher";
    hotkeys[{ main_mod,                         KEY_Q      }] = "close-focused";
    hotkeys[{ main_mod,                         KEY_S      }] = "clear-focus";
    hotkeys[{ main_mod,                         BTN_MIDDLE }] = "close-under-cursor";
    for (auto[hotkey, _] : hotkeys) {
        core_assert(scene::client::hotkey_register(hotkey_client.get(), hotkey));
    }
    scene::client::set_event_handler(hotkey_client.get(), [&](scene::Event* event) {
        if (event->type == scene::EventType::hotkey) {
            log_debug("hotkey({} - {})", hotkeys.at(event->hotkey.hotkey), event->hotkey.pressed ? "pressed" : "released");
        }
    });

    // Selection

    auto data_client = scene::client::create(scene.get());
    scene::client::set_event_handler(data_client.get(), [](scene::Event*) {});
    auto data_source = scene::data_source::create(data_client.get(), {
        .send = [&](const char* mime, int fd) {
            std::string message = "This is a test clipboard message.";
            write(fd, message.data(), message.size());
        }
    });
    scene::data_source::offer(data_source.get(), "text/plain;charset=utf-8");
    scene::data_source::offer(data_source.get(), "text/plain");
    scene::data_source::offer(data_source.get(), "text/html");
    scene::set_selection(scene.get(), data_source.get());

    // Run

    io::run(io.get());
}
