#include "internal.hpp"

#define SCENE_NOISY_NODES 0

#if SCENE_NOISY_NODES
#define NODE_LOG(...) log_error(__VA_ARGS__)
#else
#define NODE_LOG(...)
#endif

static
void request_frame(scene::Context* ctx)
{
    for (auto* output : ctx->outputs) {
        scene::output::request_frame(output);
    }
}

static
void damage_node(scene::Node* node)
{
    switch (node->type) {
        break;case scene::NodeType::tree:
            for (auto* child : static_cast<scene::Tree*>(node)->children) {
                damage_node(child);
            }
        break;case scene::NodeType::texture:
              case scene::NodeType::mesh:
            if (node->parent) {
                ::request_frame(node->parent->ctx);
                // TODO: Damage affected regions only.
                //       Since currently we're just forcing a global redraw,
                //       we can immediately return after a frame has been requested.
                return;
            }
        break;case scene::NodeType::input_region:
            ;
    }
}

// -----------------------------------------------------------------------------

scene::Node::~Node()
{
    core_assert(!parent);
}

// -----------------------------------------------------------------------------

scene::Tree::~Tree()
{
    for (auto& child : children) {
        child->parent = nullptr;
    }
}

auto scene::tree::create(scene::Context* ctx) -> core::Ref<scene::Tree>
{
    auto tree = core::create<scene::Tree>();
    tree->type = scene::NodeType::tree;
    tree->ctx = ctx;
    tree->enabled = true;
    return tree;
}

void scene::tree::set_enabled(scene::Tree* tree, bool enabled)
{
    if (tree->enabled == enabled) return;

    NODE_LOG("scene.tree{{{}}}.set_enabled({})", (void*)tree, enabled);

    if (enabled) {
        tree->enabled = true;
        damage_node(tree);
    } else {
        damage_node(tree);
        tree->enabled = false;
    }
}

void scene::node::unparent(scene::Node* node)
{
    if (!node->parent) return;

    NODE_LOG("scene.node{{{}}}.unparent", (void*)node);

    damage_node(node);
    auto parent = std::exchange(node->parent, nullptr);
    core_assert(parent->children.erase(node) == 1);
}

static
void reparent_unsafe(scene::Node* node, scene::Tree* tree)
{
    if (node->parent == tree) return;
    if (node->parent) {
        scene::node::unparent(node);
    }
    node->parent = tree;
}

static
void tree_place(scene::Tree* tree, scene::Node* reference, scene::Node* node, bool above)
{
    auto end = tree->children.end();
    auto cur =             std::ranges::find(tree->children, node);
    auto ref = reference ? std::ranges::find(tree->children, reference) : end;

    if (ref == end) {
        if (tree->children.empty()) {
            tree->children.push_back(node);
            return;
        }
        ref = above ? end - 1 : tree->children.begin();
    }

    if (cur == end) {
        if (above) tree->children.insert(ref + 1, node);
        else       tree->children.insert(ref,     node);
    } else if (cur != ref) {
        if (cur > ref) std::rotate(ref + i32(above), cur, cur + 1);
        else           std::rotate(cur, cur + 1, ref + i32(above));
    }

    NODE_LOG("scene.tree{{{}}}.place_{}({}, {})", (void*)tree, above ? "above" : "below", (void*)reference, (void*)node);

    // TODO: We only need to damage regions that were visually affected by the rotate
    damage_node(tree);
}

void scene::tree::place_below(scene::Tree* tree, scene::Node* reference, scene::Node* to_place)
{
    tree_place(tree, reference, to_place, false);
    reparent_unsafe(to_place, tree);

}

void scene::tree::place_above(scene::Tree* tree, scene::Node* reference, scene::Node* to_place)
{
    tree_place(tree, reference, to_place, true);
    reparent_unsafe(to_place, tree);
}

void scene::tree::set_translation(scene::Tree* tree, vec2f32 position)
{
    if (tree->translation == position) return;

    NODE_LOG("scene.tree{{{}}}.set_translation{}", (void*)tree, core::to_string(position));

    damage_node(tree);
    tree->translation = position;
    damage_node(tree);
}

// -----------------------------------------------------------------------------

auto scene::texture::create(scene::Context*) -> core::Ref<scene::Texture>
{
    auto texture = core::create<scene::Texture>();
    texture->blend = gpu::BlendMode::postmultiplied;
    texture->type = scene::NodeType::texture;
    texture->tint = {255, 255, 255, 255};
    texture->src = {{}, {1, 1}, core::minmax};
    return texture;
}

