#pragma once
#include "Core/Engine/Render/render_graph.h"

struct VBuffer
{
  RgHandle<GpuTexture> primitive_ids;
  RgHandle<GpuTexture> depth;
};

VBuffer init_vbuffer(AllocHeap heap, RgBuilder* builder);

void init_debug_vbuffer(AllocHeap heap, RgBuilder* builder, const VBuffer& vbuffer, RgHandle<GpuTexture>* dst);
