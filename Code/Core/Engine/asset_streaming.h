#pragma once
#include "Core/Foundation/assets.h"
#include "Core/Foundation/threading.h"

#include "Core/Engine/Render/graphics.h"

struct AssetLoader;
struct GpuStreamDevice;
struct IDStorageFactory;
struct IDStorageQueue2;
struct IDStorageFile;
struct IDStorageStatusArray;

static constexpr u32 kMaxAssetLoadRequests = 0x1000;
static constexpr u32 kMaxAssets            = 0x2000;


static constexpr u32 kMaxMaterials = 0x100;
static constexpr u32 kMaxSceneObjs = 0x1000;

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

struct AssetDesc
{
  AssetType  type  = AssetType::kModel;
  AssetState state = kAssetUnloaded;
  union
  {
    struct 
    {
    } model;
    struct
    {
      GpuDescriptor descriptor;
      GpuTexture    allocation;
    } texture;
    MaterialData material;
    GpuShader    shader;
  };
};

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



enum GpuStreamResult
{
  kGpuStreamOk,
  kGpuStreamFailedToOpenFile,
};

void init_asset_loader(void);
void kick_asset_load(AssetId asset_id);
void destroy_asset_loader(void);

Result<const GpuTexture*,    AssetState> get_gpu_texture_asset(AssetId asset_id);
Result<Texture2DPtr<float4>, AssetState> get_srv_texture_asset(AssetId asset_id);
Result<const MaterialData*,  AssetState> get_material_asset   (AssetId asset_id);
