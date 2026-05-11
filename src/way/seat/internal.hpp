#pragma once

#include "seat.hpp"

#include "../data/data.hpp"
#include "../surface/surface.hpp"
#include "../client.hpp"
#include "../shell/shell.hpp"

static
auto find_surface(WayClient* client, WmSurface* surface) -> WaySurface*
{
    if (!surface) return nullptr;
    for (auto* s : client->surfaces) {
        if (s->surface.get() == surface) return s;
    }
    return nullptr;
}
