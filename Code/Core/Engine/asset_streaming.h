#pragma once
#include "Core/Engine/Render/graphics.h"
#include "Core/Foundation/assets.h"

struct AssetLoader;
struct GpuStreamDevice;
struct IDStorageFactory;
struct IDStorageQueue2;

extern AssetLoader*     g_AssetLoader;
extern GpuStreamDevice* g_GpuStreamDevice;


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

struct TextureDesc
{
  u32        width  = 0;
  u32        height = 0;
  GpuFormat  format = kGpuFormatUnknown;
  GpuTexture gpu;
};

struct ModelDesc
{
};

struct MaterialDesc
{
  u32 num_textures = 0;
};

struct AssetDesc
{
  AssetType type = AssetType::kModel;
  union
  {
    ModelDesc    model;
    TextureDesc  texture;
    MaterialDesc material;
    GpuShader    shader;
  };
};


struct GpuStreamDevice
{
  IDStorageFactory* factory;

  IDStorageQueue2*  file_queue;
  GpuFence          file_queue_fence;
};

enum AssetState
{
  kAssetUnloaded,
  kAssetLoadRequested,
  kAssetStreaming,
  kAssetUninitialized,
  kAssetReady,
  kAssetFailedToLoad,
  kAssetFailedToInitialize,
};

struct AssetLoader
{
  RingQueue<AssetId>             requests;
  HashTable<AssetId, AssetState> asset_states;
};

enum GpuStreamResult
{
  kGpuStreamOk,
  kGpuStreamFailedToOpenFile,
};

void init_gpu_stream_device(void);
void destroy_gpu_stream_device(void);
void submit_gpu_stream_requests(void);
GpuStreamResult request_gpu_stream_asset(const AssetGpuLoadRequest& request);

void init_asset_loader(void);
void load_asset(AssetId asset_id);
void process_asset_loads(void);
void destroy_asset_loader(void);
