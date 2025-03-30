#pragma once
#include "Core/Engine/Render/render_graph.h"

struct FrameResources
{
  RgHandle<GpuBuffer> viewport_buffer;
  RgHandle<GpuBuffer> material_buffer;
  RgHandle<GpuBuffer> scene_obj_buffer;

  RgHandle<GpuBuffer> debug_draw_args_buffer;
  RgHandle<GpuBuffer> debug_line_vert_buffer;
};

FrameResources init_frame_init_pass(AllocHeap heap, RgBuilder* builder);

// TODO(bshihabi): Put this somewhere better...
extern f64 g_CpuEffectiveTime;

void init_imgui_pass(AllocHeap heap, RgBuilder* builder, RgHandle<GpuTexture>* dst);
void init_debug_draw_pass(AllocHeap heap, RgBuilder* builder, const FrameResources& frame_resources, RgHandle<GpuTexture>* dst);
