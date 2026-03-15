#pragma once

#include "way.hpp"
#include "util.hpp"

#include "core/math.hpp"

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include <wayland/server/xdg-shell.h>
#include <wayland/server/linux-dmabuf-v1.h>
#include <wayland/server/pointer-gestures-unstable-v1.h>
#include <wayland/server/viewporter.h>
#include <wayland/server/relative-pointer-unstable-v1.h>
#include <wayland/server/pointer-constraints-unstable-v1.h>
#include <wayland/server/xdg-decoration-unstable-v1.h>
#include <wayland/server/server-decoration.h>
#include <wayland/server/cursor-shape-v1.h>
#include <wayland/server/linux-drm-syncobj-v1.h>

CORE_UNIX_ERROR_BEHAVIOUR(wl_event_loop_dispatch, negative_one)

namespace way
{
    struct Client;
    struct Surface;
}

// -----------------------------------------------------------------------------

namespace way
{
    struct Keymap
    {
        core::Fd fd;
        u32     size;
    };
}

// -----------------------------------------------------------------------------

namespace way
{
    struct Server
    {
        core::EventLoop* event_loop;

        std::chrono::steady_clock::time_point epoch;

        gpu::Context* gpu;
        scene::Context*  scene;
        scene::SystemId scene_system;

        wl_display* wl_display;
        core::Fd wl_event_loop_fd;
        std::string socket_name;


        core::Ref<gpu::Sampler> sampler;

        struct {
            way::Listener created;
        } client;

        struct {
            scene::Keyboard* scene;
            way::Keymap keymap;
        } keyboard;

        struct {
            scene::Pointer* scene;
        } pointer;

        struct {
            core::Weak<way::Surface> pointer;
            core::Weak<way::Surface> keyboard;
        } focus;

        struct {
            core::Fd format_table;
            usz format_table_size;
            std::vector<u16> tranche_formats;
        } dmabuf;

        ~Server();
    };

    auto get_elapsed(way::Server*) -> std::chrono::steady_clock::duration;
}

// -----------------------------------------------------------------------------

namespace way::dmabuf
{
    void init(way::Server*);
}

// -----------------------------------------------------------------------------

namespace way::seat
{
    void init(way::Server* server);

    void on_keyboard_enter(way::Client*, scene::Event*);
    void on_keyboard_leave(way::Client*, scene::Event*);
    void on_key(           way::Client*, scene::Event*);
    void on_modifier(      way::Client*, scene::Event*);

    void on_pointer_enter(way::Client*, scene::Event*);
    void on_pointer_leave(way::Client*, scene::Event*);
    void on_motion(       way::Client*, scene::Event*);
    void on_button(       way::Client*, scene::Event*);
    void on_scroll(       way::Client*, scene::Event*);
}

// -----------------------------------------------------------------------------

namespace way
{
    struct Region
    {
        way::Resource resource;

        region2f32 region;
    };
}

// -----------------------------------------------------------------------------

namespace way
{
    /**
    * Represents accumulated damage for a surface or buffer.
    *
    * Accurate damage is not required for correctness, as reading from un-damaged parts of a buffer is valid.
    *
    * Taking advantage of this, and the fact that damage is usually localized, we only
    * track the outer bounds of accumulated damage. This avoids relatively expensive region operations,
    * and reduces the number of copies on transfer and clip regions during rendering; while remaining
    * optimal or near optimal for all realistic scenarios.
    */
    struct DamageRegion
    {
        static constexpr aabb2i32 Empty = {{INT_MAX, INT_MAX}, {INT_MIN, INT_MIN}, core::minmax};

    private:
        aabb2i32 region = Empty;

    public:
        void damage(aabb2i32 damage)
        {
            region = core::aabb::outer(region, damage);
        }

        void clip_to(aabb2i32 limit)
        {
            region = core::aabb::inner(region, limit);
        }

        void clear()
        {
            region = Empty;
        }

        explicit operator bool() const
        {
            return region.max.x > region.min.x
                && region.max.y > region.min.y;
        }

        aabb2i32 bounds()
        {
            core_assert(*this);
            return region;
        }
    };
}

