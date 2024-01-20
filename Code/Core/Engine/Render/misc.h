#pragma once
#include "Core/Engine/Render/render_graph.h"
#include "Core/Engine/Render/gbuffer.h"

// TODO(Brandon): This is stupid, don't pass the gbuffer and do a dummy write...
// I need a way to somehow place the frame init at the actual start of the frame.
void init_frame_init_pass(AllocHeap heap, RgBuilder* builder, GBuffer* gbuffer);
void render_handler_frame_init(RenderContext* ctx, const void* data);

void init_imgui_pass(AllocHeap heap, RgBuilder* builder, RgHandle<GpuImage>* dst);
void render_handler_imgui(RenderContext* ctx, const void* data);
