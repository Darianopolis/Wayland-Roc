#pragma once

#include "ui.hpp"

struct UiViewportData {
    Ref<WmSurface> surface;
    Ref<WmWindow> window;
    RefVector<SceneMesh> meshes;

    rect2f32 frame;
};

struct Ui
{
    Gpu* gpu;

    WmServer* wm;
    Ref<WmClient> client;

    std::chrono::steady_clock::time_point last_frame = {};

    std::string ini_path;

    Ref<GpuSampler> sampler;
    ImGuiContext* context;
    u32 frames_requested = 0;

    struct Texture {
        Ref<GpuImage>   image;
        Ref<GpuSampler> sampler;
        GpuBlendMode    blend;
    };
    std::vector<Texture> textures;
    Ref<GpuImage> font_image;

    std::move_only_function<UiFrameFn> frame_handler;

    struct {
        std::string text;
    } clipboard;

    ~Ui();
};

struct UiContextGuard
{
    ImGuiContext* old;

    UiContextGuard(ImGuiContext* imgui)
        : old(ImGui::GetCurrentContext())
    {
        ImGui::SetCurrentContext(imgui);
    }

    ~UiContextGuard()
    {
        ImGui::SetCurrentContext(old);
    }
};