// -----------------------------------------------------------------------------

    namespace way
    {
        struct Surface;
        struct Buffer;

// -----------------------------------------------------------------------------

    using CommitId = u32;

    enum class SurfaceRole : u32
    {
        none,
        cursor,
        drag_icon,
        subsurface,
        xdg_toplevel,
        xdg_popup,
    };

// -----------------------------------------------------------------------------

    enum class SurfaceCommittedState : u32
    {
        // wl_surface
        buffer,
        opaque_region,
        input_region,
        buffer_transform,
        buffer_scale,

        // wp_viewport
        buffer_source,
        buffer_destination,

        // wl_subsurface / xdg_toplevel / xdg_popup
        parent,
        parent_commit,

        // xdg_surface
        geometry,
        acked_serial,

        // xdg_toplevel
        title,
        app_id,
        min_size,
        max_size,
    };

    struct SubsurfacePlace
    {
        core::Ref<way::Surface> reference;
        core::Ref<way::Surface> subsurface;
        bool above;
    };

    struct SubsurfaceMove
    {
        core::Ref<way::Surface> subsurface;
        vec2i32 position;
    };

    static constexpr aabb2f32 infinite_aabb = {{-INFINITY, -INFINITY}, {INFINITY, INFINITY}, core::minmax};

    struct Positioner;

    struct SurfaceState
    {
        way::CommitId commit;

        struct {
            u32 set   = 0;
            u32 unset = 0;
        } committed;

        static_assert(sizeof(u32) * CHAR_BIT >
            std::to_underlying(*std::ranges::max_element(magic_enum::enum_values<way::SurfaceCommittedState>())));

        bool is_set(  way::SurfaceCommittedState state) { return committed.set   & (1 << std::to_underlying(state)); }
        bool is_unset(way::SurfaceCommittedState state) { return committed.unset & (1 << std::to_underlying(state)); }

        void set(way::SurfaceCommittedState state)
        {
            committed.set   |=  (1 << std::to_underlying(state));
            committed.unset &= ~(1 << std::to_underlying(state));
        }

        void unset(way::SurfaceCommittedState state)
        {
            committed.unset |=  (1 << std::to_underlying(state));
            committed.set   &= ~(1 << std::to_underlying(state));
        }

        struct {
            way::CommitId commit;
        } parent;

        struct {
            way::ResourceList frame_callbacks;
            vec2i32 offset;
            region2f32 opaque_region;
            region2f32 input_region;
            way::DamageRegion damage;
        } surface;


        core::Ref<way::Buffer>     buffer;
        core::Ref<gpu::Image>      image;
        wl_output_transform buffer_transform;
        i32                 buffer_scale = 1;
        rect2f32            buffer_source;
        vec2i32             buffer_destination;
        way::DamageRegion   buffer_damage;

        struct {
            rect2i32 geometry;
            u32 acked_serial;
        } xdg;

        struct {
            std::vector<way::SubsurfacePlace> places;
            std::vector<way::SubsurfaceMove>  moves;
        } subsurface;

        struct {
            vec2i32 min_size;
            vec2i32 max_size;
            std::string title;
            std::string app_id;
        } toplevel;

        ~SurfaceState();
    };

    struct Surface
    {
        way::Client* client;

        core::Weak<way::Surface> parent;

        // core
        way::Resource wl_surface;
        way::SurfaceRole role = way::SurfaceRole::none;

        // state tracking
        way::CommitId last_commit_id;
        way::SurfaceState* pending;
        std::deque<way::SurfaceState> cached;
        way::SurfaceState current;

        // wl_subsurface
        struct {
            way::Resource resource;
            bool synchronized;
        } subsurface;

        // xdg_surface
        way::Resource xdg_surface;
        u32 sent_serial;
        u32 acked_serial;

        // xdg_popup
        struct {
            way::Resource resource;
            vec2f32      position;
        } popup;

        // xdg_toplevel
        struct {
            way::Resource resource;
            rect2f32 anchor;
            vec2f32 gravity = {1, 1};
            core::Ref<scene::Window> window;

            bool pending; // commit response to resize configure is pending
            bool queued;  // new reposition request received while pending
        } toplevel;

        // scene
        struct {
            core::Ref<scene::Tree>         tree;
            core::Ref<scene::Texture>      texture;
            core::Ref<scene::InputRegion> input_region;
        } scene;

        bool mapped;

        ~Surface();
    };

    void role_destroy(wl_client*, wl_resource*);
}

namespace way::surface
{
    void on_redraw(way::Surface*);
}

namespace way::viewport
{
    void apply(way::Surface*, way::SurfaceState& from);
}

// -----------------------------------------------------------------------------

namespace way::subsurface
{
    void commit(way::Surface*, way::SurfaceState&);
    void apply( way::Surface*, way::SurfaceState&);
}

// -----------------------------------------------------------------------------

namespace way::xdg_surface
{
    void apply(way::Surface*, way::SurfaceState&);
    void configure(way::Surface*);
}

// -----------------------------------------------------------------------------

