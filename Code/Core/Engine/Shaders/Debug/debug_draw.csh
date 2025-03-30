#include "../interlop.hlsli"
#include "../root_signature.hlsli"

[numthreads(64, 1, 1)]
void CS_DebugDrawInitMultiDrawIndirectArgs(uint dispatch_id : SV_DispatchThreadID)
{
  if (dispatch_id == 0)
  {
    g_DebugLineArgsBuffer[0].vertex_count_per_instance = 0;
    g_DebugLineArgsBuffer[0].instance_count            = 1;
    g_DebugLineArgsBuffer[0].start_vertex_location     = 0;
    g_DebugLineArgsBuffer[0].start_instance_location   = 0;
  }

  if (dispatch_id >= kDebugMaxVertices)
  {
    return;
  }

  g_DebugLineVertexBuffer[dispatch_id].position = 0.0;
  g_DebugLineVertexBuffer[dispatch_id].color    = 0.0;
}
