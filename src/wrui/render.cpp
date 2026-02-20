#include "internal.hpp"

#include "render.h"
#include "wrui_render_shader.hpp"

#include "wren/internal.hpp"

void wrui_render_init(wrui_context* ctx)
{
    ctx->render.usage = wren_image_usage::render | wren_image_usage::storage;

    ctx->render.compute = wren_pipeline_create_compute(ctx->wren, wrui_render_shader, "compute");

    ctx->render.postmult = wren_pipeline_create_graphics(
        ctx->wren, wren_blend_mode::postmultiplied,
        wren_format_from_drm(DRM_FORMAT_ABGR8888),
        wrui_render_shader, "vertex", "fragment");

    ctx->render.premult = wren_pipeline_create_graphics(
        ctx->wren, wren_blend_mode::premultiplied,
        wren_format_from_drm(DRM_FORMAT_ABGR8888),
        wrui_render_shader, "vertex", "fragment");

    ctx->render.white = wren_image_create(ctx->wren, {1, 1},
        wren_format_from_drm(DRM_FORMAT_ABGR8888),
        wren_image_usage::texture | wren_image_usage::transfer_dst);
    wren_image_update_immed(ctx->render.white.get(), wrei_ptr_to(vec4u8{255, 255, 255, 255}));

    ctx->render.sampler = wren_sampler_create(ctx->wren, VK_FILTER_NEAREST, VK_FILTER_LINEAR);
}

static
void raster(wrui_context* ctx, wren_image* target, wren_commands* commands)
{
    auto  cmd = commands->buffer;
    auto* wren = ctx->wren;
    auto& render = ctx->render;

    struct wrui_draw
    {
        u32 first_vertex;
        u32 first_index;
        u32 num_indices;
        vec2f32 clip;
        wren_image* image;
        wren_sampler* sampler;
        wren_blend_mode blend;
        wrui_transform_state transform;
    };

    std::vector<wrui_vertex> vertices;
    std::vector<u32> indices;
    std::vector<wrui_draw> draws;

    auto walk_node = [&](this auto&& walk_node, wrui_node* node) -> void
    {
        switch (node->type) {
            break;case wrui_node_type::transform:
                wrei_assert_fail("", "Unexpected transform node in layer stack");
            break;case wrui_node_type::tree: {
                for (auto& child : static_cast<wrui_tree*>(node)->children) {
                    walk_node(child.get());
                }
            }
            break;case wrui_node_type::mesh: {
                auto* mesh = static_cast<wrui_mesh*>(node);

                u32 vtx = vertices.size();
                u32 idx = indices.size();
                vertices.insert_range(vertices.end(), mesh->vertices);
                indices.insert_range(indices.end(), mesh->indices);

                draws.emplace_back(wrui_draw {
                    .first_vertex = vtx,
                    .first_index = idx,
                    .num_indices = u32(mesh->indices.size()),
                    .image = mesh->image.get() ?: render.white.get(),
                    .sampler = mesh->sampler.get() ?: render.sampler.get(),
                    .blend = mesh->blend,
                    .transform = mesh->transform->global,
                });
            }
            break;case wrui_node_type::texture: {
                auto* texture = static_cast<wrui_texture*>(node);
                aabb2f32 src = texture->src;
                aabb2f32 dst = texture->dst;

                //  0 ---- 1
                //  | a /  |  a = 0,2,1
                //  |  / b |  b = 1,2,3
                //  2 ---- 3

                u32 vtx = vertices.size();
                u32 idx = indices.size();
                auto push_vtx = [&](vec2f32 pos, vec2f32 uv = {}) {
                    vertices.emplace_back(wrui_vertex { .pos = pos, .uv = uv, .color = texture->tint });
                };
                push_vtx({dst.min.x, dst.min.y}, {src.min.x, src.min.y});
                push_vtx({dst.max.x, dst.min.y}, {src.max.x, src.min.y});
                push_vtx({dst.min.x, dst.max.y}, {src.min.x, src.max.y});
                push_vtx({dst.max.x, dst.max.y}, {src.max.x, src.max.y});
                indices.append_range(std::array{0, 2, 1, 1, 2, 3});

                draws.emplace_back(wrui_draw {
                    .first_vertex = vtx,
                    .first_index = idx,
                    .num_indices = 6,
                    .image = texture->image.get() ?: render.white.get(),
                    .sampler = texture->sampler.get() ?: render.sampler.get(),
                    .blend = texture->blend,
                    .transform = texture->transform->global,
                });
            }
        }
    };

    walk_node(ctx->scene.get());

    auto make_gpu = [&]<typename T>(std::span<T> data) {
        wren_array<T> arr{wren_buffer_create(wren, data.size_bytes(), {}), 0};
        std::memcpy(arr.host(), data.data(), data.size_bytes());
        return arr;
    };

    auto gpu_vertices = make_gpu(std::span(vertices));
    auto gpu_indices  = make_gpu(std::span(indices));
    wren_commands_protect_object(commands, gpu_vertices.buffer.get());
    wren_commands_protect_object(commands, gpu_indices.buffer.get());

    // Protect images

    wren_commands_protect_object(commands, render.white.get());
    for (auto& draw : draws) {
        wren_commands_protect_object(commands, draw.image);
    }

    // Record

    VkExtent2D vk_extent = { target->extent.x, target->extent.y };
    vec2f32 target_extent = target->extent;

    wren->vk.CmdBeginRendering(cmd, wrei_ptr_to(VkRenderingInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { {}, vk_extent },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = wrei_ptr_to(VkRenderingAttachmentInfo {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = target->view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color{.float32{0.f, 0.f, 0.f, 1.f}}},
        }),
    }));
    wren->vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wren->pipeline_layout, 0, 1, &wren->set, 0, nullptr);
    wren->vk.CmdBindIndexBuffer(cmd, gpu_indices.buffer->buffer, 0, VK_INDEX_TYPE_UINT32);

    wren->vk.CmdSetViewport(cmd, 0, 1, wrei_ptr_to(VkViewport {
        0, 0,
        target_extent.x, target_extent.y,
        0, 1,
    }));
    wren->vk.CmdSetScissor(cmd, 0, 1, wrei_ptr_to(VkRect2D {
        .offset = {},
        .extent = vk_extent,
    }));

    rect2f32 viewport{{}, target_extent, wrei_xywh};

    for (auto& draw : draws) {
        switch (draw.blend) {
            break;case wren_blend_mode::premultiplied:
                wren->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, render.premult->pipeline);
            break;case wren_blend_mode::postmultiplied:
                wren->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, render.postmult->pipeline);
            break;case wren_blend_mode::none:
                wrei_assert_fail("", "Must select blend mode");
        }
        auto draw_scale = 2.f / viewport.extent;
        wren->vk.CmdPushConstants(cmd, wren->pipeline_layout, VK_SHADER_STAGE_ALL, 0, sizeof(wrui_render_input),
            wrei_ptr_to(wrui_render_input {
                .vertices = gpu_vertices.device(),
                .scale = draw_scale * draw.transform.scale,
                .offset = (draw.transform.translation - viewport.origin) * draw_scale - 1.f,
                .texture = {draw.image, render.sampler.get()},
            }));
        wren->vk.CmdDrawIndexed(cmd, draw.num_indices, 1, draw.first_index, draw.first_vertex, 0);
    }

    wren->vk.CmdEndRendering(cmd);
}

