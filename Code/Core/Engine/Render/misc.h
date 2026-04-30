#pragma once
#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/gbuffer.h"

// TODO(bshihabi): Put this somewhere better...
extern f64 g_CpuEffectiveTime;
void render_handler_frame_init(const RenderEntry*, u32);
void render_handler_debug_ui(const RenderEntry*, u32);
void render_handler_indirect_debug_draw(const RenderEntry*, u32);

// void init_imgui_pass(AllocHeap heap, RgBuilder* builder, RgHandle<GpuTexture>* dst);
// void init_debug_draw_pass(AllocHeap heap, RgBuilder* builder, const FrameResources& frame_resources, GBuffer* gbuffer, RgHandle<GpuTexture>* dst);
