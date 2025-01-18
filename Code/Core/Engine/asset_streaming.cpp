#include "Core/Foundation/types.h"

#include "Core/Engine/memory.h"
#include "Core/Engine/asset_streaming.h"
#include "Core/Engine/Render/renderer.h"

#include "Core/Engine/Vendor/DirectStorage/dstorage.h"
#include "Core/Engine/Vendor/DirectStorage/dstorageerr.h"

struct AssetLoader
{
  SpinLocked<RingQueue<AssetId>>            requests;
  SpinLocked<HashTable<AssetId, AssetDesc>> assets;


  // TODO(bshihabi): We really probably need TLSF here
  LinearAllocator                           allocator;

  Thread                                    thread;
  ThreadSignal                              wake_cond;
  bool                                      kill;
};

static AssetLoader*     g_AssetLoader      = nullptr;
static GpuStreamDevice* g_GpuStreamDevice  = nullptr;

static constexpr u32 kStagingBufferMemory  = MiB(256);
static constexpr u64 kAssetThreadStackSize = MiB(8);
static constexpr u64 kAssetAllocatorSize   = MiB(32);

static void
init_gpu_stream_device(void)
{
  ASSERT_MSG_FATAL(g_GpuDevice != nullptr, "Gpu Device needs to be initialized first before the asset loader!");
  if (g_GpuStreamDevice == nullptr)
  {
    g_GpuStreamDevice = HEAP_ALLOC(GpuStreamDevice, g_InitHeap, 1);
  }

  zero_memory(g_GpuStreamDevice, sizeof(GpuStreamDevice));

  HASSERT(DStorageGetFactory(IID_PPV_ARGS(&g_GpuStreamDevice->factory)));
  g_GpuStreamDevice->factory->SetStagingBufferSize(kStagingBufferMemory);

  g_GpuStreamDevice->factory->SetDebugFlags(DSTORAGE_DEBUG_SHOW_ERRORS | DSTORAGE_DEBUG_BREAK_ON_ERROR | DSTORAGE_DEBUG_RECORD_OBJECT_NAMES);

  DSTORAGE_QUEUE_DESC queue_desc{};
  queue_desc.Capacity   = DSTORAGE_MAX_QUEUE_CAPACITY;
  queue_desc.Priority   = DSTORAGE_PRIORITY_NORMAL;
  queue_desc.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
  queue_desc.Device     = g_GpuDevice->d3d12;
  queue_desc.Name       = "AssetLoader File Queue";

  HASSERT(
    g_GpuStreamDevice->factory->CreateQueue(&queue_desc, IID_PPV_ARGS(&g_GpuStreamDevice->file_queue))
  );

  g_GpuStreamDevice->file_queue_fence        = init_gpu_fence();
  g_GpuStreamDevice->in_flight_requests      = init_ring_queue<GpuStreamInFlight>(g_InitHeap, kMaxAssetLoadRequests);
  g_GpuStreamDevice->asset_completed_streams = init_ring_queue<AssetId>          (g_InitHeap, kMaxAssetLoadRequests);
}

static void
destroy_gpu_stream_device(void)
{
  g_GpuStreamDevice->file_queue->Close();

  destroy_gpu_fence(&g_GpuStreamDevice->file_queue_fence);
  COM_RELEASE(g_GpuStreamDevice->factory);
  COM_RELEASE(g_GpuStreamDevice->file_queue);

  zero_memory(g_GpuStreamDevice, sizeof(GpuStreamDevice));
}