static
void compute(wrui_context* ctx, wren_image* target, wren_commands* commands)
{
    auto  cmd = commands->buffer;
    auto* wren = ctx->wren;
    auto& render = ctx->render;

    u32 tile_size = wrui_tile_size;

    std::vector<wrui_tile> tiles;
    std::vector<std::vector<u32>> bins;
    vec2u32 tile_counts = (target->extent + tile_size - 1u) / tile_size;
    for (u32 y = 0; y < target->extent.y; y += tile_size) {
        for (u32 x = 0; x < target->extent.x; x += tile_size) {
            tiles.emplace_back(wrui_tile {
                .offset = {x, y},
            });
        }
    }
    bins.resize(tiles.size());
    auto get_tile_idx = [&](vec2u32 tid) -> u32 {
        wrei_assert(tid.x < tile_counts.x && tid.y < tile_counts.y);
        return tid.y * tile_counts.x + tid.x;
    };

    std::vector<wrui_vertex>   vertices;
    std::vector<wrui_triangle> triangles;

    auto push_mesh = [&](wrui_transform_state tform, wren_image* image, wren_sampler* sampler,
        std::span<const wrui_vertex> in_vertices, std::span<const u16> in_indices)
    {
        u32 vtx = vertices.size();
        for (auto& vert : in_vertices) {
            vertices.emplace_back(wrui_vertex {
                .pos   = tform.translation + vert.pos * tform.scale,
                .uv    = vert.uv,
                .color = vert.color,
            });
        }

        // log_warn("  offset = {}", vtx);

        for (u32 i = 0; i < in_indices.size(); i += 3) {
            // log_warn("  tri[{}]", i / 3);
            auto tri = triangles.size();
            auto[v0, v1, v2] = std::make_tuple(in_indices[i] + vtx, in_indices[i + 1] + vtx, in_indices[i + 2] + vtx);
            // log_warn("    indices = [{}, {}, {}]", v0, v1, v2);
            triangles.emplace_back(wrui_triangle {
                .image = {
                    image ?: render.white.get(),
                    sampler ?: render.sampler.get()
                },
                .vertices { v0, v1, v2 },
            });

            auto min = glm::min(glm::min(vertices[v0].pos, vertices[v1].pos), vertices[v2].pos);
            auto max = glm::max(glm::max(vertices[v0].pos, vertices[v1].pos), vertices[v2].pos);

            // log_warn("    min = {}", wrei_to_string(min));
            // log_warn("    max = {}", wrei_to_string(max));

            auto min_tile = glm::max(vec2i32(glm::floor(min / f32(tile_size))), vec2i32(0, 0));
            auto max_tile = glm::min(vec2i32(glm::ceil( max / f32(tile_size))), vec2i32(tile_counts));

            // log_warn("    min_tile = {}", wrei_to_string(min_tile));
            // log_warn("    max_tile = {}", wrei_to_string(max_tile));

            for (i32 y = min_tile.y; y < max_tile.y; ++y) {
                for (i32 x = min_tile.x; x < max_tile.x; ++x) {
                    auto idx = get_tile_idx({x, y});
                    auto& bin = bins[idx];
                    bin.emplace_back(tri);
                    // log_warn("    bin[{}, {}].count = {}", x, y, bin.size());
                }
            }
        }
    };

    auto walk_node = [&](this auto&& walk_node, wrui_node* node) -> void
    {
        switch (node->type) {
            break;case wrui_node_type::transform:
                wrei_assert_fail("", "Unexpected transform node in layer stack");
            break;case wrui_node_type::tree: {
                for (auto& child : static_cast<wrui_tree*>(node)->children) {
                    walk_node(child.get());
                }
            }
            break;case wrui_node_type::mesh: {
                auto* mesh = static_cast<wrui_mesh*>(node);
                auto& tform = mesh->transform->global;

                push_mesh(tform, mesh->image.get(), mesh->sampler.get(), mesh->vertices, mesh->indices);
            }
            break;case wrui_node_type::texture: {
                auto* texture = static_cast<wrui_texture*>(node);
                aabb2f32 src = texture->src;
                aabb2f32 dst = texture->dst;

                //  0 ---- 1
                //  | a /  |  a = 0,2,1
                //  |  / b |  b = 1,2,3
                //  2 ---- 3

                auto vtx = [&](vec2f32 pos, vec2f32 uv = {}) {
                    return wrui_vertex { .pos = pos, .uv = uv, .color = texture->tint };
                };
                push_mesh(texture->transform->global, texture->image.get(), texture->sampler.get(),
                    {
                        vtx({dst.min.x, dst.min.y}, {src.min.x, src.min.y}),
                        vtx({dst.max.x, dst.min.y}, {src.max.x, src.min.y}),
                        vtx({dst.min.x, dst.max.y}, {src.min.x, src.max.y}),
                        vtx({dst.max.x, dst.max.y}, {src.max.x, src.max.y}),
                    }, {0, 2, 1, 1, 2, 3});
            }
        }
    };

    walk_node(ctx->scene.get());

    log_trace("tiles: {}", tiles.size());

    auto make_gpu = [&]<typename T>(std::span<T> data) {
        if (data.empty()) return wren_array<T> {};
        wren_array<T> arr{wren_buffer_create(wren, data.size_bytes(), {}), 0};
        std::memcpy(arr.host(), data.data(), data.size_bytes());
        wren_commands_protect_object(commands, arr.buffer.get());
        return arr;
    };

    u32 max = 0;

    std::vector<u32> elements;
    for (u32 i = 0; i < tiles.size(); ++i) {
        auto& tile = tiles[i];
        auto& bin = bins[i];

        max = std::max(max, u32(bin.size()));

        auto start = elements.size();
        elements.append_range(bin);

        tile.start = start;
        tile.count = bin.size();
    }

    log_error("BIN MAX = {}", max);
    log_error("ImGui texture: {}", u32(ctx->imgui.font_image->id));

    auto gpu_vertices = make_gpu(std::span<wrui_vertex>(vertices));
    auto gpu_tiles = make_gpu(std::span<wrui_tile>(tiles));
    auto gpu_triangles = make_gpu(std::span<wrui_triangle>(triangles));
    auto gpu_elements = make_gpu(std::span<u32>(elements));

    wren->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, render.compute->pipeline);
    wren->vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, wren->pipeline_layout, 0, 1, &wren->set, 0, nullptr);

    wrei_assert(target->usage.contains(wren_image_usage::storage));

    wren->vk.CmdPushConstants(cmd, wren->pipeline_layout, VK_SHADER_STAGE_ALL, 0, sizeof(wrui_render_input),
        wrei_ptr_to(wrui_render_input {
            .vertices = gpu_vertices.device(),
            .texture = {target, nullptr},
            .tiles = gpu_tiles.device(),
            .triangles = gpu_triangles.device(),
            .elements = gpu_elements.device(),
            .extent = target->extent,
        }));

    wren->vk.CmdDispatch(cmd, 1, 1, tiles.size());
}

void wrui_render(wrui_context* ctx, wrio_output* output, wren_image* target)
{
    auto queue = wren_get_queue(ctx->wren, wren_queue_type::graphics);
    auto commands = wren_commands_begin(queue);

    (void)raster;
    (void)compute;

    auto start = std::chrono::steady_clock::now();

    // raster(ctx, target, commands.get());
    compute(ctx, target, commands.get());

    auto end = std::chrono::steady_clock::now();
    log_debug("Recorded in: {}", wrei_duration_to_string(end - start));

    struct timer_guard : wrei_object
    {
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        ~timer_guard()
        {
            auto end = std::chrono::steady_clock::now();
            log_debug("Executed in: {}", wrei_duration_to_string(end - start));
        }
    };
    auto timer = wrei_create<timer_guard>();
    wren_commands_protect_object(commands.get(), timer.get());

    auto done = wren_commands_submit(commands.get(), {});
    wrio_output_present(output, target, done);
}
