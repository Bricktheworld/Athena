#pragma once

#define kHZBMipCount 4
#define kHZBDownsampleDimension 16U

struct GBufferFillIndirectArgsPhaseOneSrt
{
  u32 scene_obj_count;
  StructuredBufferPtr<u64> scene_obj_occlusion;
  RWStructuredBufferPtr<u32> scene_obj_gpu_ids;
  RWStructuredBufferPtr<MultiDrawIndirectIndexedArgs> multi_draw_args;
};

struct GBufferFillIndirectArgsPhaseTwoSrt
{
  u32 scene_obj_count;
  RWStructuredBufferPtr<u64> scene_obj_occlusion;
  RWStructuredBufferPtr<u32> scene_obj_gpu_ids;
  RWStructuredBufferPtr<MultiDrawIndirectIndexedArgs> multi_draw_args;
  Texture2DPtr<f32> hzb;
};

struct GBufferIndirectSrt
{
  StructuredBufferPtr<u32> scene_obj_gpu_ids;
};


struct GenerateHZBSrt
{
  Texture2DPtr<f32>   gbuffer_depth;
  RWTexture2DPtr<f32> depth_mip0;
  RWTexture2DPtr<f32> depth_mip1;
  RWTexture2DPtr<f32> depth_mip2;
  RWTexture2DPtr<f32> depth_mip3;
};
