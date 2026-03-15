#include "internal.hpp"

static
void get_subsurface(wl_client* client, wl_resource* resource, u32 id, wl_resource* wl_surface, wl_resource* wl_parent)
{
    auto* surface = way::get_userdata<way::Surface>(wl_surface);
    surface->role = way::SurfaceRole::subsurface;
    surface->subsurface.resource = way_resource_create_refcounted(wl_subsurface, client, resource, id, surface);

    auto* parent = way::get_userdata<way::Surface>(wl_parent);
    surface->parent = parent;

    // Subsurfaces start synchronized
    surface->subsurface.synchronized = true;

    // Place into parent's surface stack
    parent->pending->subsurface.places.emplace_back(way::SubsurfacePlace {
        .reference = nullptr,
        .subsurface = surface,
        .above = true,
    });
}

WAY_INTERFACE(wl_subcompositor) = {
    .destroy = way::simple_destroy,
    .get_subsurface = get_subsurface,
};

WAY_BIND_GLOBAL(wl_subcompositor, bind)
{
    way_resource_create_unsafe(wl_subcompositor, bind.client, bind.version, bind.id, bind.server);
}

// -----------------------------------------------------------------------------

static
void set_position(wl_client* client, wl_resource* resource, i32 x, i32 y)
{
    auto* surface = way::get_userdata<way::Surface>(resource);
    if (!surface->parent) return;
    surface->parent->pending->subsurface.moves.emplace_back(way::SubsurfaceMove {
        .subsurface = surface,
        .position = {x, y},
    });
}

template<bool above>
void place(wl_client* client, wl_resource* resource, wl_resource* sibling)
{
    auto* surface = way::get_userdata<way::Surface>(resource);
    if (!surface->parent) return;
    surface->parent->pending->subsurface.places.emplace_back(way::SubsurfacePlace {
        .reference = way::get_userdata<way::Surface>(sibling),
        .subsurface = surface,
        .above = above,
    });
}

static
bool is_synchronized(way::Surface* surface)
{
    if (surface->role != way::SurfaceRole::subsurface) return false;
    return surface->subsurface.synchronized || (surface->parent && is_synchronized(surface->parent.get()));
}

template<bool sync>
void set_sync(wl_client* client, wl_resource* resource)
{
    auto* surface = way::get_userdata<way::Surface>(resource);
    surface->subsurface.synchronized = sync;
}

WAY_INTERFACE(wl_subsurface) = {
    .destroy = way::simple_destroy,
    .set_position = set_position,
    .place_above = place<true>,
    .place_below = place<false>,
    .set_sync   = set_sync<true>,
    .set_desync = set_sync<false>,
};

// -----------------------------------------------------------------------------

void way::subsurface::commit(way::Surface* surface, way::SurfaceState& pending)
{
    if (is_synchronized(surface) && surface->parent) {
        pending.parent.commit = surface->parent->last_commit_id + 1;
        pending.set(way::SurfaceCommittedState::parent_commit);
    }
}

void way::subsurface::apply(way::Surface* surface, way::SurfaceState& from)
{
    // Arrange child subsurfaces

    for (auto& move : from.subsurface.moves) {
        scene::tree::set_translation(move.subsurface->scene.tree.get(), move.position);
    }
    from.subsurface.moves.clear();

    for (auto& place : from.subsurface.places) {
        auto* reference = place.reference ? place.reference->scene.tree.get() : nullptr;
        if (place.above) {
            scene::tree::place_above(
                surface->scene.tree.get(),
                reference,
                place.subsurface->scene.tree.get());
        } else {
            scene::tree::place_below(
                surface->scene.tree.get(),
                reference,
                place.subsurface->scene.tree.get());
        }
    }
    from.subsurface.places.clear();
}
