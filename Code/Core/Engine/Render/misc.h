#pragma once
#include "Core/Engine/Render/render_graph.h"

void init_frame_init_pass(AllocHeap heap, RgBuilder* builder);

void init_imgui_pass(AllocHeap heap, RgBuilder* builder, RgHandle<GpuTexture>* dst);