static GpuStreamResult
request_gpu_stream_asset(const AssetGpuLoadRequest& request)
{
  ASSERT_MSG_FATAL(g_AssetLoader != nullptr, "AssetLoader not initialized!");

  wchar_t path[kAssetPathSize];
  asset_id_to_path_w(path, request.asset_id);

  IDStorageFile* file = nullptr;
  HRESULT res = g_GpuStreamDevice->factory->OpenFile(path, IID_PPV_ARGS(&file));
  if (FAILED(res))
  {
    return kGpuStreamFailedToOpenFile;
  }

  DSTORAGE_REQUEST drequest          = {};
  drequest.Options.CompressionFormat = DSTORAGE_COMPRESSION_FORMAT_NONE;
  drequest.Options.SourceType        = DSTORAGE_REQUEST_SOURCE_FILE;
  drequest.Source.File.Source        = file;
  drequest.Source.File.Offset        = request.src_offset;
  drequest.Source.File.Size          = request.compressed_size;
  drequest.UncompressedSize          = request.uncompressed_size;
  drequest.CancellationTag           = request.asset_id;
  drequest.Name                      = "GPU Asset Streaming Request";

  switch (request.type)
  {
    case kAssetGpuLoadTypeBuffer:
    {
      drequest.Options.DestinationType     = DSTORAGE_REQUEST_DESTINATION_BUFFER;
      drequest.Destination.Buffer.Resource = request.buffer.dst->d3d12_buffer;
      drequest.Destination.Buffer.Offset   = request.buffer.dst_offset;
      drequest.Destination.Buffer.Size     = request.uncompressed_size;
    } break;
    case kAssetGpuLoadTypeTexture:
    {
      drequest.Options.DestinationType                           = DSTORAGE_REQUEST_DESTINATION_MULTIPLE_SUBRESOURCES;
      drequest.Destination.MultipleSubresources.Resource         = request.texture.dst->d3d12_texture;
      drequest.Destination.MultipleSubresources.FirstSubresource = 0;
    } break;
  }

  g_GpuStreamDevice->file_queue->EnqueueRequest(&drequest);
  FenceValue value = ++g_GpuStreamDevice->file_queue_fence.value;
  g_GpuStreamDevice->file_queue->EnqueueSignal(
    g_GpuStreamDevice->file_queue_fence.d3d12_fence,
    value
  );
  g_GpuStreamDevice->file_queue->Submit();

  GpuStreamInFlight in_flight_desc = {0};
  in_flight_desc.file              = file;
  in_flight_desc.asset_id          = request.asset_id;
  in_flight_desc.fence_value       = value;

  ring_queue_push(&g_GpuStreamDevice->in_flight_requests, in_flight_desc);
  dbgln("Streaming 0x%x\n  Texture: (%u x %u)", request.asset_id, request.texture.dst->desc.width, request.texture.dst->desc.height);

  return kGpuStreamOk;
}


static void
submit_gpu_stream_requests(void)
{
#if 0
  DSTORAGE_ERROR_RECORD record;
  g_GpuStreamDevice->file_queue->RetrieveErrorRecord(&record);
  if (record.FailureCount > 0)
  {
    dbgln("Direct storage errors: %u", record.FailureCount);
    if (FAILED(record.FirstFailure.HResult))
    {
      _com_error err(record.FirstFailure.HResult);
      const wchar_t* err_msg = err.ErrorMessage();
      dbgln("Direct Storage error: %ls", err_msg);
    }
  }
#endif

  FenceValue value = poll_gpu_fence_value(&g_GpuStreamDevice->file_queue_fence);
  while (!ring_queue_is_empty(g_GpuStreamDevice->in_flight_requests))
  {
    GpuStreamInFlight in_flight;
    ring_queue_peak_front(g_GpuStreamDevice->in_flight_requests, &in_flight);
    if (in_flight.fence_value <= value)
    {
      ring_queue_pop(&g_GpuStreamDevice->in_flight_requests);
      ring_queue_push(&g_GpuStreamDevice->asset_completed_streams, in_flight.asset_id);
      dbgln("GPU Stream 0x%x Completed", in_flight.asset_id);

      COM_RELEASE(in_flight.file);
    }
    else
    {
      break;
    }
  }

  if (value < g_GpuStreamDevice->file_queue_fence.value)
  {
    g_GpuStreamDevice->file_queue->Submit();
  }
  else
  {
    bool no_requests = false;
    ACQUIRE(&g_AssetLoader->requests, auto* requests)
    {
      no_requests = ring_queue_is_empty(*requests);
    };

    if (no_requests)
    {
      wait_for_thread_signal(&g_AssetLoader->wake_cond);
    }
  }
}

static void process_asset_loads(void);

static u32
asset_loader_thread(void*)
{
  while (!g_AssetLoader->kill)
  {
    process_asset_loads();
    submit_gpu_stream_requests();
  }

  return 0;
}

