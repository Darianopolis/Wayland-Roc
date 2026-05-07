#include "wm.hpp"

auto wm_surface_create(WmClient* client) -> Ref<WmSurface>
{
    auto surface = ref_create<WmSurface>();
    surface->client = client;

    surface->tree = scene_tree_create();

    surface->texture = scene_texture_create();
    scene_tree_place_above(surface->tree.get(), nullptr, surface->texture.get());

    surface->input_region = scene_input_region_create();
    surface->focus = seat_focus_create(wm_get_seat_client(client), surface->input_region.get());
    scene_tree_place_above(surface->tree.get(), nullptr, surface->input_region.get());

    return surface;
}

static
void unparent(WmSurface* surface)
{
    if (!surface->parent) return;
    std::erase(surface->parent->children, surface);
    surface->parent = nullptr;
}

WmSurface::~WmSurface()
{
    unparent(this);
    for (auto* child : children) {
        child->parent = nullptr;
    }
}

void wm_surface_set_parent(WmSurface* surface, WmSurface* parent)
{
    if (surface->parent == parent) return;

    unparent(surface);

    surface->parent = parent;
    parent->children.emplace_back(surface);

    // Place into parent's surface stack
    scene_tree_place_above(parent->tree.get(), nullptr, surface->tree.get());
}

auto wm_surface_contains_focus(WmSurface* surface, SeatFocus* focus) -> bool
{
    if (surface->focus.get() == focus) return true;
    for (auto* child : surface->children) {
        if (wm_surface_contains_focus(child, focus)) return true;
    }
    return false;
}
