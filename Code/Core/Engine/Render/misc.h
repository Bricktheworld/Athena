#pragma once
#include "Core/Engine/Render/render_graph.h"

struct FrameResources
{
  RgHandle<GpuBuffer> scene_buffer;
  RgHandle<GpuBuffer> material_buffer;
};

FrameResources init_frame_init_pass(AllocHeap heap, RgBuilder* builder);

void init_imgui_pass(AllocHeap heap, RgBuilder* builder, RgHandle<GpuTexture>* dst);
