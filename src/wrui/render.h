#pragma once

#include "wren/shaders/shared.h"

struct wrui_vertex
{
    vec2f32 pos;
    vec2f32 uv;
    vec4u8 color;
};

struct wrui_triangle
{
    image4f32 image;
    u32 vertices[3];
};

struct wrui_tile
{
    u32 count;
    u32 start;
    vec2u32 offset;
};

static const u8 wrui_tile_size = 16;

struct wrui_render_input
{
    wren_const_ptr<wrui_vertex> vertices;
    vec2f32 scale;
    vec2f32 offset;
    image4f32 texture;

    wren_const_ptr<wrui_tile> tiles;
    wren_const_ptr<wrui_triangle> triangles;
    wren_const_ptr<u32> elements;
    vec2u32 extent;
};
