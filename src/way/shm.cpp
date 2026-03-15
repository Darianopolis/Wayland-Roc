#include "internal.hpp"

static
void pool_unmap(way::ShmPool* pool)
{
    if (pool->data) {
        core::check<munmap>(pool->data, pool->size);
        pool->data = nullptr;
        pool->size = 0;
    }
}

static
void pool_map(way::ShmPool* pool, usz size)
{
    pool_unmap(pool);

    auto res = core::check<mmap>(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, pool->fd.get(), 0);
    if (res.ok()) {
        pool->data = res.value;
        pool->size = size;
    } else {
        way::post_error(pool->server, pool->resource, WL_SHM_ERROR_INVALID_FD, "mmap failed: {}", strerror(res.error));
    }
}

static
void create_pool(wl_client* client, wl_resource* resource, u32 id, int fd, i32 size)
{
    auto pool = core::create<way::ShmPool>();
    pool->server = way::get_userdata<way::Server>(resource);
    pool->fd = core::fd::adopt(fd);
    pool->resource = way_resource_create_refcounted(wl_shm_pool, client, resource, id, pool.get());
    pool_map(pool.get(), size);
}

WAY_INTERFACE(wl_shm) = {
    .create_pool = create_pool,
    .release = way::simple_destroy,
};

WAY_BIND_GLOBAL(wl_shm, bind)
{
    auto resource = way_resource_create_unsafe(wl_shm, bind.client, bind.version, bind.id, bind.server);

    way_send(bind.server, wl_shm_send_format, resource, WL_SHM_FORMAT_ARGB8888);
    way_send(bind.server, wl_shm_send_format, resource, WL_SHM_FORMAT_XRGB8888);
}

// -----------------------------------------------------------------------------

inline
wl_shm_format from_drm(gpu::DrmFormat drm)
{
    switch (drm) {
        break;case DRM_FORMAT_XRGB8888: return WL_SHM_FORMAT_XRGB8888;
        break;case DRM_FORMAT_ARGB8888: return WL_SHM_FORMAT_ARGB8888;
        break;default:                  return wl_shm_format(drm);
    }
}

inline
gpu::DrmFormat to_drm(wl_shm_format shm)
{
    switch (shm) {
        break;case WL_SHM_FORMAT_XRGB8888: return DRM_FORMAT_XRGB8888;
        break;case WL_SHM_FORMAT_ARGB8888: return DRM_FORMAT_ARGB8888;
        break;default:                     return gpu::DrmFormat(shm);
    }
}

// -----------------------------------------------------------------------------

namespace way
{
    struct ShmBuffer : way::Buffer
    {
        core::Ref<way::ShmPool> pool;

        way::Resource resource;

        i32 offset;
        i32 stride;
        gpu::Format format;

        virtual auto acquire(way::Surface*, way::SurfaceState& from) -> core::Ref<gpu::Image> final override;
    };
}

static
void create_buffer(wl_client* client, wl_resource* resource, u32 id, i32 offset, i32 width, i32 height, i32 stride, u32 _format)
{
    auto* pool = way::get_userdata<way::ShmPool>(resource);
    auto* server = pool->server;

    auto buffer = core::create<way::ShmBuffer>();
    buffer->resource = way_resource_create_refcounted(wl_buffer, client, resource, id, buffer.get());

    buffer->format = gpu::format::from_drm(to_drm(wl_shm_format(_format)));
    buffer->extent = {width, height};
    buffer->pool = pool;
    buffer->stride = stride;
    buffer->offset = offset;

    if (!buffer->format) {
        way::post_error(server, resource, WL_SHM_ERROR_INVALID_FORMAT, "Format {} is not supported", core::to_string(wl_shm_format(_format)));
        return;
    }
}

static
void pool_resize(wl_client* client, wl_resource* resource, i32 size)
{
    auto* pool = way::get_userdata<way::ShmPool>(resource);
    pool_map(pool, size);
}

WAY_INTERFACE(wl_shm_pool) = {
    .create_buffer = create_buffer,
    .destroy = way::simple_destroy,
    .resize = pool_resize,
};

way::ShmPool::~ShmPool()
{
    pool_unmap(this);
}

// -----------------------------------------------------------------------------

#define NOISY_SHM_BUFFER_IMAGES 1

static
auto try_steal(way::ShmBuffer* buffer, way::Surface* surface) -> gpu::Image*
{
    // If the last attached buffer is a wl_shm buffer...
    auto* shm_buffer = dynamic_cast<way::ShmBuffer*>(surface->current.buffer.get());
    if (!shm_buffer) return nullptr;

    // ...try to steal the previously acquired image...
    auto* candidate = surface->current.image.get();

    // ...if compatible with the newly attached buffer
    if (candidate->extent() != buffer->extent) return nullptr;
    if (candidate->format() != buffer->format) return nullptr;

#if NOISY_SHM_BUFFER_IMAGES
    if (shm_buffer == buffer) log_info( "REUSING shm buffer image {}",  core::to_string(candidate->extent()));
    else                      log_debug("STEALING shm buffer image {}", core::to_string(candidate->extent()));
#endif

    return candidate;
}

auto way::ShmBuffer::acquire(way::Surface* surface, way::SurfaceState& packet) -> core::Ref<gpu::Image>
{
    core::Ref image = try_steal(this, surface);

    auto damage = packet.buffer_damage;

    auto* server = pool->server;

    if (!image) {
        image = gpu::image::create(server->gpu, {
            .extent = extent,
            .format = format,
            .usage = gpu::ImageUsage::transfer_dst | gpu::ImageUsage::texture,
        });

        damage.damage({{}, extent, core::minmax});

#if NOISY_SHM_BUFFER_IMAGES
        log_warn("ALLOCATING shm buffer image {}", core::to_string(extent));
#endif
    }

    damage.clip_to({{}, extent, core::minmax});

    if (damage) {
        aabb2i32 aabb = damage.bounds();
        rect2i32 rect = aabb;
#if NOISY_SHM_BUFFER_IMAGES
        log_trace("  damage {}", core::to_string(rect));
#endif

        auto* gpu = server->gpu;

        auto queue = gpu::queue::get(gpu, gpu::QueueType::graphics);
        auto commands = gpu::commands::begin(queue);

        auto& info = format->info;
        auto min_offset = gpu::image::compute_linear_offset(format, aabb.min,     stride);
        auto max_offset = gpu::image::compute_linear_offset(format, aabb.max - 1, stride) + info.texel_block_size;

        auto size = max_offset - min_offset;
        auto staging = gpu::buffer::create(gpu, size, gpu::BufferFlag::host);
        core_assert((offset + max_offset) <= pool->size, "accessed {} > available {}", offset + max_offset, pool->size);
        std::memcpy(staging->host_address, core::byte_offset_pointer<void>(pool->data, offset + min_offset), size);

        gpu::commands::copy_buffer_to_image(commands.get(), image.get(), staging.get(),
            {{
                .image_extent = rect.extent,
                .image_offset = rect.origin,
                .buffer_row_length = u32(stride) / info.texel_block_size,
            }});

        gpu::submit(commands.get(), {});
    }
#if NOISY_SHM_BUFFER_IMAGES
    else {
        log_warn("  damage was empty");
    }
#endif

    way_send(server, wl_buffer_send_release, resource);

    return image;
}