void scene::texture::set_image(scene::Texture* texture, gpu::Image* image, gpu::Sampler* sampler, gpu::BlendMode blend)
{
    if (   texture->image.get()   == image
        && texture->sampler.get() == sampler
        && texture->blend         == blend) return;

#if SCENE_NOISY_NODES
    if (texture->image.get()   != image)   NODE_LOG("scene.texture{{{}}}.set_image({})",   (void*)texture, (void*)image);
    if (texture->sampler.get() != sampler) NODE_LOG("scene.texture{{{}}}.set_sampler({})", (void*)texture, (void*)sampler);
    if (texture->blend         != blend)   NODE_LOG("scene.texture{{{}}}.set_blend({})",   (void*)texture, core::to_string(blend));
#endif

    texture->image = image;
    texture->sampler = sampler;
    texture->blend = blend;
    damage_node(texture);
}

void scene::texture::set_tint(scene::Texture* texture, vec4u8 tint)
{
    if (texture->tint == tint) return;

    NODE_LOG("scene.texture{{{}}}.set_tint{}", (void*)texture, core::to_string(tint));

    texture->tint = tint;
    damage_node(texture);
}

void scene::texture::set_src(scene::Texture* texture, aabb2f32 source)
{
    if (source == texture->src) return;

    NODE_LOG("scene.texture{{{}}}.set_src{}", (void*)texture, core::to_string(source));

    texture->src = source;
    damage_node(texture);
}

void scene::texture::set_dst(scene::Texture* texture, rect2f32 extent)
{
    if (extent == texture->dst) return;

    NODE_LOG("scene.texture{{{}}}.set_dst{}", (void*)texture, core::to_string(extent));

    damage_node(texture);
    texture->dst = extent;
    damage_node(texture);
}

void scene::texture::damage(scene::Texture* texture, aabb2i32 damage)
{
    NODE_LOG("scene.texture{{{}}}.damage{}", (void*)texture, core::to_string(rect2i32(damage)));

    damage_node(texture);
}

// -----------------------------------------------------------------------------

auto scene::mesh::create(scene::Context* ctx) -> core::Ref<scene::Mesh>
{
    auto mesh = core::create<scene::Mesh>();
    mesh->type = scene::NodeType::mesh;
    return mesh;
}

void scene::mesh::update(scene::Mesh* mesh, gpu::Image* image, gpu::Sampler* sampler, gpu::BlendMode blend, aabb2f32 clip, std::span<const scene_vertex> vertices, std::span<const u16> indices)
{
#if SCENE_NOISY_NODES
    if (mesh->image.get()   != image)   NODE_LOG("scene.mesh{{{}}}.set_image({})",   (void*)mesh, (void*)image);
    if (mesh->sampler.get() != sampler) NODE_LOG("scene.mesh{{{}}}.set_sampler({})", (void*)mesh, (void*)sampler);
    if (mesh->blend         != blend)   NODE_LOG("scene.mesh{{{}}}.set_blend({})",   (void*)mesh, core::to_string(blend));
    if (mesh->clip          != clip)    NODE_LOG("scene.mesh{{{}}}.set_clip{}",      (void*)mesh, core::to_string(clip));

    NODE_LOG("scene.mesh{{{}}}.set_vertices({}, {})", (void*)mesh, (void*)vertices.data(), vertices.size());
    NODE_LOG("scene.mesh{{{}}}.set_indices({}, {})",  (void*)mesh, (void*)indices.data(),  indices.size());
#endif

    damage_node(mesh);
    mesh->image = image;
    mesh->sampler = sampler;
    mesh->blend = blend;
    mesh->clip = clip;
    mesh->vertices.assign_range(vertices);
    mesh->indices.assign_range(indices);
    damage_node(mesh);
}

// -----------------------------------------------------------------------------

scene::InputRegion::~InputRegion()
{
    client->input_regions--;

    if (parent) {
        scene::node::unparent(this);
    }

    scene::update_pointer_focus(client->ctx);
}

auto scene::input_region::create(scene::Client* client) -> core::Ref<scene::InputRegion>
{
    auto region = core::create<scene::InputRegion>();
    region->type = scene::NodeType::input_region;
    region->client = client;
    client->input_regions++;
    return region;
}

void scene::input_region::set_region(scene::InputRegion* input_region, region2f32 region)
{
    if (input_region->region == region) return;

#if SCENE_NOISY_NODES
    NODE_LOG("scene.input_region{{{}}}.set_region([{:s}])", (void*)input_region,
        region.aabbs
            | std::views::transform((std::string(&)(const aabb2f32&))core::to_string)
            | std::views::join_with(", "sv));
#endif

    input_region->region = std::move(region);
}
