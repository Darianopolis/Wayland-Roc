#include "render.h"

[vk::push_constant] wrui_render_input si;

struct vertex_output
{
    vec4f32 pos : SV_Position;
    vec2f32 uv;
    vec4f32 color;
};

[shader("vertex")]
vertex_output vertex(u32 id : SV_VulkanVertexID)
{
    var v = si.vertices[id];
    vertex_output out;
    out.pos = vec4f32((v.pos * si.scale) + si.offset, 0, 1);
    out.uv = v.uv;
    out.color = wrei_unpack_unorm4u8(v.color);
    return out;
}

[shader("fragment")]
vec4f32 fragment(in vertex_output vout)
{
    return si.texture.sample(vout.uv) * vout.color;
}

// -----------------------------------------------------------------------------

f32 edge_function(vec2f32 a, vec2f32 b, vec2f32 c)
{
    return (c.x - a.x) * (b.y - a.y)
         - (c.y - a.y) * (b.x - a.x);
}

bool inside(f32 w0, f32 w1, f32 w2)
{
    return w0 >= 0 && w1 >= 0 && w2 >= 0;
}

bool is_top_left(vec2f32 a, vec2f32 b)
{
    if (a.y == b.y && a.x < b.x) return true;
    if (a.y > b.y) return true;
    return false;
}

bool inside_edge(f32 e, bool topleft)
{
    // return (e > 0) || (e == 0 && topleft);
    return e >= 0;
}

wren_const_ptr<wrui_vertex> vert(wren_const_ptr<wrui_triangle> t, u32 i)
{
    return si.vertices + t.vertices[i];
}

bool intersect_tri(wren_const_ptr<wrui_triangle> t, vec2f32 p, out f32 w0, out f32 w1, out f32 w2)
{
    // printf("vertices = %p\n", si.vertices);
    // printf("intersecting tri [%d] @ (%f, %f) - %d, %d, %d \n",
    //     idx, p.x, p.y,
    //     t.vertices[0], t.vertices[1], t.vertices[2]);

    // printf("intersecting tri [%d] @ (%f, %f) - (%f, %f) (%f, %f) (%f, %f) \n",
    //     idx, p.x, p.y,
    //     vert(t, 0).pos.x, vert(t, 0).pos.y,
    //     vert(t, 1).pos.x, vert(t, 1).pos.y,
    //     vert(t, 2).pos.x, vert(t, 2).pos.y);

    var v0 = vert(t, 0).pos;
    var v1 = vert(t, 1).pos;
    var v2 = vert(t, 2).pos;

    f32 area = edge_function(v0, v1, v2);
    if (area == 0.0) {
        // printf("degenerate\n");
        w0 = w1 = w2 = -1;
        return false;
    }

    // f32 e0 = edge_function(v1, v2, p);
    // f32 e1 = edge_function(v2, v0, p);
    // f32 e2 = edge_function(v0, v1, p);

    // // Edge top-left flags
    // bool tl0 = is_top_left(v1, v2);
    // bool tl1 = is_top_left(v2, v0);
    // bool tl2 = is_top_left(v0, v1);

    // f32 inv_area = 1.0 / area;

    // w0 = edge_function(v1, v2, p) * inv_area;
    // w1 = edge_function(v2, v0, p) * inv_area;
    // w2 = 1.0 - w0 - w1;

    // if (inside_edge(w0, tl0) && inside_edge(w1, tl1) && inside_edge(w2, tl2)) {
    //     // f32 inv_area = 1.0 / area;

    //     // w0 = e0 * inv_area;
    //     // w1 = e1 * inv_area;
    //     // w2 = 1.0 - w0 - w1;

    //     return true;
    // } else {
    //     // w0 = w1 = w2 = -1;
    //     return false;
    // }

    f32 inv_area = 1.0 / area;

    w0 = edge_function(v1, v2, p) * inv_area;
    w1 = edge_function(v2, v0, p) * inv_area;
    w2 = 1.0 - w0 - w1;

    // printf("bary(%f, %f, %f)\n", w0, w1, w2);

    return inside(w0, w1, w2);
}

