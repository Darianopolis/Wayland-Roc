#include "wm.hpp"

auto wm_create(scene::Context* scene) -> core::Ref<wm_context>
{
    auto wm = core::create<wm_context>();
    wm->scene = scene;

    wm->main_mod = scene::Modifier::alt;

    wm_init_movesize(wm.get());

    return wm;
}
