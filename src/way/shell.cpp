#include "internal.hpp"

static
void get_xdg_surface(wl_client* client, wl_resource* resource, u32 id, wl_resource* wl_surface)
{
    auto* surface = way::get_userdata<way::Surface>(wl_surface);
    surface->xdg_surface = way_resource_create_refcounted(xdg_surface, client, resource, id, surface);
}

WAY_INTERFACE(xdg_wm_base) = {
    .destroy = way::simple_destroy,
    .create_positioner = way::positioner::create,
    .get_xdg_surface = get_xdg_surface,
    WAY_STUB(pong),
};

WAY_BIND_GLOBAL(xdg_wm_base, bind)
{
    way_resource_create_unsafe(xdg_wm_base, bind.client, bind.version, bind.id, bind.server);
}

// -----------------------------------------------------------------------------

static
void get_toplevel(wl_client* client, wl_resource* resource, u32 id)
{
    auto* surface = way::get_userdata<way::Surface>(resource);
    surface->role = way::SurfaceRole::xdg_toplevel;
    surface->toplevel.resource = way_resource_create_refcounted(xdg_toplevel, client, resource, id, surface);

    surface->toplevel.window = scene::window::create(surface->client->scene.get());

    scene::tree::place_above(scene::window::get_tree(surface->toplevel.window.get()), nullptr, surface->scene.tree.get());
}

static
void ack_configure(wl_client* client, wl_resource* resource, u32 serial)
{
    auto* surface = way::get_userdata<way::Surface>(resource);
    auto* server = surface->client->server;

    if (serial > surface->sent_serial) {
        way::post_error(server, surface->xdg_surface, XDG_SURFACE_ERROR_INVALID_SERIAL,
            "Client acked configure {} which is higher than latest sent configure serial {}",
            serial, surface->sent_serial);
        return;
    }

    if (serial <= surface->acked_serial) {
        log_warn("Client acked old configure serial");
        return;
    }

    surface->pending->xdg.acked_serial = serial;
    surface->pending->set(way::SurfaceCommittedState::acked_serial);

    surface->acked_serial = serial;
}

WAY_INTERFACE(xdg_surface) = {
    .destroy = way::role_destroy,
    .get_toplevel = get_toplevel,
    .get_popup = way::get_popup,
    .set_window_geometry = WAY_ADDON_SIMPLE_STATE_REQUEST(way_xdg_surface, xdg.geometry, geometry, rect2i32({x, y}, {w, h}, core::xywh), i32 x, i32 y, i32 w, i32 h),
    .ack_configure = ack_configure,
};

void way::xdg_surface::apply(way::Surface* surface, way::SurfaceState& from)
{
    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, xdg.geometry,     geometry);
    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, xdg.acked_serial, acked_serial);

    if (!surface->current.is_set(way::SurfaceCommittedState::geometry) && surface->mapped) {
        surface->current.xdg.geometry = { {}, surface->current.buffer->extent, core::xywh };
    }
}

void way::xdg_surface::configure(way::Surface* surface)
{
    auto* server = surface->client->server;
    surface->sent_serial = way::next_serial(server);
    way_send(server, xdg_surface_send_configure, surface->xdg_surface, surface->sent_serial);
}

// -----------------------------------------------------------------------------

void way::toplevel::on_map_change(way::Surface* surface, bool mapped)
{
    if (mapped) {
        scene::window::map(surface->toplevel.window.get());
    } else {
        scene::window::unmap(surface->toplevel.window.get());
    }
}

static
void configure_toplevel(way::Surface* surface, vec2u32 extent)
{
    way_send(surface->client->server, xdg_toplevel_send_configure, surface->toplevel.resource,
        extent.x, extent.y,
        core::ptr_to(way::to_wl_array<const xdg_toplevel_state>({
            XDG_TOPLEVEL_STATE_ACTIVATED,
        }))
    );
}

void way::toplevel::on_reposition(way::Surface* surface, rect2f32 frame, vec2f32 gravity)
{
    if (surface->toplevel.anchor.extent == frame.extent) {
        // Move
        scene::window::set_frame(surface->toplevel.window.get(), {
            frame.origin,
            scene::window::get_frame(surface->toplevel.window.get()).extent,
            core::xywh
        });
    } else {
        // Resize
        if (surface->toplevel.pending) {
            surface->toplevel.queued = true;
        } else {
            configure_toplevel(surface, frame.extent);
            way::xdg_surface::configure(surface);
            surface->toplevel.pending = true;
        }
    }
    surface->toplevel.anchor = frame;
    surface->toplevel.gravity = gravity;
}

static
void send_premap_configure(way::Surface* surface)
{
    auto* server = surface->client->server;

    if (wl_resource_get_version(surface->toplevel.resource) >= XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION) {
        way_send(server, xdg_toplevel_send_wm_capabilities, surface->toplevel.resource, core::ptr_to(way::to_wl_array<const xdg_toplevel_wm_capabilities>({
            XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN,
        })));
    }

    configure_toplevel(surface, {0, 0});
    way::xdg_surface::configure(surface);
}

void way::toplevel::apply(way::Surface* surface, way::SurfaceState& from)
{
    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, toplevel.title, title);
    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, toplevel.app_id, app_id);
    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, toplevel.max_size, max_size);
    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, toplevel.min_size, min_size);

    if (surface->mapped) {
        vec2f32 extent = surface->current.xdg.geometry.extent;
        rect2f32 anchor = surface->toplevel.anchor;

        rect2f32 frame { anchor.origin, extent, core::xywh };

        // Apply gravity
        vec2f32 rel = 1.f - ((surface->toplevel.gravity + 1.f) * .5f);
        frame.origin -= rel * (extent - anchor.extent);

        scene::window::set_frame(surface->toplevel.window.get(), frame);
        scene::tree::set_translation(surface->scene.tree.get(), -surface->current.xdg.geometry.origin);

        surface->toplevel.pending = false;
        if (surface->toplevel.queued) {
            configure_toplevel(surface, anchor.extent);
            way::xdg_surface::configure(surface);
            surface->toplevel.queued = false;
            surface->toplevel.pending = true;
        }
    } else {
        log_info("toplevel surface committed but not mapped, sending configure");
        send_premap_configure(surface);
    }
}

// -----------------------------------------------------------------------------

WAY_INTERFACE(xdg_toplevel) = {
    .destroy = way::role_destroy,
    WAY_STUB(set_parent),
    .set_title  = WAY_ADDON_SIMPLE_STATE_REQUEST(way_toplevel, toplevel.title,  title,  title,  const char* title),
    .set_app_id = WAY_ADDON_SIMPLE_STATE_REQUEST(way_toplevel, toplevel.app_id, app_id, app_id, const char* app_id),
    WAY_STUB(show_window_menu),
    WAY_STUB(move),
    WAY_STUB(resize),
    .set_max_size = WAY_ADDON_SIMPLE_STATE_REQUEST(way_toplevel, toplevel.max_size, max_size, vec2i32(w, h), i32 w, i32 h),
    .set_min_size = WAY_ADDON_SIMPLE_STATE_REQUEST(way_toplevel, toplevel.min_size, min_size, vec2i32(w, h), i32 w, i32 h),
    WAY_STUB(set_maximized),
    WAY_STUB(unset_maximized),
    WAY_STUB(set_fullscreen),
    WAY_STUB(unset_fullscreen),
    WAY_STUB(set_minimized),
};
