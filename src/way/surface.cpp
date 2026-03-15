#include "internal.hpp"

WAY_INTERFACE(wl_region) = {
    .destroy = way::simple_destroy,
    .add = [](wl_client* client, wl_resource* resource, i32 x, i32 y, i32 w, i32 h) {
        way::get_userdata<way::Region>(resource)->region.add({{x, y}, {w, h}, core::xywh});
    },
    .subtract = [](wl_client* client, wl_resource* resource, i32 x, i32 y, i32 w, i32 h) {
        way::get_userdata<way::Region>(resource)->region.subtract({{x, y}, {w, h}, core::xywh});
    }
};

static
void create_region(wl_client* client, wl_resource* resource, u32 id)
{
    auto region = core::create<way::Region>();
    region->resource = way_resource_create_refcounted(wl_region, client, resource, id, region.get());
}

// -----------------------------------------------------------------------------

static
void create_surface(wl_client* client, wl_resource* resource, u32 id)
{
    auto surface = core::create<way::Surface>();

    surface->client = way::client::from(way::get_userdata<way::Server>(resource), client);
    surface->client->surfaces.emplace_back(surface.get());

    surface->pending = &surface->cached.emplace_back();

    auto* server = surface->client->server;
    auto* scene = server->scene;

    surface->scene.tree = scene::tree::create(scene);
    surface->scene.tree->system = server->scene_system;
    surface->scene.tree->userdata = surface.get();
    scene::tree::set_enabled(surface->scene.tree.get(), false);

    surface->scene.texture = scene::texture::create(scene);
    scene::tree::place_above(surface->scene.tree.get(), nullptr, surface->scene.texture.get());

    surface->scene.input_region = scene::input_region::create(surface->client->scene.get());
    scene::tree::place_above(surface->scene.tree.get(), nullptr, surface->scene.input_region.get());

    surface->wl_surface = way_resource_create_refcounted(wl_surface, client, resource, id, surface.get());
}

WAY_INTERFACE(wl_compositor) = {
    .create_surface = create_surface,
    .create_region  = create_region,
};

WAY_BIND_GLOBAL(wl_compositor, bind)
{
    way_resource_create_unsafe(wl_compositor, bind.client, bind.version, bind.id, bind.server);
}

// -----------------------------------------------------------------------------

static
void offset(wl_client* client, wl_resource* resource, i32 dx, i32 dy)
{
    auto* surface = way::get_userdata<way::Surface>(resource);
    surface->pending->surface.offset += vec2i32{dx, dy};
}

static
void attach(wl_client* client, wl_resource* resource, wl_resource* wl_buffer, i32 dx, i32 dy)
{
    auto* surface = way::get_userdata<way::Surface>(resource);
    auto* pending = surface->pending;

    core_assert(!pending->image);

    if (!wl_buffer) {
        pending->buffer = nullptr;
        pending->unset(way::SurfaceCommittedState::buffer);
        return;
    }

    pending->buffer = way::get_userdata<way::Buffer>(wl_buffer);
    pending->set(way::SurfaceCommittedState::buffer);

    surface->pending->surface.offset += vec2i32{dx, dy};
}

static
void damage(wl_client* client, wl_resource* resource, i32 x, i32 y, i32 width, i32 height)
{
    auto* surface = way::get_userdata<way::Surface>(resource);
    auto* pending = surface->pending;

    pending->surface.damage.damage({{x, y}, {width, height}, core::xywh});
}

static
void damage_buffer(wl_client* client, wl_resource* resource, i32 x, i32 y, i32 width, i32 height)
{
    auto* surface = way::get_userdata<way::Surface>(resource);
    auto* pending = surface->pending;

    pending->buffer_damage.damage({{x, y}, {width, height}, core::xywh});
}

static
void frame(wl_client* client, wl_resource* resource, u32 id)
{
    auto* surface = way::get_userdata<way::Surface>(resource);
    auto* pending = surface->pending;

    auto callback = way::resource_create(client, &wl_callback_interface, resource, id, nullptr, nullptr, false);

    pending->surface.frame_callbacks.emplace_back(callback);
}