namespace way::toplevel
{
    void apply(        way::Surface*, way::SurfaceState&);
    void on_map_change(way::Surface*, bool mapped);
    void on_reposition(way::Surface*, rect2f32 frame, vec2f32 gravity);
}

// -----------------------------------------------------------------------------

namespace way::positioner
{
    void create(wl_client*, wl_resource*, u32 id);
}

namespace way
{
    void get_popup(wl_client*, wl_resource*, u32 id,
                   wl_resource* parent, wl_resource* positioner);
}

namespace way::popup
{
    void apply(way::Surface*, way::SurfaceState&);
}

// -----------------------------------------------------------------------------

namespace way::output
{
    void init(way::Server*);
}

// -----------------------------------------------------------------------------

namespace way
{
    struct DataSource
    {
        way::Client* client;

        way::Resource resource;

        core::Ref<scene::DataSource> source;
    };

    struct DataOffer
    {
        way::Client* client;

        way::Resource resource;

        core::Ref<scene::DataSource> source;
    };

    namespace data_offer
    {
        void selection(way::Client*);
    }
}

// -----------------------------------------------------------------------------

namespace way
{
    struct Buffer
    {
        vec2u32 extent;

        // Sent on apply, should return a gpu::Image when the buffer is ready to display
        [[nodiscard]] virtual auto acquire(way::Surface*, way::SurfaceState& from) -> core::Ref<gpu::Image> = 0;

    protected:
        ~Buffer() = default;
    };
}

// -----------------------------------------------------------------------------

namespace way
{
    struct ShmPool;

    struct ShmPool
    {
        way::Server* server;

        way::Resource resource;

        core::Fd fd;
        void*   data;
        usz     size;

        ~ShmPool();
    };
}

// -----------------------------------------------------------------------------

namespace way
{
    struct Client
    {
        way::Server* server;

        wl_client* wl_client;

        core::Ref<scene::Client> scene;

        std::vector<way::Surface*> surfaces;

        way::ResourceList keyboards;
        way::ResourceList pointers;
        way::ResourceList data_devices;

        struct {
            core::Ref<way::DataOffer> offer;
        } drag;
    };

    void on_client_create(wl_listener*, void* data);

    namespace client
    {
        way::Client* from(way::Server*, const wl_client*);

        auto is_behind(way::Client*) -> bool;
    }
}

// -----------------------------------------------------------------------------

#define WAY_ADDON_SIMPLE_STATE_REQUEST(Type, Field, Name, Expr, ...) \
    [](wl_client* client, wl_resource* resource, __VA_ARGS__) { \
        auto* surface = way::get_userdata<way::Surface>(resource); \
        surface->pending->Field = Expr; \
        surface->pending->set(way::SurfaceCommittedState::Name); \
    }

/**
 * Convenience macro for applying trivial state elements.
 */
#define WAY_ADDON_SIMPLE_STATE_APPLY(From, To, Field, Name) \
    do { \
        if ((From).is_set(way::SurfaceCommittedState::Name)) { \
            (To).Field = std::move((From).Field); \
        } \
    } while (false)

// -----------------------------------------------------------------------------

namespace way
{
    u32 next_serial(way::Server* server);

    void queue_client_flush(way::Server* server);

    void send(way::Server* server, const char* fn_name, auto fn, auto&& resource, auto&&... args)
    {
        if (resource) {
            fn(resource, args...);
            way::queue_client_flush(server);
        } else {
            log_error("Failed to dispatch {}, resource is null", fn_name);
        }
    }
}

