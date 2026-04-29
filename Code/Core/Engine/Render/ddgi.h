#pragma once
#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Shaders/Include/ddgi_common.hlsli"

void render_handler_rt_diffuse_gi_init      (const RenderEntry* entries, u32 entry_count);
void render_handler_rt_diffuse_gi_trace_rays(const RenderEntry* entries, u32 entry_count);
void render_handler_rt_diffuse_gi_probe_blend(const RenderEntry* entries, u32 entry_count);