void way::surface::on_redraw(way::Surface* surface)
{
    auto* server = surface->client->server;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(way::get_elapsed(server)).count();

    auto send_frame_callbacks = [&](way::ResourceList& list) {
        while (auto callback = list.front()) {
            way_send(server, wl_callback_send_done, callback, ms);
            wl_resource_destroy(callback);
        }
    };

    send_frame_callbacks(surface->current.surface.frame_callbacks);
    for (auto& pending : surface->cached) {
        if (!pending.commit) continue;
        send_frame_callbacks(pending.surface.frame_callbacks);
    }

    way::queue_client_flush(server);
}

static
void set_input_region(wl_client* client, wl_resource* resource, wl_resource* region)
{
    auto* surface = way::get_userdata<way::Surface>(resource);
    auto* pending = surface->pending;

    if (region) {
        pending->surface.input_region = way::get_userdata<way::Region>(region)->region;
        pending->set(way::SurfaceCommittedState::input_region);
    } else {
        pending->unset(way::SurfaceCommittedState::input_region);
    }
}

way::SurfaceState::~SurfaceState()
{
    // TODO: Empty callbacks
}

static
void surface_set_mapped(way::Surface* surface, bool mapped)
{
    if (mapped == surface->mapped) return;
    surface->mapped = mapped;

    log_info("Surface {} was {}", (void*)surface, mapped ? "mapped" : "unmapped");

    scene::tree::set_enabled(surface->scene.tree.get(), mapped);

    if (surface->role == way::SurfaceRole::xdg_toplevel) {
        way::toplevel::on_map_change(surface, mapped);
    }
}

static
void update_map_state(way::Surface* surface)
{
    bool can_be_mapped =
           surface->current.buffer
        && surface->current.image
        && surface->wl_surface;

    surface_set_mapped(surface, can_be_mapped);
}

static
void apply(way::Surface* surface, way::SurfaceState& from)
{
    auto& to = surface->current;

    to.commit = from.commit;

    to.committed.set |=  from.committed.set;
    to.committed.set &= ~from.committed.unset;

    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, buffer_transform, buffer_transform);
    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, buffer_scale,     buffer_scale);

    to.surface.frame_callbacks.take_and_append_all(std::move(from.surface.frame_callbacks));

    // Offset

    if (from.surface.offset.x || from.surface.offset.y) {
        switch (surface->role) {
            break;case way::SurfaceRole::cursor:
                  case way::SurfaceRole::drag_icon:
                scene::tree::set_translation(surface->scene.tree.get(), surface->scene.tree->translation + vec2f32(from.surface.offset));
            break;default:
                ;
        }
    }

    // Buffer state

    if (from.is_set(way::SurfaceCommittedState::buffer)) {
        to.buffer = std::move(from.buffer);
        to.image  = std::move(from.image);

        scene::texture::set_image(surface->scene.texture.get(),
            to.image.get(),
            surface->client->server->sampler.get(),
            gpu::BlendMode::premultiplied);

        if (from.buffer_damage) {
            scene::texture::damage(surface->scene.texture.get(), from.buffer_damage.bounds());
        }

    } else if (from.is_unset(way::SurfaceCommittedState::buffer)) {
        to.buffer = nullptr;

        scene::texture::set_image(surface->scene.texture.get(), nullptr, nullptr, gpu::BlendMode::none);
    }

    // Buffer source / destination

    way::viewport::apply(surface, from);

    // Input regions

    if (from.is_set(way::SurfaceCommittedState::input_region)) {
        // TODO: Clip set input_regions against surface bounds?
        scene::input_region::set_region(surface->scene.input_region.get(), std::move(from.surface.input_region));

    } else if (!to.is_set(way::SurfaceCommittedState::input_region) && to.buffer) {
        // Unset input_region fills entire surface
        scene::input_region::set_region(surface->scene.input_region.get(),
            {{{}, to.buffer->extent, core::xywh}});
    }

    // Map state

    update_map_state(surface);

    // Component state

    if (surface->xdg_surface) {
        way::xdg_surface::apply(surface, from);
    }

    switch (surface->role) {
        break;case way::SurfaceRole::xdg_toplevel:
            way::toplevel::apply(surface, from);
        break;case way::SurfaceRole::subsurface:
        break;case way::SurfaceRole::cursor:
        break;case way::SurfaceRole::drag_icon:
        break;case way::SurfaceRole::xdg_popup:
            way::popup::apply(surface, from);
        break;case way::SurfaceRole::none:
            ;
    }

    way::subsurface::apply(surface, from);
}

