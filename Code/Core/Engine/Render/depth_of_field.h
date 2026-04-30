#pragma once
#include "Core/Engine/Render/renderer.h"

void render_handler_dof_generate_coc(const RenderEntry* entries, u32 entry_count);
void render_handler_dof_bokeh_blur   (const RenderEntry* entries, u32 entry_count);
void render_handler_dof_composite    (const RenderEntry* entries, u32 entry_count);