vec4f32 triangle(u32 idx, vec2i32 pixel)
{
    var center = vec2f32(pixel) + 0.49;

    // printf("intersecting tri [%d] @ (%f, %f)\n", idx, center.x, center.y);

    f32 w0, w1, w2;
    var t = si.triangles + idx;
    if (intersect_tri(t, center, w0, w1, w2)) {
        var v0 = vert(t, 0);
        var v1 = vert(t, 1);
        var v2 = vert(t, 2);
        // vec2f32 uv = (vert(t, 0).uv * vec2f32(w0))
        //            * (vert(t, 1).uv * vec2f32(w1))
        //            * (vert(t, 2).uv * vec2f32(w2));
        vec2f32 uv = v0.uv * w0 + v1.uv * w1 + v2.uv * w2;
        vec4f32 color = wrei_unpack_unorm4u8(v0.color) * w0
                      + wrei_unpack_unorm4u8(v1.color) * w1
                      + wrei_unpack_unorm4u8(v2.color) * w2;
        printf("intersect(%f,%f, %f) - (%f, %f) (%f, %f) (%f, %f) -> (%f, %f)\n",
            w0, w1, w2,
            vert(t, 0).uv.x, vert(t, 0).uv.y,
            vert(t, 1).uv.x, vert(t, 1).uv.y,
            vert(t, 2).uv.x, vert(t, 2).uv.y,
            uv.x, uv.y
        );
        // vec2f32 uv = vec2f32(0.1, 0.1);
        // printf("intersect, t.image = %d, t.sampler = %d\n", t.image.image, t.image.sampler);
        // if (uv.x != 0 || uv.y != 0) {
        //     printf("intersect(%f, %f, %f) - (%f, %f)\n", w0, w1, w2, uv.x, uv.y);
        // }
        vec4f32 sampled = t.image.sample(uv);
        // vec4f32 sampled = image4f32(3, 1).sample(vec2f32(0.1, 0.1));
        return sampled * color;
        // return vec4f32(w0, w1, w2, 1);
    }

    return vec4f32(0);
}

[numthreads(wrui_tile_size, wrui_tile_size, 1)]
void compute(uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    var tile = si.tiles + gid.z;

    // printf("tex = %d, dims = (%d, %d)\n", si.texture.image, si.extent.x, si.extent.y);

    vec2i32 dims = si.extent;
    vec2i32 pixel = vec2i32(gtid.xy) + tile.offset;

    // printf("z = %d (offset = %d, %d), xy = (%d, %d) -> (%d, %d)\n", gid.z, tile.offset.x, tile.offset.y, gtid.x, gtid.y, pixel.x, pixel.y);

    // Guard against over-dispatch
    if (pixel.x >= dims.x || pixel.y >= dims.y)
        return;

    // vec4f32 color = vec4f32(0, 0, 0, 1);
    vec3f32 color = vec3f32(0, 0, 0);
    f32 transmittance = 1;

    u32 depth = 0;
    for (u32 i = tile.count; i-- > 0;) {
        depth++;
        // color = triangle(si.elements[tile.start + i], pixel);
        // if (color.a > 0) break;
        vec4f32 res = triangle(si.elements[tile.start + i], pixel);
        color += res.xyz * transmittance * res.w;
        transmittance *= (1 - res.w);
        if (transmittance == 0) break;
        // if (color.a > 0) break;
    }

    vec2f32 uv = (vec2f32(pixel) + 0.5) / vec2f32(dims);
    // color = vec3f32(depth / 56.0, 0.0, 0.0);
    // color = image4f32(3, 1).sample(vec2f32(pixel) / vec2f32(dims));
    // color = image4f32(3, 1).sample(vec2f32(0.1, 0.1));

    // color = vec4f32(depth / 56.0, 0.0, 0.0, 1.0);

    // printf("color = (%f, %f, %f, %f)\n", color.x, color.y, color.z, color.w);

    si.texture.store(pixel, vec4f32(color, 1));
}
