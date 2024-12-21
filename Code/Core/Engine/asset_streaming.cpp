#include "Core/Foundation/types.h"

#include "Core/Engine/memory.h"
#include "Core/Engine/asset_streaming.h"

#include "Core/Engine/Vendor/DirectStorage/dstorage.h"
#include "Core/Engine/Vendor/DirectStorage/dstorageerr.h"


AssetLoader*     g_AssetLoader     = nullptr;
GpuStreamDevice* g_GpuStreamDevice = nullptr;

static constexpr u32 kStagingBufferMemory  = MiB(256);
static constexpr u32 kMaxAssetLoadRequests = 4000;
static constexpr u32 kMaxAssets            = 8000;

void
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

  DSTORAGE_QUEUE_DESC queue_desc{};
  queue_desc.Capacity   = DSTORAGE_MAX_QUEUE_CAPACITY;
  queue_desc.Priority   = DSTORAGE_PRIORITY_NORMAL;
  queue_desc.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
  queue_desc.Device     = g_GpuDevice->d3d12;
  queue_desc.Name       = "AssetLoader File Queue";

  HASSERT(
    g_GpuStreamDevice->factory->CreateQueue(&queue_desc, IID_PPV_ARGS(&g_GpuStreamDevice->file_queue))
  );

  g_GpuStreamDevice->file_queue_fence = init_gpu_fence();
}

void
destroy_asset_loader(void)
{
  g_GpuStreamDevice->file_queue->Close();

  destroy_gpu_fence(&g_GpuStreamDevice->file_queue_fence);
  COM_RELEASE(g_GpuStreamDevice->factory);
  COM_RELEASE(g_GpuStreamDevice->file_queue);

  zero_memory(g_GpuStreamDevice, sizeof(GpuStreamDevice));
}

AssetLoadResult
request_gpu_stream_asset(const AssetGpuLoadRequest& request)
{
  ASSERT_MSG_FATAL(g_AssetLoader != nullptr, "AssetLoader not initialized!");

  // TODO(bshihabi): Use this https://johnnylee-sde.github.io/Fast-unsigned-integer-to-hex-string/
  wchar_t path[512];
  swprintf(path, 512, L"Assets/Built/0x%08x.built", request.asset_id);

  IDStorageFile* file = nullptr;
  HRESULT res = g_AssetLoader->factory->OpenFile(path, IID_PPV_ARGS(&file));
  if (FAILED(res))
  {
    return kAssetLoadFailedToOpenFile;
  }

  defer { COM_RELEASE(file); };

  DSTORAGE_REQUEST drequest;
  drequest.Options.SourceType      = DSTORAGE_REQUEST_SOURCE_FILE;
  drequest.Source.File.Source      = file;
  drequest.Source.File.Offset      = request.src_offset;
  drequest.Source.File.Size        = request.compressed_size;
  drequest.UncompressedSize        = request.uncompressed_size;
  drequest.CancellationTag         = request.asset_id;

  switch (request.type)
  {
    case kAssetGpuLoadTypeBuffer:
    {
      drequest.Options.DestinationType     = DSTORAGE_REQUEST_DESTINATION_BUFFER;
      drequest.Options.DestinationType     = DSTORAGE_REQUEST_DESTINATION_BUFFER;
      drequest.Destination.Buffer.Resource = request.buffer.dst->d3d12_buffer;
      drequest.Destination.Buffer.Offset   = request.buffer.dst_offset;
      drequest.Destination.Buffer.Size     = request.uncompressed_size;
    } break;
    case kAssetGpuLoadTypeTexture:
    {
      drequest.Options.DestinationType              = DSTORAGE_REQUEST_DESTINATION_TEXTURE_REGION;
      drequest.Destination.Texture.Resource         = request.texture.dst->d3d12_texture;
      drequest.Destination.Texture.SubresourceIndex = 0;
      drequest.Destination.Texture.Region.left      = 0;
      drequest.Destination.Texture.Region.top       = 0;
      drequest.Destination.Texture.Region.front     = 0;
      drequest.Destination.Texture.Region.right     = request.texture.dst->desc.width;
      drequest.Destination.Texture.Region.bottom    = request.texture.dst->desc.height;
      drequest.Destination.Texture.Region.back      = 1;
    } break;
  }

  g_GpuStreamDevice->file_queue->EnqueueRequest(&drequest);
  g_GpuStreamDevice->file_queue->EnqueueSignal(
    g_GpuStreamDevice->file_queue_fence.d3d12_fence,
    ++g_GpuStreamDevice->file_queue_fence.value
  );

  return kAssetLoadOk;
}


void
submit_gpu_stream_requests(void)
{
  g_GpuStreamDevice->file_queue->Submit();
}


void
init_asset_loader(void)
{
  ASSERT_MSG_FATAL(g_GpuStreamDevice != nullptr, "Gpu Stream Device not initialized!");
  if (g_AssetLoader == nullptr)
  {
    g_AssetLoader = HEAP_ALLOC(AssetLoader, g_InitHeap, 1);
  }

  zero_memory(g_AssetLoader, sizeof(AssetLoader));

  g_AssetLoader->requests     = init_ring_queue<AssetId>(g_InitHeap, kMaxAssetLoadRequests);
  g_AssetLoader->asset_states = init_hash_table<AssetId, AssetState>(g_InitHeap, kMaxAssets);
}

void
load_asset(AssetId asset_id)
{
  AssetState state = hash_table_insert(&g_AssetLoader->asset_states, asset_id);

  // Load is already in progress
  if (state > kAssetUnloaded)
  {
    return;
  }

  *state = kAssetLoadRequested;
  ring_queue_push(&g_AssetLoader->requests, asset_id);
}

void
process_asset_loads(void)
{
  while (!ring_queue_is_empty(&g_AssetLoader->requests))
  {
    AssetId asset_id = 0;
    ring_queue_pop(&g_AssetLoader->requests, &asset_id);

    auto file = open_built_asset_file(asset_id);
    if (!file)
    {
      FileError err = file.error();
      dbgln("%s", file_error_to_str(err));
    }
  }
}

void
destroy_asset_loader(void)
{
}

