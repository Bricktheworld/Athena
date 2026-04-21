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

struct Asset
{
  AssetId    id    = kNullAssetId;
  AssetType  type  = AssetType::kModel;
  u32        state = kAssetUnloaded;
};

// The template type needs to have a member "asset" of type "Asset"
template <typename T>
struct AssetHandle
{
  AssetId m_Id  = kNullAssetId;
  T*      m_Ptr = nullptr;

  bool is_valid() const
  {
    return m_Ptr->asset.id == m_Id;
  }
  const Asset* to_asset() const
  {
    return &deref()->asset;
  }

  operator       bool  ()      const { return is_valid();        }
  operator const Asset*()      const { return to_asset();        }

  AssetState get_asset_state() const { return (AssetState)to_asset()->state; }

  bool       is_loaded()       const { return get_asset_state() == kAssetReady;                                            }
  bool       is_streaming()    const { return get_asset_state() >= kAssetLoadRequested && get_asset_state() < kAssetReady; }

  T* deref()
  {
    ASSERT_MSG_FATAL(is_valid(), "Asset handle for 0x%x pointing to 0x%llx is invalid! Check using is_valid() first!", m_Id, m_Ptr);
    return m_Ptr;
  }

  const T* deref() const
  {
    ASSERT_MSG_FATAL(is_valid(), "Asset handle for 0x%x pointing to 0x%llx is invalid! Check using is_valid() first!", m_Id, m_Ptr);
    return m_Ptr;
  }

  // Overloaded arrow operator allows for safe 
  T* operator->()
  {
    ASSERT_MSG_FATAL(is_valid(), "Asset handle for 0x%x pointing to 0x%llx is invalid! Check using is_valid() first!", m_Id, m_Ptr);
    return m_Ptr;
  }

  const T* operator->() const
  {
    ASSERT_MSG_FATAL(is_valid(), "Asset handle for 0x%x pointing to 0x%llx is invalid! Check using is_valid() first!", m_Id, m_Ptr);
    return m_Ptr;
  }
};

struct Texture
{
  Asset asset;
};
typedef AssetHandle<Texture> TextureHandle;

struct Material
{
  Asset asset;
  u32   gpu_id = 0;
};
typedef AssetHandle<Material> MaterialHandle;


struct ModelSubset
{
  u32 vertex_start = 0;
  u32 vertex_count = 0;
  u32 index_start  = 0;
  u32 index_count  = 0;
};

struct Model
{
  Asset                 asset;
  Array<ModelSubset>    subsets;
  Array<GpuRtBlas>      subset_rt_blases;
  Array<MaterialHandle> materials;
};
typedef AssetHandle<Model> ModelHandle;

            void           init_asset_streamer(void);
            void           destroy_asset_streamer(void);
            void           asset_streamer_update(void);
            void           init_asset_registry(void);
THREAD_SAFE ModelHandle    kick_model_load(AssetId asset_id);
THREAD_SAFE MaterialHandle kick_material_load(AssetId asset_id);
