#pragma once

#include "scene/scene.hpp"

namespace way
{
    struct Server;

    auto create(core::EventLoop*, gpu::Context*, scene::Context*) -> core::Ref<way::Server>;
}
