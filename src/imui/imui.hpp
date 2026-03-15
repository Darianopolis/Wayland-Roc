#pragma once

#include "scene/scene.hpp"

namespace imui
{
    struct Context;

    auto create(gpu::Context*, scene::Context*) -> core::Ref<imui::Context>;

// -----------------------------------------------------------------------------

    using FrameFn = void();

    void request_frame(imui::Context*);
    void add_frame_handler(imui::Context*, std::move_only_function<imui::FrameFn>&&);
    auto get_texture(imui::Context*, gpu::Image*, gpu::Sampler*, gpu::BlendMode) -> ImTextureID;

    auto get_window(ImGuiWindow*) -> scene::Window*;

// -----------------------------------------------------------------------------

    template<typename ...Args>
    void text(std::format_string<Args...> fmt, Args&&... args)
    {
        ImGui::TextUnformatted(std::vformat(fmt.get(), std::make_format_args(args...)).c_str());
    }
}
