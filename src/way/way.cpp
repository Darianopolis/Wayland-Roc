#include "internal.hpp"

way::Server::~Server()
{
    wl_event_loop_fd = nullptr;
    wl_display_terminate(wl_display);
    wl_display_destroy(wl_display);
}

auto way::create(core::EventLoop* event_loop, gpu::Context* gpu, scene::Context* scene) -> core::Ref<way::Server>
{
    auto server = core::create<way::Server>();

    server->epoch = std::chrono::steady_clock::now();

    server->event_loop = event_loop;
    server->gpu = gpu;
    server->scene = scene;
    server->scene_system = scene::register_system(scene);

    way::seat::init(server.get());

    server->wl_display = wl_display_create();

    wl_display_set_default_max_buffer_size(server->wl_display, 4096);

    server->socket_name = wl_display_add_socket_auto(server->wl_display);

    server->wl_event_loop_fd = core::fd::reference(wl_event_loop_get_fd(wl_display_get_event_loop(server->wl_display)));
    core::fd::add_listener(server->wl_event_loop_fd.get(), event_loop, core::FdEventBit::readable,
        [server = server.get()](int fd, core::Flags<core::FdEventBit> events) {
            core::check<wl_event_loop_dispatch>(wl_display_get_event_loop(server->wl_display), 0);
            wl_display_flush_clients(server->wl_display);
        });

    way_global(server.get(), wl_shm);
    way_global(server.get(), wl_compositor);
    way_global(server.get(), wl_subcompositor);
    way_global(server.get(), xdg_wm_base);
    way_global(server.get(), wl_seat);
    way_global(server.get(), wl_data_device_manager);
    way_global(server.get(), wp_viewporter);
    way_global(server.get(), zwp_pointer_gestures_v1);
    way_global(server.get(), wp_cursor_shape_manager_v1);
    way::output::init(server.get());
    way::dmabuf::init(server.get());

    server->sampler = gpu::sampler::create(gpu, {
        .mag = VK_FILTER_NEAREST,
        .min = VK_FILTER_LINEAR,
    });

    server->client.created.data = server.get();
    server->client.created.listener.notify = way::on_client_create;
    wl_display_add_client_created_listener(server->wl_display, &server->client.created.listener);

    return server;
}

auto way::get_elapsed(way::Server* server) -> std::chrono::steady_clock::duration
{
    return std::chrono::steady_clock::now() - server->epoch;
}

wl_global* way::global(way::Server* server, const wl_interface* interface, i32 version, wl_global_bind_func_t bind, void* data)
{
    core_assert(version <= interface->version);
    return wl_global_create(server->wl_display, interface, version, data ?: server, bind);
}

wl_resource* way::resource_create(wl_client* client, const wl_interface* interface, int version, int id, const void* impl, void* data, bool refcount)
{
    auto resource = wl_resource_create(client, interface, version, id);
    if (refcount) {
        core::add_ref(data);
        wl_resource_set_implementation(resource, impl, data, [](wl_resource* resource) {
            core::remove_ref(wl_resource_get_user_data(resource));
        });
    } else {
        wl_resource_set_implementation(resource, impl, data, nullptr);
    }
    return resource;
}

u32 way::next_serial(way::Server* server)
{
    return wl_display_next_serial(server->wl_display);
}

void way::queue_client_flush(way::Server* server)
{
    // TODO: Queue to run at end of event
    wl_display_flush_clients(server->wl_display);
}

void way::simple_destroy(wl_client* client, wl_resource* resource)
{
    wl_resource_destroy(resource);
}
