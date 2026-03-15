#include "internal.hpp"

struct scene_cursor_manager
{
    const char* theme = "breeze_cursors";
    i32         size = 24;

    core::Map<std::string_view, core::Ref<scene::Node>> cache;
};

void scene_cursor_manager_init(scene::Context* ctx)
{
    ctx->cursor_manager = core::create<scene_cursor_manager>();
}

static
auto get_visual(scene::Pointer* pointer) -> scene::Node*
{
    core_assert(pointer->tree->children.size() <= 1);

    return pointer->tree->children.empty()
        ? nullptr
        : pointer->tree->children.front();
}

static
void set_visual(scene::Pointer* pointer, scene::Node* visual)
{
    core_assert(get_visual(pointer) != visual);

    while (!pointer->tree->children.empty()) {
        scene::node::unparent(*pointer->tree->children.begin());
    }

    if (visual) {
        scene::tree::place_above(pointer->tree.get(), nullptr, visual);
    }
}

void scene::pointer::set_cursor(scene::Pointer* pointer, scene::Node* visual)
{
    if (visual != get_visual(pointer)) {
        log_trace("scene.pointer.set_cursor({})", visual ? std::format("{}", (void*)visual) : "nullptr");
        set_visual(pointer, visual);
    }
}

static
auto get_xcursor(scene::Context* ctx, const char* semantic) -> scene::Node*
{
    auto* manager = ctx->cursor_manager.get();

    auto iter = manager->cache.find(semantic);
    if (iter != manager->cache.end()) {
        return iter->second.get();
    }

    log_debug("Loading XCursor icon \"{}\"", semantic);

    auto* cursor = XcursorLibraryLoadImage(semantic, manager->theme, manager->size);

    if (!cursor) {
        core_assert("default"sv != semantic);
        log_error("XCursor icon \"{}\" not found, falling back to \"default\"", semantic);
        auto fallback = get_xcursor(ctx, "default");
        manager->cache.insert({semantic, fallback});
        return fallback;
    }

    defer { XcursorImageDestroy(cursor); };
    auto image = gpu::image::create(ctx->gpu, {
        .extent = {cursor->width, cursor->height},
        .format = gpu::format::from_drm(DRM_FORMAT_ABGR8888),
        .usage = gpu::ImageUsage::texture | gpu::ImageUsage::transfer
    });
    gpu::copy_memory_to_image(image.get(), {cursor->pixels, cursor->width * cursor->height * 4}, {{image->extent()}});

    auto visual = scene::texture::create(ctx);
    scene::texture::set_image(visual.get(), image.get(), ctx->render.sampler.get(), gpu::BlendMode::premultiplied);
    scene::texture::set_dst(visual.get(), {-vec2f32{cursor->xhot, cursor->yhot}, {cursor->width, cursor->height}, core::xywh});

    manager->cache.insert({semantic, visual});

    return visual.get();
}

void scene::pointer::set_xcursor(scene::Pointer* pointer, const char* semantic)
{
    auto visual = semantic ? get_xcursor(pointer->ctx, semantic) : nullptr;

    if (visual != get_visual(pointer)) {
        log_trace("scene.pointer.set_xcursor({})", semantic ?: "nullptr");
        set_visual(pointer, visual);
    }
}