static
auto is_blocked_by_parent(way::Surface* surface, way::SurfaceState& pending) -> bool
{
    if (!pending.is_set(way::SurfaceCommittedState::parent_commit)) return false;

    if (surface->parent && surface->parent->current.commit < pending.parent.commit) {
        return true;
    } else if (!surface->parent) {
        log_warn("parent_commit set but parent is gone, applying");
    }

    return false;
}

static
void flush(way::Surface* surface)
{
    // TODO: Queued applications

    auto prev_applied_commit_id = surface->current.commit;

    while (surface->cached.size() > 1) {
        auto& packet = surface->cached.front();

        if (is_blocked_by_parent(surface, packet)) break;

        // Convert surface damage to buffer damage

        if (packet.surface.damage) {
            auto bounds = packet.surface.damage.bounds();

            // Apply buffer transform
            auto transform = packet.is_set(way::SurfaceCommittedState::buffer_transform)
                ? packet.buffer_transform
                : surface->current.buffer_transform;
            core_assert(transform == WL_OUTPUT_TRANSFORM_NORMAL, "TODO: Support buffer transforms");

            // Apply buffer scale
            bounds.min *= packet.buffer_scale;
            bounds.max *= packet.buffer_scale;

            packet.buffer_damage.damage(bounds);
            packet.surface.damage.clear();
        }

        // Check for buffer ready

        core_assert(!packet.image);
        if (packet.buffer && !(packet.image = packet.buffer->acquire(surface, packet))) {
            core::debugkill();
        }

        apply(surface, packet);

        surface->cached.pop_front();
    }

    if (surface->current.commit == prev_applied_commit_id) return;

    // Flush subsurface state recursively

    auto* server = surface->client->server;

    for (auto* child : surface->scene.tree->children) {
        if (child->type != scene::NodeType::tree) continue;
        auto* tree = static_cast<scene::Tree*>(child);
        if (tree->system != server->scene_system) continue;
        flush(way::get_userdata<way::Surface>(tree->userdata));
    }
}

static
void commit(wl_client* client, wl_resource* resource)
{
    auto* surface = way::get_userdata<way::Surface>(resource);

    auto* pending = surface->pending;
    pending->commit = ++surface->last_commit_id;
    surface->pending = &surface->cached.emplace_back();

    // Queue frame request for frame callbacks

    if (pending->surface.frame_callbacks.front()) {
        scene::request_frame(surface->client->server->scene);
    }

    // Apply subsurface synchronization barriers

    if (surface->role == way::SurfaceRole::subsurface) {
        way::subsurface::commit(surface, *pending);
    }

    if (!pending->is_set(way::SurfaceCommittedState::buffer)) {
        core_assert(!pending->buffer_damage,  "TODO: wl_surface::damage_buffer without attached buffer");
        core_assert(!pending->surface.damage, "TODO: wl_surface::damage without attached buffer");
    }

    // Attempt to flush any state immediately

    flush(surface);
}

WAY_INTERFACE(wl_surface) = {
    .destroy = way::simple_destroy,
    .attach = attach,
    .damage = damage,
    .frame = frame,
    WAY_STUB_QUIET(set_opaque_region),
    .set_input_region = set_input_region,
    .commit = commit,
    .set_buffer_transform = WAY_ADDON_SIMPLE_STATE_REQUEST(way::Surface, buffer_transform, buffer_transform, wl_output_transform(bt), i32 bt),
    .set_buffer_scale     = WAY_ADDON_SIMPLE_STATE_REQUEST(way::Surface, buffer_scale,     buffer_scale,     scale,                   i32 scale),
    .damage_buffer = damage_buffer,
    .offset = offset,
};

// -----------------------------------------------------------------------------

way::Surface::~Surface()
{
    scene::node::unparent(scene.tree.get());
    scene.tree->userdata = nullptr;
    core_assert(std::erase(client->surfaces, this));
}

void way::role_destroy(wl_client* client, wl_resource* resource)
{
    auto* surface = way::get_userdata<way::Surface>(resource);
    surface->role = way::SurfaceRole::none;
    way::simple_destroy(client, resource);
}
