#pragma once

#include "imui.hpp"

struct imui_viewport_data {
    core::Ref<scene::Window>       window;
    core::Ref<scene::Tree>         draws;
    core::Ref<scene::InputRegion> input_plane;

    // Pending reposition request. Requests are double-buffered so that
    // resizes requested during ImGui frames are handled correctly.
    std::optional<rect2f32> reposition;
};

struct imui_context
{
    gpu::Context*   gpu;
    scene::Context* scene;

    core::Ref<gpu::Sampler>  sampler;
    core::Ref<scene::Client> client;
    ImGuiContext*     context;
    u32 frames_requested = 0;

    struct texture {
        core::Ref<gpu::Image>   image;
        core::Ref<gpu::Sampler> sampler;
        gpu::BlendMode   blend;
    };
    std::vector<texture> textures;
    core::Ref<gpu::Image> font_image;

    std::vector<std::move_only_function<imui_frame_fn>> frame_handlers;

    scene::Keyboard* keyboard;
    scene::Pointer*  pointer;

    struct {
        std::string text;
    } clipboard;

    ~imui_context();
};

void imui_init(imui_context*);
void imui_frame(imui_context*);
void imui_handle_key(imui_context*, scene::Scancode, bool pressed);
void imui_handle_mods(imui_context*);
void imui_handle_motion(imui_context*);
void imui_handle_button(imui_context*, scene::Scancode, bool pressed);
void imui_handle_wheel(imui_context*, vec2f32 delta);
void imui_handle_keyboard_enter(imui_context*, scene::Keyboard*, scene::InputRegion*);
void imui_handle_keyboard_leave(imui_context*);
void imui_handle_pointer_enter(imui_context*, scene::Pointer*, scene::InputRegion*);
void imui_handle_pointer_leave(imui_context*);
void imui_handle_output_layout(imui_context*);