void
init_asset_loader(void)
{
  init_gpu_stream_device();

  ASSERT_MSG_FATAL(g_GpuStreamDevice != nullptr, "Gpu Stream Device not initialized!");
  if (g_AssetLoader == nullptr)
  {
    g_AssetLoader = HEAP_ALLOC(AssetLoader, g_InitHeap, 1);
  }

  zero_memory(g_AssetLoader, sizeof(AssetLoader));

  g_AssetLoader->requests  = init_ring_queue<AssetId>(g_InitHeap, kMaxAssetLoadRequests);
  g_AssetLoader->assets    = init_hash_table<AssetId, AssetDesc>(g_InitHeap, kMaxAssets);

  g_AssetLoader->allocator = init_linear_allocator(HEAP_ALLOC_ALIGNED(g_InitHeap, kAssetAllocatorSize, alignof(u64)), kAssetAllocatorSize);
  // TODO(bshihabi): This is pretty dumb. We should probably defer this to TLSF too
  OSAllocator os_allocator = init_os_allocator();

  g_AssetLoader->thread    = init_thread(
    g_AssetLoader->allocator,
    os_allocator,
    kAssetThreadStackSize,
    &asset_loader_thread,
    nullptr,
    7
  );
  g_AssetLoader->wake_cond = init_thread_signal();

  set_thread_name(&g_AssetLoader->thread, L"Asset Streaming Thread");
}

static void
kick_asset_load(HashTable<AssetId, AssetDesc>* assets, AssetId asset_id)
{
  if (asset_id == kNullAssetId)
  {
    return;
  }

  AssetDesc* desc = hash_table_insert(assets, asset_id);

  // Load is already in progress
  if (desc->state > kAssetUnloaded)
  {
    return;
  }

  desc->state = kAssetLoadRequested;

  ACQUIRE(&g_AssetLoader->requests, auto* requests)
  {
    ring_queue_push(requests, asset_id);
  };

  notify_all_thread_signal(&g_AssetLoader->wake_cond);
}

void
kick_asset_load(AssetId asset_id)
{
  if (asset_id == kNullAssetId)
  {
    return;
  }
  ACQUIRE(&g_AssetLoader->assets, auto* assets)
  {
    kick_asset_load(assets, asset_id);
  };
  ACQUIRE(&g_AssetLoader->requests, auto* requests)
  {
    ring_queue_push(requests, asset_id);
  };

  notify_all_thread_signal(&g_AssetLoader->wake_cond);
}

