#pragma once
#include "Core/Engine/Render/render_graph.H"

struct GBuffer;
RgHandle<GpuTexture> init_taa_buffer(RgBuilder* builder);
void init_taa(AllocHeap heap, RgBuilder* builder, RgHandle<GpuTexture> hdr_lit, const GBuffer& gbuffer, RgHandle<GpuTexture>* taa_buffer);
