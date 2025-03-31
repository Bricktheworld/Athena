#include "../interlop.hlsli"
#include "../root_signature.hlsli"

[numthreads(64, 1, 1)]
void CS_DebugDrawInitMultiDrawIndirectArgs(uint dispatch_id : SV_DispatchThreadID)
{
  if (dispatch_id == 0)
  {
    g_DebugArgsBuffer[0].vertex_count_per_instance = 0;
    g_DebugArgsBuffer[0].instance_count            = 1;
    g_DebugArgsBuffer[0].start_vertex_location     = 0;
    g_DebugArgsBuffer[0].start_instance_location   = 0;

    g_DebugArgsBuffer[1].vertex_count_per_instance = 6;
    g_DebugArgsBuffer[1].instance_count            = 0;
    g_DebugArgsBuffer[1].start_vertex_location     = 0;
    g_DebugArgsBuffer[1].start_instance_location   = 0;
  }

  if (dispatch_id < kDebugMaxVertices)
  {
    g_DebugLineVertexBuffer[dispatch_id] = (DebugLinePoint)0;
  }

  if (dispatch_id < kDebugMaxSdfs)
  {
    g_DebugSdfBuffer[dispatch_id] = (DebugSdf)0;
  }
}
