#include "../root_signature.hlsli"
#include "../interlop.hlsli"
#include "../Include/clear_common.hlsli"

ConstantBuffer<ClearStructuredBufferSrt> g_Srt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(64, 1, 1)]
void CS_ClearStructuredBuffer(uint thread_id : SV_DispatchThreadID)
{
  RWStructuredBuffer<u32> buffer = DEREF(g_Srt.dst);

  uint num_structs = 0;
  uint stride      = 0;

  if (thread_id >= g_Srt.count)
  {
    return;
  }

  uint dst    = thread_id + g_Srt.offset;

  buffer[dst] = g_Srt.clear_value;
}