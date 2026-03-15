#pragma once

#include "scene/scene.hpp"

enum class wm_movesize_mode
{
    none,
    move,
    size,
};

struct wm_context
{
    scene::Context* scene;

    scene::Modifier main_mod;

    struct {
        scene::Pointer* pointer;

        core::Ref<scene::Client> client;
        core::Weak<scene::Window> window;

        vec2f32  grab;
        rect2f32 frame;
        vec2f32  relative;

        wm_movesize_mode mode;
    } movesize;
};

auto wm_create(scene::Context*) -> core::Ref<wm_context>;

void wm_init_movesize(wm_context*);
