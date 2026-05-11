#ifndef UI_RENDER_H
#define UI_RENDER_H

#include "gpu/shaders/shared.h"

struct UiVertex
{
    vec2f32 pos;
    vec2f32 uv;
    vec4u8 color;
};

GPU_CONST_PTR_DECLARE(UiVertex);

struct UiRenderInput
{
    GPU_CONST_PTR(UiVertex) vertices;
    vec2f32 scale;
    vec2f32 offset;
    GpuImageHandle texture;
};

#endif // UI_RENDER_H