void
process_asset_loads(void)
{
  while (!ring_queue_is_empty(g_GpuStreamDevice->asset_completed_streams))
  {
    AssetId asset_id = kNullAssetId;
    ring_queue_pop(&g_GpuStreamDevice->asset_completed_streams, &asset_id);

    if (asset_id == kNullAssetId)
    {
      continue;
    }

    ACQUIRE(&g_AssetLoader->assets, auto* assets)
    {
      AssetDesc* desc = hash_table_find(assets, asset_id);
      if (desc == nullptr)
      {
        return;
      }

      // TODO(bshihabi): We should also do initialization here if we need to
      dbgln("Asset 0x%x Ready", asset_id);

      desc->state = kAssetReady;
    };
  }

  // This will exit when there are no more requests
  for (;;)
  {
    AssetId asset_id = kNullAssetId;
    ACQUIRE(&g_AssetLoader->requests, auto* requests)
    {
      if (ring_queue_is_empty(*requests))
      {
        return;
      }
      ring_queue_pop(requests, &asset_id);
    };

    if (asset_id == kNullAssetId)
    {
      break;
    }

    ACQUIRE(&g_AssetLoader->assets, auto* assets)
    {
      dbgln("Requested load for 0x%x", asset_id);

      AssetDesc* desc = hash_table_find(assets, asset_id);
      ASSERT_MSG_FATAL(desc        != nullptr,             "Asset loader is in a bad state!");
      if (desc->state >= kAssetStreaming)
      {
        return;
      }

      auto file = open_built_asset_file(asset_id);
      if (!file)
      {
        FileError err = file.error();
        dbgln("Failed to load asset 0x%x: %s", asset_id, file_error_to_str(err));
        desc->state = kAssetFailedToLoad;
        return;
      }
      defer { close_file(&file.value()); };

      AssetMetadata metadata = {0};
      bool read_result = read_file(file.value(), &metadata, sizeof(metadata), 0);
      if (!read_result)
      {
        dbgln("Failed to read from asset file 0x%x", asset_id);
        desc->state = kAssetFailedToLoad;
        return;
      }

      if (metadata.magic_number != kAssetMagicNumber)
      {
        dbgln("Asset 0x%x is corrupted (invalid magic number 0x%x)", asset_id, metadata.magic_number);
        desc->state = kAssetFailedToLoad;
        return;
      }

      if (metadata.asset_hash != asset_id)
      {
        dbgln("Asset 0x%x is corrupted (metadata.asset_hash does not match!)", asset_id, metadata.asset_hash);
        desc->state = kAssetFailedToLoad;
        return;
      }

      switch(metadata.asset_type)
      {
        case AssetType::kModel:
        {
          ASSERT_MSG_FATAL(metadata.version == kModelAssetVersion, "Model built version does not match! Make sure you run the builder on the assets to make sure that the versions match.");

          desc->type = AssetType::kModel;

          ModelAsset model_asset = {0};
          model_asset.metadata = metadata;
          read_result = read_file(file.value(), (u8*)&model_asset, sizeof(model_asset), 0);
          if (!read_result)
          {
            dbgln("Failed to read from asset file 0x%x", asset_id);
            desc->state = kAssetFailedToLoad;
            return;
          }


          ScratchAllocator scratch = alloc_scratch_arena();
          defer { free_scratch_arena(&scratch); };

          ModelAsset::ModelSubset* subsets = HEAP_ALLOC(ModelAsset::ModelSubset, scratch, model_asset.num_model_subsets);
          read_result = read_file(file.value(), subsets, sizeof(ModelAsset::ModelSubset) * model_asset.num_model_subsets, model_asset.model_subsets);
          if (!read_result)
          {
            dbgln("Failed to read from asset file 0x%x", asset_id);
            desc->state = kAssetFailedToLoad;
            return;
          }

          for (u32 isubset = 0; isubset < model_asset.num_model_subsets; isubset++)
          {
            kick_asset_load(assets, subsets[isubset].material);
          }

        } break;
        case AssetType::kTexture:
        {
          ASSERT_MSG_FATAL(metadata.version == kTextureAssetVersion, "Texture built version does not match! Make sure you run the builder on the assets to make sure that the versions match.");
          desc->type = AssetType::kTexture;

          TextureAsset texture_asset = {0};
          texture_asset.metadata = metadata;

          {
            read_result = read_file(file.value(), (u8*)&texture_asset, sizeof(texture_asset), 0);
            defer { close_file(&file.value()); };
            if (!read_result)
            {
              dbgln("Failed to read from asset file 0x%x", asset_id);
              desc->state = kAssetFailedToLoad;
              return;
            }
          }

          GpuTextureDesc gpu_desc = {0};
          gpu_desc.width          = texture_asset.width;
          gpu_desc.height         = texture_asset.height;
          gpu_desc.array_size     = 1;
          gpu_desc.initial_state  = D3D12_RESOURCE_STATE_COMMON;
          gpu_desc.format         = kGpuFormatRGBA8Unorm;

          char name[512];
          snprintf(name, sizeof(name), "Streamed Texture 0x%x", asset_id);
          desc->texture.allocation = alloc_gpu_texture_no_heap(g_GpuDevice, gpu_desc, name);
          desc->texture.descriptor = alloc_descriptor(g_DescriptorCbvSrvUavPool);

          GpuTextureSrvDesc srv_desc = {0};
          srv_desc.mip_levels        = 1;
          srv_desc.most_detailed_mip = 0;
          srv_desc.array_size        = 1;
          srv_desc.format            = kGpuFormatRGBA8Unorm;
          init_texture_srv(&desc->texture.descriptor, &desc->texture.allocation, srv_desc);

          AssetGpuLoadRequest request = {0};
          request.asset_id          = asset_id;
          request.type              = kAssetGpuLoadTypeTexture;
          request.src_offset        = texture_asset.data;
          request.compressed_size   = texture_asset.compressed_size;
          request.uncompressed_size = texture_asset.uncompressed_size;
          request.texture.dst       = &desc->texture.allocation;

          GpuStreamResult result = request_gpu_stream_asset(request);
          if (result == kGpuStreamFailedToOpenFile)
          {
            dbgln("Failed to stream asset 0x%x (could not open file)", asset_id);
            desc->state = kAssetFailedToLoad;

            free_gpu_texture(&desc->texture.allocation);
            return;
          }

          ASSERT_MSG_FATAL(result == kGpuStreamOk, "Unhandled GPU stream error!");
          desc->state = kAssetStreaming;
        } break;
        case AssetType::kShader:
        {
          desc->type = AssetType::kShader;
          // TODO(bshihabi): Implement
          UNREACHABLE;
        } break;
        case AssetType::kMaterial:
        {
          ASSERT_MSG_FATAL(metadata.version == kMaterialAssetVersion, "Material built version does not match! Make sure you run the builder on the assets to make sure that the versions match.");
          desc->type = AssetType::kMaterial;

          MaterialAsset material_asset = {0};
          material_asset.metadata = metadata;
          read_result = read_file(file.value(), (u8*)&material_asset, sizeof(material_asset), 0);
          if (!read_result)
          {
            dbgln("Failed to read from asset file 0x%x", asset_id);
            desc->state = kAssetFailedToLoad;
            return;
          }

          desc->material.textures = init_array_zeroed<AssetId>(g_AssetLoader->allocator, material_asset.num_textures);

          read_result = read_file(file.value(), desc->material.textures.memory, sizeof(AssetId) * material_asset.num_textures, material_asset.textures);
          if (!read_result)
          {
            dbgln("Failed to read from asset file 0x%x", asset_id);
            desc->state = kAssetFailedToLoad;
            return;
          }

          for (u32 itexture = 0; itexture < material_asset.num_textures; itexture++)
          {
            kick_asset_load(assets, desc->material.textures[itexture]);
          }
          desc->state = kAssetReady;
        } break;
        default:
        {
          dbgln("Asset 0x%x is corrupted (invalid asset type 0x%x)", asset_id, metadata.asset_type);
          desc->state = kAssetFailedToLoad;
          return;
        } break;
      }
    };
  }
}

