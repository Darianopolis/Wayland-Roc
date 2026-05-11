#include "internal.hpp"

auto wm_surface_create(WmClient* client) -> Ref<WmSurface>
{
    auto surface = ref_create<WmSurface>();
    surface->client = client;

    client->surfaces.emplace_back(surface.get());

    surface->tree = scene_tree_create();

    surface->texture = scene_texture_create();
    scene_tree_place_above(surface->tree.get(), nullptr, surface->texture.get());

    surface->input_region = scene_input_region_create();
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
    std::erase(client->surfaces, this);
    for (auto* child : children) {
        child->parent = nullptr;
    }

    auto* wm = client->wm;
    for (auto* seat : wm->seats) {
        if (wm_keyboard_get_focus(seat) == this) wm_keyboard_focus(seat, nullptr);
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

auto wm_surface_contains(WmSurface* haystack, WmSurface* needle) -> bool
{
    if (haystack == needle) return true;
    for (auto* child : haystack->children) {
        if (wm_surface_contains(child, needle)) return true;
    }
    return false;
}
