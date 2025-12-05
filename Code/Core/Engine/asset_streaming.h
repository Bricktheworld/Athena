#pragma once
#include "Core/Foundation/assets.h"
#include "Core/Foundation/threading.h"

#include "Core/Engine/constants.h"

#include "Core/Engine/Render/graphics.h"

struct AssetLoader;
struct GpuStreamDevice;
struct IDStorageFactory;
struct IDStorageQueue2;
struct IDStorageFile;
struct IDStorageStatusArray;

static constexpr u32 kMaxAssetLoadRequests = 0x1000;

enum AssetGpuLoadType
{
  kAssetGpuLoadTypeBuffer,
  kAssetGpuLoadTypeTexture,
};

struct AssetGpuLoadRequest
{
  AssetId          asset_id          = 0;
  AssetGpuLoadType type              = kAssetGpuLoadTypeBuffer;
  u64              src_offset        = 0;
  u32              compressed_size   = 0;
  u32              uncompressed_size = 0;

  union
  {
    struct
    {
      const GpuBuffer*  dst;
      u64               dst_offset;
    } buffer;
    union
    {
      const GpuTexture* dst;
    } texture;
  };
};

enum AssetState : u32
{
  kAssetUnloaded = 0,
  kAssetLoadRequested,
  kAssetStreaming,
  kAssetUninitialized,
  kAssetReady,
  kAssetFailedToLoad,
  kAssetFailedToInitialize,
};

struct ModelSubsetMetadata
{
  u32 vertex_start;
  u32 vertex_count;
  u32 index_start;
  u32 index_count;
};

struct ModelMetadata
{
  Array<ModelSubsetMetadata> subsets;
  Array<GpuRtBlas>           subset_rt_blases;
};

struct AssetDesc
{
  AssetType  type       = AssetType::kModel;
  AssetState state      = kAssetUnloaded;
  bool       needs_init = false;

  union
  {
    ModelMetadata model;
    struct
    {
      GpuDescriptor descriptor;
      GpuTexture    allocation;
    } texture;
    MaterialData material;
    GpuShader    shader;
  };
};

// GpuStreamDevice is responsible for copying stuff to the GPU asynchronously
struct GpuStreamInFlight
{
  IDStorageFile* file        = nullptr;
  AssetId        asset_id    = 0;
  FenceValue     fence_value = 0;
};

struct GpuStreamDevice
{
  IDStorageFactory*            factory      = nullptr;

  IDStorageQueue2*             file_queue   = nullptr;
  GpuFence                     file_queue_fence;

  RingQueue<GpuStreamInFlight> in_flight_requests;
  RingQueue<AssetId>           asset_completed_streams;
};


// GpuBuildDevice is responsible for any building tasks on the GPU
struct GpuBuildInFlight
{
  AssetId    asset_id    = 0;
  FenceValue fence_value = 0;
};

struct GpuBuildDevice
{
  GpuFence                    build_queue_fence;
                              
  RingQueue<GpuBuildInFlight> in_flight_rquests;
  RingQueue<AssetId>          asset_built_streams;
};



enum GpuStreamResult
{
  kGpuStreamOk,
  kGpuStreamFailedToOpenFile,
};

            void init_asset_loader(void);
THREAD_SAFE void kick_asset_load(AssetId asset_id);
            void destroy_asset_loader(void);
THREAD_SAFE Result<const GpuTexture*,    AssetState> get_gpu_model_asset(AssetId asset_id);
THREAD_SAFE Result<const GpuTexture*,    AssetState> get_gpu_texture_asset(AssetId asset_id);
THREAD_SAFE Result<Texture2DPtr<float4>, AssetState> get_srv_texture_asset(AssetId asset_id);
THREAD_SAFE Result<const ModelMetadata*, AssetState> get_model_asset      (AssetId asset_id);
THREAD_SAFE bool                                     is_model_bvh_built   (AssetId asset_id);
THREAD_SAFE Result<const MaterialData*,  AssetState> get_material_asset   (AssetId asset_id);