void
destroy_asset_loader(void)
{
  g_AssetLoader->kill = true;
  notify_all_thread_signal(&g_AssetLoader->wake_cond);
  join_threads(&g_AssetLoader->thread, 1);
  destroy_gpu_stream_device();
}

Result<const GpuTexture*, AssetState>
get_gpu_texture_asset(AssetId asset_id)
{
  return ACQUIRE(&g_AssetLoader->assets, auto* assets) -> Result<const GpuTexture*, AssetState>
  {
    AssetDesc* desc = hash_table_find(assets, asset_id);
    if (desc == nullptr)
    {
      return Err(kAssetUnloaded);
    }

    if (desc->state >= kAssetFailedToLoad || desc->state < kAssetReady)
    {
      return Err(desc->state);
    }

    ASSERT_MSG_FATAL(desc->type  == AssetType::kTexture, "Asset is not a texture!");
    ASSERT_MSG_FATAL(desc->state == kAssetReady,         "Asset has not loaded yet!");

    return Ok((const GpuTexture*)&desc->texture.allocation);
  };
}

Result<Texture2DPtr<float4>, AssetState>
get_srv_texture_asset(AssetId asset_id)
{
  return ACQUIRE(&g_AssetLoader->assets, auto* assets) -> Result<Texture2DPtr<float4>, AssetState>
  {
    AssetDesc* desc = hash_table_find(assets, asset_id);
    if (desc == nullptr)
    {
      return Err(kAssetUnloaded);
    }

    if (desc->state >= kAssetFailedToLoad || desc->state < kAssetReady)
    {
      return Err(desc->state);
    }

    ASSERT_MSG_FATAL(desc->type  == AssetType::kTexture, "Asset is not a texture!");
    ASSERT_MSG_FATAL(desc->state == kAssetReady,         "Asset has not loaded yet!");

    Texture2DPtr<float4> ptr;
    ptr.m_Index = desc->texture.descriptor.index;

    return Ok(ptr);
  };
}

Result<const MaterialData*, AssetState>
get_material_asset(AssetId asset_id)
{
  return ACQUIRE(&g_AssetLoader->assets, auto* assets) -> Result<const MaterialData*, AssetState>
  {
    AssetDesc* desc = hash_table_find(assets, asset_id);
    if (desc == nullptr)
    {
      return Err(kAssetUnloaded);
    }

    if (desc->state >= kAssetFailedToLoad || desc->state < kAssetReady)
    {
      return Err(desc->state);
    }

    ASSERT_MSG_FATAL(desc->type == AssetType::kMaterial,  "Asset is not a texture!");
    ASSERT_MSG_FATAL(desc->state == kAssetReady,          "Asset has not loaded yet!");

    return Ok((const MaterialData*)&desc->material);
  };
}
