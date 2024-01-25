#pragma once
#include "Core/Engine/Render/render_graph.H"

struct GBuffer;
void init_taa(AllocHeap heap, RgBuilder* builder, RgHandle<GpuTexture>* hdr_lit, const GBuffer& gbuffer);