#define way_send(Server, Fn, Resource, ...) \
    way::send(Server, #Fn, Fn, Resource __VA_OPT__(,) __VA_ARGS__)

namespace way
{
    template<typename ...Args>
    void post_error(way::Server* server, wl_resource* resource, u32 code, std::format_string<Args...> fmt, Args&&... args)
    {
        if (!resource) return;
        auto message = std::vformat(fmt.get(), std::make_format_args(args...));
        log_error("{}", message);
        wl_resource_post_error(resource, code, "%s", message.c_str());
        way::queue_client_flush(server);
    }
}

// -----------------------------------------------------------------------------

namespace way
{
    void simple_destroy(wl_client* client, wl_resource* resource);
}

// -----------------------------------------------------------------------------

namespace way
{
    wl_global* global(way::Server*, const wl_interface*, i32 version, wl_global_bind_func_t, void* data = nullptr);
}

#define way_global(Server, Interface, ...) \
    way::global(Server, &Interface##_interface, way_##Interface##_version, way_##Interface##_bind_global __VA_OPT__(,) __VA_ARGS__)

// -----------------------------------------------------------------------------

#define WAY_STUB(Name) \
    .Name = [](wl_client*, wl_resource* resource, auto...) { \
        log_error("TODO - {}{{{}}}::" #Name, wl_resource_get_interface(resource)->name, (void*)resource); \
    }
#define WAY_STUB_QUIET(Name) \
    .Name = [](auto...) {}

#define WAY_INTERFACE(Name) \
    const struct Name##_interface way_##Name##_impl

namespace way
{
    struct BindGlobalData
    {
        wl_client* client;
        way::Server* server;
        u32 version;
        u32 id;
    };
}

#define WAY_BIND_GLOBAL(Name, Data) \
    static void way_##Name##_bind_global_impl(const way::BindGlobalData& Data); \
           void way_##Name##_bind_global(wl_client* client, void* data, u32 version, u32 id) \
    { \
        way_##Name##_bind_global_impl({client, way::get_userdata<way::Server>(data), version, id}); \
    } \
    static void way_##Name##_bind_global_impl(const way::BindGlobalData& Data)

#define WAY_INTERFACE_DECLARE(Name, ...) \
    extern WAY_INTERFACE(Name) \
    __VA_OPT__(; \
        static_assert(std::same_as<decltype(__VA_ARGS__), int>); \
        constexpr u32 way_##Name##_version = __VA_ARGS__; \
        void way_##Name##_bind_global(wl_client* client, void* data, u32 version, u32 id) \
    )

// -----------------------------------------------------------------------------

namespace way
{
    wl_resource* resource_create(wl_client*, const wl_interface*, int version, int id, const void* impl, void*, bool refcount);

    inline
    wl_resource* resource_create(wl_client* client, const wl_interface* interface, wl_resource* parent, int id, const void* impl, void* object, bool refcount)
    {
        return way::resource_create(client, interface, wl_resource_get_version(parent), id, impl, object, refcount);
    }
}

#define way_resource_create_unsafe(Name, Client, Version, IdOrResource, Object) \
    way::resource_create(Client, &Name##_interface, Version, IdOrResource, &way_##Name##_impl, Object, false)

#define way_resource_create_refcounted(Name, Client, Version, IdOrResource, Object) \
    way::resource_create(Client, &Name##_interface, Version, IdOrResource, &way_##Name##_impl, Object, true)

// -----------------------------------------------------------------------------

WAY_INTERFACE_DECLARE(wl_compositor, 6);
WAY_INTERFACE_DECLARE(wl_region);
WAY_INTERFACE_DECLARE(wl_surface);

WAY_INTERFACE_DECLARE(wl_subcompositor, 1);
WAY_INTERFACE_DECLARE(wl_subsurface);

WAY_INTERFACE_DECLARE(wl_output, 4);

WAY_INTERFACE_DECLARE(wl_data_device_manager, 3);
WAY_INTERFACE_DECLARE(wl_data_offer);
WAY_INTERFACE_DECLARE(wl_data_source);
WAY_INTERFACE_DECLARE(wl_data_device);

WAY_INTERFACE_DECLARE(zwp_pointer_gestures_v1, 3);
WAY_INTERFACE_DECLARE(zwp_pointer_gesture_swipe_v1);
WAY_INTERFACE_DECLARE(zwp_pointer_gesture_pinch_v1);
WAY_INTERFACE_DECLARE(zwp_pointer_gesture_hold_v1);

WAY_INTERFACE_DECLARE(wp_cursor_shape_manager_v1, 2);
WAY_INTERFACE_DECLARE(wp_cursor_shape_device_v1);

WAY_INTERFACE_DECLARE(wp_viewporter, 1);
WAY_INTERFACE_DECLARE(wp_viewport);

WAY_INTERFACE_DECLARE(xdg_wm_base, 7);
WAY_INTERFACE_DECLARE(xdg_surface);
WAY_INTERFACE_DECLARE(xdg_toplevel);
WAY_INTERFACE_DECLARE(xdg_positioner);
WAY_INTERFACE_DECLARE(xdg_popup);

WAY_INTERFACE_DECLARE(wl_buffer);

WAY_INTERFACE_DECLARE(wl_shm, 2);
WAY_INTERFACE_DECLARE(wl_shm_pool);

WAY_INTERFACE_DECLARE(wl_seat, 9);
WAY_INTERFACE_DECLARE(wl_keyboard);
WAY_INTERFACE_DECLARE(wl_pointer);

WAY_INTERFACE_DECLARE(zwp_linux_dmabuf_v1, 5);
WAY_INTERFACE_DECLARE(zwp_linux_buffer_params_v1);
WAY_INTERFACE_DECLARE(zwp_linux_dmabuf_feedback_v1);
