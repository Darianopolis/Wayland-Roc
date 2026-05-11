#include "internal.hpp"

void wm_init_xcursor(WmServer* wm)
{
    wm->xcursor.theme = "breeze_cursors";
    wm->xcursor.size = 24;
    wm->xcursor.sampler = gpu_sampler_create(wm->gpu, {
        .mag = VK_FILTER_NEAREST,
        .min = VK_FILTER_LINEAR,
    });
}

static
void set_visual(WmSeat* seat, SceneNode* visual)
{
    if (seat->pointer.visual == visual) return;

    if (seat->pointer.visual) {
        scene_node_unparent(seat->pointer.visual.get());
    }
    seat->pointer.visual = visual;
    if (visual) {
        scene_tree_place_above(seat->pointer.tree.get(), nullptr, visual);
    }
}

void wm_pointer_set_cursor(WmSeat* seat, WmSurface* surface)
{
    set_visual(seat, surface ? surface->tree.get() : nullptr);
}

static
auto get_xcursor(WmServer* wm, const char* semantic) -> SceneNode*
{
    auto iter = wm->xcursor.cache.find(semantic);
    if (iter != wm->xcursor.cache.end()) {
        return iter->second.get();
    }

    log_debug("Loading XCursor icon \"{}\"", semantic);

    auto* cursor = XcursorLibraryLoadImage(semantic, wm->xcursor.theme.c_str(), wm->xcursor.size);

    if (!cursor) {
        debug_assert("default"sv != semantic);
        log_error("XCursor icon \"{}\" not found, falling back to \"default\"", semantic);
        auto fallback = get_xcursor(wm, "default");
        wm->xcursor.cache.insert({semantic, fallback});
        return fallback;
    }

    defer { XcursorImageDestroy(cursor); };
    auto image = gpu_image_create(wm->gpu, {
        .extent = {cursor->width, cursor->height},
        .format = gpu_format_from_drm(DRM_FORMAT_ABGR8888),
        .usage = GpuImageUsage::texture | GpuImageUsage::transfer
    });
    gpu_copy_memory_to_image(image.get(), as_bytes(cursor->pixels, cursor->width * cursor->height * 4), {{{image->extent()}}});

    auto visual = scene_texture_create();
    scene_texture_set_image(visual.get(), image.get(), wm->xcursor.sampler.get(), GpuBlendMode::premultiplied);
    scene_texture_set_dst(visual.get(), {-vec2f32{cursor->xhot, cursor->yhot}, {cursor->width, cursor->height}, xywh});

    wm->xcursor.cache.insert({semantic, visual});

    return visual.get();
}

void wm_pointer_set_xcursor(WmSeat* seat, const char* semantic)
{
    auto visual = semantic ? get_xcursor(seat->server, semantic) : nullptr;

    set_visual(seat, visual);
}
