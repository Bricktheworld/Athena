#pragma once
#include "Core/Foundation/assets.h"
#include "Core/Foundation/threading.h"

#include "Core/Engine/constants.h"

#include "Core/Engine/Render/graphics.h"

static constexpr u32 kMaxAssetLoadRequests = 0x1000;

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
  AssetId    id       = kNullAssetId;
  AssetType  type     = AssetType::kModel;
  u32        state    = kAssetUnloaded;
  u32        __pad0__ = 0;
};

// The template type needs to have a member "asset" of type "Asset"
template <typename T>
struct AssetHandle
{
  AssetId m_Id  = kNullAssetId;
  T*      m_Ptr = nullptr;

  bool is_valid() const
  {
    return m_Ptr && m_Ptr->asset.id == m_Id;
  }
  const Asset* to_asset() const
  {
    return &deref()->asset;
  }

  operator       bool  ()      const { return is_valid();        }
  operator const Asset*()      const { return to_asset();        }

  AssetState get_asset_state() const { return (AssetState)to_asset()->state; }

  bool       is_loaded()       const { return get_asset_state() == kAssetReady;                                            }
  bool       is_broken()       const { return get_asset_state() >= kAssetFailedToLoad;                                     }
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
  Asset          asset;

  u32            width;
  u32            height;
  ColorSpaceName color_space;

  GpuTexture     gpu_texture;
  GpuDescriptor  srv_descriptor;
};
typedef AssetHandle<Texture> TextureHandle;

struct Material
{
  Asset                asset;
  u32                  gpu_id = 0;
  Array<TextureHandle> textures;
};
typedef AssetHandle<Material> MaterialHandle;

struct ModelSubsetLod
{
  u32  vertex_start = 0;
  u32  vertex_count = 0;
  u32  index_start  = 0;
  u32  index_count  = 0;
  f32  error        = 0.0f;
};

struct ModelSubset
{
  Array<ModelSubsetLod> lods;
  u32  mat_gpu_id  = 0;
  u32  rt_blas_lod = 0;
  Vec3 center      = {};
  f32  radius      = 0.0f;
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
THREAD_SAFE TextureHandle  kick_texture_load(AssetId asset_id);

struct AssetStreamingStatistics
{
  alignas(kCacheLineSize) Atomic<u64> file_io_bpf         = 0;
  alignas(kCacheLineSize) Atomic<u64> file_io_elapsed_ms  = 0;
  alignas(kCacheLineSize) Atomic<u64> gpu_io_bpf          = 0;
  alignas(kCacheLineSize) Atomic<u64> gpu_io_elapsed_ms   = 0;

  // EMA-smoothed bandwidth in bytes/sec, updated on the main thread
  f64                                 file_io_bps         = 0.0;
  f64                                 gpu_io_bps          = 0.0;
};

extern AssetStreamingStatistics g_AssetStreamingStats;
