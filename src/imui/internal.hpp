#pragma once

#include "imui.hpp"

namespace imui
{
    struct ViewportData {
        core::Ref<scene::Window>       window;
        core::Ref<scene::Tree>         draws;
        core::Ref<scene::InputRegion> input_plane;

        // Pending reposition request. Requests are double-buffered so that
        // resizes requested during ImGui frames are handled correctly.
        std::optional<rect2f32> reposition;
    };

    struct Context
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

        std::vector<std::move_only_function<imui::FrameFn>> frame_handlers;

        scene::Keyboard* keyboard;
        scene::Pointer*  pointer;

        struct {
            std::string text;
        } clipboard;

        ~Context();
    };

    void init(imui::Context*);
    void frame(imui::Context*);
    void handle_key(imui::Context*, scene::Scancode, bool pressed);
    void handle_mods(imui::Context*);
    void handle_motion(imui::Context*);
    void handle_button(imui::Context*, scene::Scancode, bool pressed);
    void handle_wheel(imui::Context*, vec2f32 delta);
    void handle_keyboard_enter(imui::Context*, scene::Keyboard*, scene::InputRegion*);
    void handle_keyboard_leave(imui::Context*);
    void handle_pointer_enter(imui::Context*, scene::Pointer*, scene::InputRegion*);
    void handle_pointer_leave(imui::Context*);
    void handle_output_layout(imui::Context*);
}
