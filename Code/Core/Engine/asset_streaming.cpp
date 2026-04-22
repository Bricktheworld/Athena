#include "Core/Foundation/types.h"
#include "Core/Foundation/profiling.h"

#include "Core/Foundation/Containers/push_buffer.h"
#include "Core/Foundation/bit_allocator.h"

#include "Core/Engine/memory.h"
#include "Core/Engine/asset_streaming.h"
#include "Core/Engine/Render/renderer.h"

#include "Core/Engine/Vendor/DirectStorage/dstorage.h"
#include "Core/Engine/Vendor/DirectStorage/dstorageerr.h"

struct AssetStreamRequest
{
  AssetType asset_type = AssetType::kUnknown;
  AssetId   asset_id   = kNullAssetId;
};
enum StreamingCmd : u32
{
  // 
  kNullStreamingCmd = 0,


  // Model cmds
  kModelCpuStreamHeader,
  kModelCpuStreamContent,
  kModelGpuStreamContent,
  kModelCmdEnd,           // Leave this at the end here, so that we can determine streaming cmd type

  // Material cmds
  kMaterialCpuStreamHeader,
  kMaterialCpuStreamContent,
  kMaterialStreamDependencies,
  kMaterialGpuStreamContent,
  kMaterialCmdEnd,        // Leave this at the end here, so that we can determine streaming cmd type

  // Texture cmds
  kTextureCpuStreamHeader,
  kTextureCpuStreamContent,
  kTextureGpuStreamContent,
  kTextureCmdEnd,         // Leave this at the end here, so that we can determine streaming cmd type
};

struct FileStreamingCmdHeader
{
  StreamingCmd     cmd              = kNullStreamingCmd;
  AsyncFilePromise file_promise;    
};

struct GpuStreamingCmdHeader        
{                                   
  StreamingCmd     cmd              = kNullStreamingCmd;
  FenceValue       gpu_fence_value  = 0;
};

struct MainThreadCmdHeader
{
  StreamingCmd     cmd              = kNullStreamingCmd;
};

struct AssetDependencyCmdHeader
{
  StreamingCmd     cmd              = kNullStreamingCmd;
  u32              dependency_count = 0;
  const Asset**    dependencies     = nullptr;
};

struct AssetStreamer
{
  SpinLocked<RingQueue<AssetStreamRequest>> asset_stream_requests;

  PushBuffer                                file_io_buffer;
  PushBuffer                                gpu_io_buffer;
  PushBuffer                                asset_dependency_queue;

  // Asset Streaming Thread (producer) -> Main Thread (consumer)
  //   Use this for any work that isn't thread safe. It will be done at the beginning of the frame.
  PushBuffer                                main_thread_cmd_queue;

  // Caches the next in queue file I/O and Gpu commands
  FileStreamingCmdHeader                    next_file_io_cmd;
  GpuStreamingCmdHeader                     next_gpu_cmd;
  AssetDependencyCmdHeader                  next_asset_dependency_cmd;

  GpuRingBuffer                             gpu_staging_buffer;
  GpuBuffer                                 gpu_scratch_buffer;

  CmdList                                   gpu_cmd_buffer;
  CmdListAllocator                          gpu_cmd_buffer_allocator;

  Thread                                    thread;

  // Used to allocate metadata for models/materials. Not sure what the best allocator is for this with _no_ fragmentation
  LinearAllocator                           metadata_allocator;

  alignas(kCacheLineSize) Atomic<u64>       kill            = 0;
};

static FenceValue
flush_gpu_cmds(AssetStreamer* streamer)
{
  FenceValue ret = submit_cmd_lists(&streamer->gpu_cmd_buffer_allocator, {streamer->gpu_cmd_buffer});
  gpu_ring_buffer_commit(&streamer->gpu_staging_buffer, &streamer->gpu_cmd_buffer_allocator);

  streamer->gpu_cmd_buffer = alloc_cmd_list(&streamer->gpu_cmd_buffer_allocator);

  return ret;
}

static u64
alloc_gpu_staging_bytes_blocking(AssetStreamer* streamer, u32 size)
{
  while (true)
  {
    Result<u64, FenceValue> ret = gpu_ring_buffer_alloc(&streamer->gpu_staging_buffer, size);
    if (ret)
    {
      return ret.value();
    }

    // Flush and wait for the GPU to catch up
    flush_gpu_cmds(streamer);
    gpu_ring_buffer_wait(&streamer->gpu_staging_buffer, size);
  }
}

struct ModelRegistry
{
  // TODO(bshihabi): Use TLSF allocator here (or page allocator)
  LinearAllocator                       allocator;
  SpinLocked<HashTable<AssetId, Model>> asset_map;
};

struct MaterialRegistry
{
  SpinLocked<HashTable<AssetId, Material>> asset_map;

  // Used to allocate material slots on the Gpu
  BitAllocator                             gpu_material_slot_allocator;
  GpuBuffer                                gpu_material_buffer;
};

struct TextureRegistry
{
  // TODO(bshihabi): Use TLSF allocator here (or page allocator)
  LinearAllocator                         allocator;
  SpinLocked<HashTable<AssetId, Texture>> asset_map;
};

struct AssetRegistry
{
  ModelRegistry    model_registry;
  MaterialRegistry material_registry;
  TextureRegistry  texture_registry;
};

AssetStreamer* g_AssetStreamer = nullptr;
AssetRegistry* g_AssetRegistry = nullptr;

// Only block on the file I/O thread 
static constexpr u32 kFileIOBlockRateMs = 2;

//////////////////////////////
//     Model Streaming      //
//////////////////////////////
static ModelRegistry
init_model_registry(void)
{
  size_t kModelManagerSize = KiB(8);
  void*  model_manager_mem = HEAP_ALLOC_ALIGNED(g_InitHeap, kModelManagerSize, 16);

  ModelRegistry ret;
  ret.allocator  = init_linear_allocator(model_manager_mem, kModelManagerSize);
  ret.asset_map  = init_hash_table<AssetId, Model>(g_InitHeap, kMaxAssets);
  return ret;
}

// Used for streaming stuff from files
struct ModelFileHeaderStreamingPacket
{
  Model*             model = nullptr;
  // Pop this off of the CPU scratch buffer to get the streaming packet
  u64                size  = 0;
  AsyncFileStream    file_stream;

  // The header for the asset filled by the file I/O request
  ModelAsset         asset_header;
};

// Used for streaming stuff from files
struct ModelFileContentStreamingPacket
{
  Model*           model = nullptr;
  u64              size  = 0;
  void*            buf   = nullptr;
  ModelAsset       asset_header;
  AsyncFileStream  file_stream;
};

struct ModelGpuContentStreamingPacket
{
  Model*           model = nullptr;
  ModelAsset       asset_header;
};

static void
kick_model_load(ModelRegistry* registry, AssetStreamer* streamer, AssetId asset_id)
{
  Model* model = nullptr;
  ACQUIRE(&registry->asset_map, auto* asset_map)
  {
    model = hash_table_find(asset_map, asset_id);
    ASSERT_MSG_FATAL(model != nullptr, "Model 0x%x was not initialized in the ModelRegistry! This indicates that there was some out-of-order event that occurred as this is responsible for the calling kick_model_load before the asset streaming thread gets it.", asset_id);

    // Validate it if we already have the metadata
    ASSERT_MSG_FATAL(model->asset.id   == asset_id,          "Possible hash table collision in the ModelRegistry! Expected model to have asset ID 0x%llx but found 0x%llx", asset_id, model->asset.id);
    ASSERT_MSG_FATAL(model->asset.type == AssetType::kModel, "ModelRegistry is in a bad state, found non-model asset in the asset map with type %u", model->asset.type);
  };

  // Add the model to the load queue if no one has requested it to be loaded yet
  if (InterlockedCompareExchange(&model->asset.state, kAssetLoadRequested, kAssetUnloaded) == kAssetUnloaded)
  {
    // Open the built asset file
    char asset_path[kAssetPathSize];
    asset_id_to_path(asset_path, asset_id);
    Result<AsyncFileStream, FileError> file_open_ok = open_file_async(asset_path, kFileStreamRead);
    if (!file_open_ok)
    {
      dbgln("Failed to open file for asset 0x%x.", asset_id);
      model->asset.state = kAssetFailedToLoad;
      return;
    }

    // Allocate some scratch memory in the ring buffer to read the file data
    u64   scratch_size   = sizeof(FileStreamingCmdHeader)         +
                           sizeof(ModelFileHeaderStreamingPacket);
    void* file_io_memory = push_buffer_begin_edit(&streamer->file_io_buffer, scratch_size);
    defer { push_buffer_end_edit(&streamer->file_io_buffer, file_io_memory); };

    void* scratch_memory = file_io_memory;

    // Allocate the command header and packet data
    FileStreamingCmdHeader*         header = (FileStreamingCmdHeader*        )ALLOC_OFF(scratch_memory, sizeof(FileStreamingCmdHeader));
    ModelFileHeaderStreamingPacket* pkt    = (ModelFileHeaderStreamingPacket*)ALLOC_OFF(scratch_memory, sizeof(ModelFileHeaderStreamingPacket));

    // Initialize all of the packet data
    header->cmd          = kModelCpuStreamHeader;
    header->file_promise = kAsyncFileError;
    pkt->model           = model;
    pkt->size            = sizeof(ModelAsset);
    pkt->file_stream     = file_open_ok.value();

    // If the file read fails, then we already allocated the memory so we can't abort now, we just mark the file promise as failed and the consumer will ignore the packet.
    Result<void, FileError> file_read_ok = read_file(pkt->file_stream, &header->file_promise, &pkt->asset_header, pkt->size, 0);
    if (!file_read_ok)
    {
      dbgln("Failed to read file for asset 0x%x.", asset_id);
      model->asset.state   = kAssetFailedToLoad;
      return;
    }
  }
}


static void
process_model_file_request(AssetStreamer* streamer, FileStreamingCmdHeader header, AwaitError await_result)
{
  switch (header.cmd)
  {
    // Streaming in of the header
    case kModelCpuStreamHeader:
    {
      // Pop the rest of the packet off of the queue
      ModelFileHeaderStreamingPacket src_pkt;
      push_buffer_pop(&streamer->file_io_buffer, &src_pkt, sizeof(src_pkt));

      Model*  model    = src_pkt.model;
      AssetId asset_id = model->asset.id;

      ASSERT_MSG_FATAL(await_result != kAwaitInFlight, "In flight requests should be handled earlier up the call stack, something went wrong in the asset streamer.");
      if (await_result == kAwaitFailed)
      {
        // If the packet failed, ignore it and mark the model as failed to load.
        model->asset.state = kAssetFailedToLoad;
        return;
      }
      else if (await_result == kAwaitInFlight)
      {
        return;
      }

      // A bunch of validation checks for the model data
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.magic_number == kAssetMagicNumber,  "Model header data is corrupted for asset 0x%x. Expected magic number 0x%x but got 0x%x", asset_id, kAssetMagicNumber, src_pkt.asset_header.metadata.magic_number);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.asset_hash   == asset_id,           "Model header data is corrupted for asset 0x%x. Expected asset ID 0x%x but got 0x%x",     asset_id, asset_id,          src_pkt.asset_header.metadata.asset_hash);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.asset_type   == AssetType::kModel,  "Model header data is corrupted for asset 0x%x. Expected asset type 0x%x but got 0x%x",   asset_id, AssetType::kModel, src_pkt.asset_header.metadata.asset_type);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.version      == kModelAssetVersion, "Model asset version for asset 0x%x mismatched. Expected version 0x%x but got 0x%x. Please run the asset builder on this asset.", asset_id, kModelAssetVersion, src_pkt.asset_header.metadata.version);

      bool valid_data = src_pkt.asset_header.metadata.magic_number == kAssetMagicNumber &&
                        src_pkt.asset_header.metadata.asset_hash   == asset_id          &&
                        src_pkt.asset_header.metadata.asset_type   == AssetType::kModel &&
                        src_pkt.asset_header.metadata.version      == kModelAssetVersion;
      if (!valid_data)
      {
        dbgln("Skipping corrupted asset 0x%x", asset_id);
        model->asset.state = kAssetFailedToLoad;
        return;
      }

      // Update the model with the correct number of model subsets and allocate the arrays
      model->subsets          = init_array<ModelSubset   >(streamer->metadata_allocator, src_pkt.asset_header.num_model_subsets);
      model->subset_rt_blases = init_array<GpuRtBlas     >(streamer->metadata_allocator, src_pkt.asset_header.num_model_subsets);
      model->materials        = init_array<MaterialHandle>(streamer->metadata_allocator, src_pkt.asset_header.num_model_subsets);

      // Bytes to read from the asset file for the content
      u64   read_size    = src_pkt.asset_header.num_model_subsets * sizeof(ModelAsset::ModelSubset) +
                           src_pkt.asset_header.vertices_size                                       +
                           src_pkt.asset_header.indices_size;

      // Allocate some scratch memory in the ring buffer to read the file data
      u64   scratch_size = sizeof(FileStreamingCmdHeader)          +
                           sizeof(ModelFileContentStreamingPacket) +
                           read_size;

      void* file_io_memory = push_buffer_begin_edit(&streamer->file_io_buffer, scratch_size);
      defer { push_buffer_end_edit(&streamer->file_io_buffer, file_io_memory); };

      // Initialize the packets to push to the queue
      void* scratch_memory = file_io_memory;

      auto* dst_header         = (FileStreamingCmdHeader*         )ALLOC_OFF(scratch_memory, sizeof(FileStreamingCmdHeader));
      dst_header->cmd          = kModelCpuStreamContent;
      dst_header->file_promise = {0};

      auto* dst_pkt            = (ModelFileContentStreamingPacket*)ALLOC_OFF(scratch_memory, sizeof(ModelFileContentStreamingPacket));
      dst_pkt->model           = model;
      dst_pkt->file_stream     = src_pkt.file_stream;
      dst_pkt->asset_header    = src_pkt.asset_header;
      dst_pkt->buf             = ALLOC_OFF(scratch_memory, read_size);
      dst_pkt->size            = read_size;

      Result<void, FileError> stream_ok = read_file(dst_pkt->file_stream, &dst_header->file_promise, dst_pkt->buf, dst_pkt->size, src_pkt.size);
      if (!stream_ok)
      {
        dbgln("Failed to stream asset 0x%x. File read failed.", asset_id);
        model->asset.state       = kAssetFailedToLoad;
        return;
      }
    } break;
    case kModelCpuStreamContent:
    {
      ModelFileContentStreamingPacket src_pkt;
      push_buffer_pop(&streamer->file_io_buffer, &src_pkt, sizeof(src_pkt));

      // !!! WARNING !!!
      //
      // The first sizeof(ModelAsset) bytes of this pointer are invalid.
      // 
      // The pointer arithmetic simply makes more sense if I subtract off the ModelAsset header which has 
      // already been read in. This way, the offset pointers are just added to this base pointer.
      //
      // Read from this pointer with care.
      //
      // !!! WARNING !!!
      u8*     buf      = (u8*)src_pkt.buf - sizeof(ModelAsset);

      Model*  model    = src_pkt.model;
      AssetId asset_id = model->asset.id;

      model->asset.state = kAssetStreaming;

      // A bunch of validation checks for the model data mostly to prevent against buffer overrun bugs.
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.magic_number == kAssetMagicNumber,  "Model header data is corrupted for asset 0x%x. Expected magic number 0x%x but got 0x%x", asset_id, kAssetMagicNumber, src_pkt.asset_header.metadata.magic_number);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.asset_hash   == asset_id,           "Model header data is corrupted for asset 0x%x. Expected asset ID 0x%x but got 0x%x",     asset_id, asset_id,          src_pkt.asset_header.metadata.asset_hash);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.asset_type   == AssetType::kModel,  "Model header data is corrupted for asset 0x%x. Expected asset type 0x%x but got 0x%x",   asset_id, AssetType::kModel, src_pkt.asset_header.metadata.asset_type);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.version      == kModelAssetVersion, "Model asset version for asset 0x%x mismatched. Expected version 0x%x but got 0x%x. Please run the asset builder on this asset.", asset_id, kModelAssetVersion, src_pkt.asset_header.metadata.version);

      bool valid_data = src_pkt.asset_header.metadata.magic_number == kAssetMagicNumber &&
                        src_pkt.asset_header.metadata.asset_hash   == asset_id          &&
                        src_pkt.asset_header.metadata.asset_type   == AssetType::kModel &&
                        src_pkt.asset_header.metadata.version      == kModelAssetVersion;
      if (!valid_data)
      {
        dbgln("Skipping corrupted asset 0x%x", asset_id);
        model->asset.state = kAssetFailedToLoad;
        return;
      }

      // Now that we know there's not some weird corruption happening, we can safely pop off the rest of the packet
      defer { push_buffer_pop(&streamer->file_io_buffer, src_pkt.size); };

      ASSERT_MSG_FATAL(await_result != kAwaitInFlight, "In flight requests should be handled earlier up the call stack, something went wrong in the asset streamer.");
      if (await_result == kAwaitFailed)
      {
        // If the packet failed, ignore it and mark the model as failed to load.
        model->asset.state = kAssetFailedToLoad;
        return;
      }
      else if (await_result == kAwaitInFlight)
      {
        return;
      }

      // Initialize all the model subsets in the metadata
      ModelAsset::ModelSubset* asset_subset    = (ModelAsset::ModelSubset*)(buf + src_pkt.asset_header.model_subsets);
      for (u32 isubset = 0; isubset < src_pkt.asset_header.num_model_subsets; isubset++, asset_subset++)
      {
        // Initialize the memory for the model subsets
        ModelSubset* runtime_subset    = array_add(&model->subsets);
        {
          runtime_subset->vertex_count = (u32)asset_subset->num_vertices;
          runtime_subset->index_count  = (u32)asset_subset->num_indices;

          u64 vertex_offset_bytes      = alloc_uber_vertex(asset_subset->num_vertices * sizeof(Vertex));
          u64 index_offset_bytes       = alloc_uber_index (asset_subset->num_indices  * sizeof(u16));
          runtime_subset->vertex_start = (u32)vertex_offset_bytes / sizeof(Vertex);
          runtime_subset->index_start  = (u32)index_offset_bytes  / sizeof(u16);

          // TODO(bshihabi): We need to add proper debug names for the BLASes
          GpuRtBlas* subset_rt_blas    = array_add(&model->subset_rt_blases);
          *subset_rt_blas              = alloc_uber_blas(runtime_subset->vertex_start, runtime_subset->vertex_count, runtime_subset->index_start, runtime_subset->index_count, "Content subset RT BLAS");
        }

        // Kick off the material loads
        {
          MaterialHandle* dst = array_add(&model->materials);
          *dst                = kick_material_load(asset_subset->material);
        }

        // Copy the data into the ring buffer
        {
          u32 subset_vertex_size_in_bytes = (u32)(sizeof(Vertex) * asset_subset->num_vertices);
          u32 subset_index_size_in_bytes  = (u32)(sizeof(u16)    * asset_subset->num_indices);

          u8* gpu_scratch_mapped_base     = (u8*)unwrap(streamer->gpu_staging_buffer.buffer.mapped);
          u8* gpu_scratch_mapped          = gpu_scratch_mapped_base + alloc_gpu_staging_bytes_blocking(streamer, subset_vertex_size_in_bytes + subset_index_size_in_bytes);

          u8* subset_vertex_staging       = ALLOC_OFF(gpu_scratch_mapped, subset_vertex_size_in_bytes);
          u8* subset_index_staging        = ALLOC_OFF(gpu_scratch_mapped, subset_index_size_in_bytes );
          memcpy(subset_vertex_staging, buf + asset_subset->vertices, subset_vertex_size_in_bytes);
          memcpy(subset_index_staging,  buf + asset_subset->indices,  subset_index_size_in_bytes );

          gpu_copy_buffer(
            &streamer->gpu_cmd_buffer,
            g_UnifiedGeometryBuffer.vertex_buffer,
            runtime_subset->vertex_start * sizeof(Vertex),
            streamer->gpu_staging_buffer.buffer,
            subset_vertex_staging - gpu_scratch_mapped_base,
            subset_vertex_size_in_bytes
          );

          gpu_copy_buffer(
            &streamer->gpu_cmd_buffer,
            g_UnifiedGeometryBuffer.index_buffer,
            runtime_subset->index_start * sizeof(u16),
            streamer->gpu_staging_buffer.buffer,
            subset_index_staging - gpu_scratch_mapped_base,
            subset_index_size_in_bytes
          );
        }
      }

      // Flush all the buffer copies for the vertex/index buffer
      gpu_memory_barrier(&streamer->gpu_cmd_buffer);

      for (u32 isubset = 0; isubset < src_pkt.asset_header.num_model_subsets; isubset++, asset_subset++)
      {
        const GpuRtBlas&   subset_blas = model->subset_rt_blases[isubset];

        // TODO(bshihabi): We should do all the copies first, then do all the BLAS building, that way we can just do one cache flush
        build_rt_blas(
          &streamer->gpu_cmd_buffer,
          subset_blas,
          streamer->gpu_scratch_buffer,
          0,
          g_UnifiedGeometryBuffer.index_buffer,
          g_UnifiedGeometryBuffer.vertex_buffer,
          0
        );
      }

      u64   scratch_size      = sizeof(GpuStreamingCmdHeader) + sizeof(ModelGpuContentStreamingPacket);
      void* gpu_stream_memory = push_buffer_begin_edit(&streamer->gpu_io_buffer, scratch_size);
      defer { push_buffer_end_edit(&streamer->gpu_io_buffer, gpu_stream_memory); };

      // Push the GPU content streaming packet to the queue
      void* scratch_memory        = gpu_stream_memory;

      auto* dst_header            = (GpuStreamingCmdHeader*         )ALLOC_OFF(scratch_memory, sizeof(GpuStreamingCmdHeader));
      dst_header->cmd             = kModelGpuStreamContent;
      dst_header->gpu_fence_value = flush_gpu_cmds(streamer);

      auto* dst_pkt               = (ModelGpuContentStreamingPacket*)ALLOC_OFF(scratch_memory, sizeof(ModelGpuContentStreamingPacket));
      dst_pkt->model              = model;
      dst_pkt->asset_header       = src_pkt.asset_header;
    } break;
    default: UNREACHABLE; break;
  }
}

static void
process_model_gpu_request(AssetStreamer* streamer, GpuStreamingCmdHeader header)
{
  switch (header.cmd)
  {
    // Streaming in of the header
    case kModelGpuStreamContent:
    {
      // Pop the rest of the packet off of the queue
      ModelGpuContentStreamingPacket src_pkt;
      push_buffer_pop(&streamer->gpu_io_buffer, &src_pkt, sizeof(src_pkt));

      Model*  model    = src_pkt.model;
      AssetId asset_id = model->asset.id;

      // A bunch of validation checks for the model data
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.magic_number == kAssetMagicNumber,  "Model header data is corrupted for asset 0x%x. Expected magic number 0x%x but got 0x%x", asset_id, kAssetMagicNumber, src_pkt.asset_header.metadata.magic_number);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.asset_hash   == asset_id,           "Model header data is corrupted for asset 0x%x. Expected asset ID 0x%x but got 0x%x",     asset_id, asset_id,          src_pkt.asset_header.metadata.asset_hash);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.asset_type   == AssetType::kModel,  "Model header data is corrupted for asset 0x%x. Expected asset type 0x%x but got 0x%x",   asset_id, AssetType::kModel, src_pkt.asset_header.metadata.asset_type);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.version      == kModelAssetVersion, "Model asset version for asset 0x%x mismatched. Expected version 0x%x but got 0x%x. Please run the asset builder on this asset.", asset_id, kModelAssetVersion, src_pkt.asset_header.metadata.version);

      bool valid_data = src_pkt.asset_header.metadata.magic_number == kAssetMagicNumber &&
                        src_pkt.asset_header.metadata.asset_hash   == asset_id          &&
                        src_pkt.asset_header.metadata.asset_type   == AssetType::kModel &&
                        src_pkt.asset_header.metadata.version      == kModelAssetVersion;
      if (!valid_data)
      {
        dbgln("Skipping corrupted asset 0x%x", asset_id);
        model->asset.state = kAssetFailedToLoad;
        return;
      }

      model->asset.state = kAssetReady;
    } break;
    default: UNREACHABLE; break;
  }
}

//////////////////////////////
//    Material Streaming    //
//////////////////////////////
static MaterialRegistry
init_material_registry(void)
{
  MaterialRegistry ret;
  ret.asset_map                   = init_hash_table<AssetId, Material>(g_InitHeap, kMaxAssets);
  ret.gpu_material_slot_allocator = init_bit_allocator(g_InitHeap, kMaxAssets);

  GpuBufferDesc desc = {0};
  desc.size = sizeof(MaterialGpu) * kMaxAssets;
  ret.gpu_material_buffer         = alloc_gpu_buffer_no_heap(g_GpuDevice, desc, kGpuHeapGpuOnly, "Material Buffer");
  return ret;
}

struct MaterialFileHeaderStreamingPacket
{
  Material*       material    = nullptr;
  u64             size        = 0;
  AsyncFileStream file_stream;
  MaterialAsset   asset_header;
};

struct MaterialFileContentStreamingPacket
{
  Material*       material    = nullptr;
  u64             size        = 0;
  void*           buf         = nullptr;
  MaterialAsset   asset_header;
  AsyncFileStream file_stream;
};

struct MaterialDependencyStreamingPacket
{
  Material*       material    = nullptr;
  MaterialAsset   asset_header;
};

struct MaterialGpuContentStreamingPacket
{
  Material*       material = nullptr;
  MaterialAsset   asset_header;
};

static void
kick_material_load(MaterialRegistry* registry, AssetStreamer* streamer, AssetId asset_id)
{
  Material* material = nullptr;
  ACQUIRE(&registry->asset_map, auto* asset_map)
  {
    material = hash_table_find(asset_map, asset_id);
    ASSERT_MSG_FATAL(material != nullptr, "Material 0x%x was not initialized in the MaterialRegistry! This indicates that there was some out-of-order event that occurred as this is responsible for the calling kick_material_load before the asset streaming thread gets it.", asset_id);

    ASSERT_MSG_FATAL(material->asset.id   == asset_id,             "Possible hash table collision in the MaterialRegistry! Expected material to have asset ID 0x%llx but found 0x%llx", asset_id, material->asset.id);
    ASSERT_MSG_FATAL(material->asset.type == AssetType::kMaterial, "MaterialRegistry is in a bad state, found non-material asset in the asset map with type %u", material->asset.type);
  };

  if (InterlockedCompareExchange(&material->asset.state, kAssetLoadRequested, kAssetUnloaded) == kAssetUnloaded)
  {
    char asset_path[kAssetPathSize];
    asset_id_to_path(asset_path, asset_id);
    Result<AsyncFileStream, FileError> file_open_ok = open_file_async(asset_path, kFileStreamRead);
    if (!file_open_ok)
    {
      dbgln("Failed to open file for asset 0x%x.", asset_id);
      material->asset.state = kAssetFailedToLoad;
      return;
    }

    Option<u32> gpu_id   = bit_alloc(&registry->gpu_material_slot_allocator);
    if (!gpu_id)
    {
      dbgln("Out of GPU material slots! Skipping streaming material 0x%x", asset_id);
      material->asset.state = kAssetFailedToLoad;
      return;
    }

    material->gpu_id     = unwrap_or(gpu_id, 0);

    u64   scratch_size   = sizeof(FileStreamingCmdHeader)            +
                           sizeof(MaterialFileHeaderStreamingPacket);
    void* file_io_memory = push_buffer_begin_edit(&streamer->file_io_buffer, scratch_size);
    defer { push_buffer_end_edit(&streamer->file_io_buffer, file_io_memory); };

    void* scratch_memory = file_io_memory;

    FileStreamingCmdHeader*            header = (FileStreamingCmdHeader*           )ALLOC_OFF(scratch_memory, sizeof(FileStreamingCmdHeader));
    MaterialFileHeaderStreamingPacket* pkt    = (MaterialFileHeaderStreamingPacket*)ALLOC_OFF(scratch_memory, sizeof(MaterialFileHeaderStreamingPacket));


    header->cmd          = kMaterialCpuStreamHeader;
    pkt->material        = material;
    pkt->size            = sizeof(MaterialAsset);
    pkt->file_stream     = file_open_ok.value();

    Result<void, FileError> file_read_ok = read_file(pkt->file_stream, &header->file_promise, &pkt->asset_header, pkt->size, 0);
    if (!file_read_ok)
    {
      dbgln("Failed to read file for asset 0x%x.", asset_id);
      material->asset.state = kAssetFailedToLoad;
      return;
    }
  }
}

static void
process_material_file_request(AssetStreamer* streamer, FileStreamingCmdHeader header, AwaitError await_result)
{
  switch (header.cmd)
  {
    case kMaterialCpuStreamHeader:
    {
      MaterialFileHeaderStreamingPacket src_pkt;
      push_buffer_pop(&streamer->file_io_buffer, &src_pkt, sizeof(src_pkt));

      Material* material = src_pkt.material;
      AssetId   asset_id = material->asset.id;

      ASSERT_MSG_FATAL(await_result != kAwaitInFlight, "In flight requests should be handled earlier up the call stack, something went wrong in the asset streamer.");
      if (await_result == kAwaitFailed)
      {
        material->asset.state = kAssetFailedToLoad;
        return;
      }
      else if (await_result == kAwaitInFlight)
      {
        return;
      }

      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.magic_number == kAssetMagicNumber,     "Material header data is corrupted for asset 0x%x. Expected magic number 0x%x but got 0x%x",    asset_id, kAssetMagicNumber,        src_pkt.asset_header.metadata.magic_number);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.asset_hash   == asset_id,              "Material header data is corrupted for asset 0x%x. Expected asset ID 0x%x but got 0x%x",         asset_id, asset_id,               src_pkt.asset_header.metadata.asset_hash);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.asset_type   == AssetType::kMaterial,  "Material header data is corrupted for asset 0x%x. Expected asset type 0x%x but got 0x%x",       asset_id, AssetType::kMaterial,   src_pkt.asset_header.metadata.asset_type);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.version      == kMaterialAssetVersion, "Material asset version for asset 0x%x mismatched. Expected version 0x%x but got 0x%x. Please run the asset builder on this asset.", asset_id, kMaterialAssetVersion, src_pkt.asset_header.metadata.version);

      bool valid_data = src_pkt.asset_header.metadata.magic_number == kAssetMagicNumber    &&
                        src_pkt.asset_header.metadata.asset_hash   == asset_id             &&
                        src_pkt.asset_header.metadata.asset_type   == AssetType::kMaterial &&
                        src_pkt.asset_header.metadata.version      == kMaterialAssetVersion;
      if (!valid_data)
      {
        dbgln("Skipping corrupted asset 0x%x", asset_id);
        material->asset.state = kAssetFailedToLoad;
        return;
      }

      u64   read_size    = src_pkt.asset_header.num_textures * sizeof(AssetRef<TextureAsset>);

      u64   scratch_size = sizeof(FileStreamingCmdHeader)             +
                           sizeof(MaterialFileContentStreamingPacket) +
                           read_size;

      void* file_io_memory = push_buffer_begin_edit(&streamer->file_io_buffer, scratch_size);
      defer { push_buffer_end_edit(&streamer->file_io_buffer, file_io_memory); };

      void* scratch_memory = file_io_memory;

      auto* dst_header         = (FileStreamingCmdHeader*            )ALLOC_OFF(scratch_memory, sizeof(FileStreamingCmdHeader));
      dst_header->cmd          = kMaterialCpuStreamContent;
      dst_header->file_promise = {0};

      auto* dst_pkt            = (MaterialFileContentStreamingPacket*)ALLOC_OFF(scratch_memory, sizeof(MaterialFileContentStreamingPacket));
      dst_pkt->material        = material;
      dst_pkt->file_stream     = src_pkt.file_stream;
      dst_pkt->asset_header    = src_pkt.asset_header;
      dst_pkt->buf             = ALLOC_OFF(scratch_memory, read_size);
      dst_pkt->size            = read_size;

      Result<void, FileError> stream_ok = read_file(dst_pkt->file_stream, &dst_header->file_promise, dst_pkt->buf, dst_pkt->size, src_pkt.size);
      if (!stream_ok)
      {
        dbgln("Failed to stream asset 0x%x. File read failed.", asset_id);
        material->asset.state    = kAssetFailedToLoad;
        return;
      }
    } break;
    case kMaterialCpuStreamContent:
    {
      MaterialFileContentStreamingPacket src_pkt;
      push_buffer_pop(&streamer->file_io_buffer, &src_pkt, sizeof(src_pkt));

      // !!! WARNING !!!
      //
      // The first sizeof(MaterialAsset) bytes of this pointer are invalid.
      //
      // The pointer arithmetic simply makes more sense if I subtract off the MaterialAsset header which has
      // already been read in. This way, the offset pointers are just added to this base pointer.
      //
      // Read from this pointer with care.
      //
      // !!! WARNING !!!
      u8*       buf      = (u8*)src_pkt.buf - sizeof(MaterialAsset);

      Material* material = src_pkt.material;
      AssetId   asset_id = material->asset.id;

      material->asset.state = kAssetStreaming;

      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.magic_number == kAssetMagicNumber,     "Material header data is corrupted for asset 0x%x. Expected magic number 0x%x but got 0x%x",    asset_id, kAssetMagicNumber,        src_pkt.asset_header.metadata.magic_number);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.asset_hash   == asset_id,              "Material header data is corrupted for asset 0x%x. Expected asset ID 0x%x but got 0x%x",         asset_id, asset_id,               src_pkt.asset_header.metadata.asset_hash);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.asset_type   == AssetType::kMaterial,  "Material header data is corrupted for asset 0x%x. Expected asset type 0x%x but got 0x%x",       asset_id, AssetType::kMaterial,   src_pkt.asset_header.metadata.asset_type);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.version      == kMaterialAssetVersion, "Material asset version for asset 0x%x mismatched. Expected version 0x%x but got 0x%x. Please run the asset builder on this asset.", asset_id, kMaterialAssetVersion, src_pkt.asset_header.metadata.version);

      bool valid_data = src_pkt.asset_header.metadata.magic_number == kAssetMagicNumber    &&
                        src_pkt.asset_header.metadata.asset_hash   == asset_id             &&
                        src_pkt.asset_header.metadata.asset_type   == AssetType::kMaterial &&
                        src_pkt.asset_header.metadata.version      == kMaterialAssetVersion;
      if (!valid_data)
      {
        dbgln("Skipping corrupted asset 0x%x", asset_id);
        material->asset.state = kAssetFailedToLoad;
        return;
      }

      defer { push_buffer_pop(&streamer->file_io_buffer, src_pkt.size); };

      ASSERT_MSG_FATAL(await_result != kAwaitInFlight, "In flight requests should be handled earlier up the call stack, something went wrong in the asset streamer.");
      if (await_result == kAwaitFailed)
      {
        material->asset.state = kAssetFailedToLoad;
        return;
      }
      else if (await_result == kAwaitInFlight)
      {
        return;
      }

      // Initialize the texture handles
      material->textures = init_array<TextureHandle>(streamer->metadata_allocator, src_pkt.asset_header.num_textures);

      u32 unstreamed_textures = 0;

      AssetRef<TextureAsset>* texture_asset_ids = (AssetRef<TextureAsset>*)(buf + src_pkt.asset_header.textures);
      for (u32 itexture = 0; itexture < src_pkt.asset_header.num_textures; itexture++)
      {
        TextureHandle* dst = array_add(&material->textures);
        *dst               = kick_texture_load(texture_asset_ids[itexture]);
        if (dst->is_streaming())
        {
          unstreamed_textures++;
        }
        else if (!dst->is_loaded())
        {
          material->asset.state = kAssetFailedToInitialize;
        }
      }

      u64          scratch_size      = sizeof(AssetDependencyCmdHeader)          +
                                       sizeof(Asset*) * unstreamed_textures      +
                                       sizeof(MaterialDependencyStreamingPacket);
      // We need to wait for the textures to be initialized to finish initializing the material
      void*        dependency_memory = push_buffer_begin_edit(&streamer->asset_dependency_queue, scratch_size);
      defer { push_buffer_end_edit(&streamer->asset_dependency_queue, dependency_memory); };

      void*         scratch_memory   = dependency_memory;
                              
      auto*         dst_header       = (AssetDependencyCmdHeader*         )ALLOC_OFF(scratch_memory, sizeof(AssetDependencyCmdHeader));
      const Asset** dependencies     = (const Asset**                     )ALLOC_OFF(scratch_memory, sizeof(Asset*) * material->textures.size);
      // NOTE(bshihabi): This has to appear after the header and dependencies because the consumer pops the dependencies off first
      auto*         dst_pkt          = (MaterialDependencyStreamingPacket*)ALLOC_OFF(scratch_memory, sizeof(MaterialDependencyStreamingPacket));

      dst_header->cmd                = kMaterialStreamDependencies;
      dst_header->dependency_count   = unstreamed_textures;
      dst_header->dependencies       = dependencies;
      dst_pkt->material              = material;
      dst_pkt->asset_header          = src_pkt.asset_header;

      // Fill the dependencies
      for (u32 itexture = 0; itexture < src_pkt.asset_header.num_textures; itexture++)
      {
        TextureHandle texture = material->textures[itexture];
        if (!texture.is_loaded())
        {
          *dependencies = texture.to_asset();
          dependencies++;
        }
      }
    } break;
    default: UNREACHABLE; break;
  }
}

static void
process_material_dep_request(AssetStreamer* streamer, AssetDependencyCmdHeader header)
{
  switch (header.cmd)
  {
    case kMaterialStreamDependencies:
    {
      MaterialDependencyStreamingPacket src_pkt;
      push_buffer_pop(&streamer->asset_dependency_queue, &src_pkt, sizeof(src_pkt));

      Material* material = src_pkt.material;
      AssetId   asset_id = material->asset.id;

      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.magic_number == kAssetMagicNumber,     "Material header data is corrupted for asset 0x%x. Expected magic number 0x%x but got 0x%x",    asset_id, kAssetMagicNumber,        src_pkt.asset_header.metadata.magic_number);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.asset_hash   == asset_id,              "Material header data is corrupted for asset 0x%x. Expected asset ID 0x%x but got 0x%x",         asset_id, asset_id,               src_pkt.asset_header.metadata.asset_hash);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.asset_type   == AssetType::kMaterial,  "Material header data is corrupted for asset 0x%x. Expected asset type 0x%x but got 0x%x",       asset_id, AssetType::kMaterial,   src_pkt.asset_header.metadata.asset_type);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.version      == kMaterialAssetVersion, "Material asset version for asset 0x%x mismatched. Expected version 0x%x but got 0x%x. Please run the asset builder on this asset.", asset_id, kMaterialAssetVersion, src_pkt.asset_header.metadata.version);

      bool valid_data = src_pkt.asset_header.metadata.magic_number == kAssetMagicNumber    &&
                        src_pkt.asset_header.metadata.asset_hash   == asset_id             &&
                        src_pkt.asset_header.metadata.asset_type   == AssetType::kMaterial &&
                        src_pkt.asset_header.metadata.version      == kMaterialAssetVersion;
      if (!valid_data)
      {
        dbgln("Skipping corrupted asset 0x%x", asset_id);
        material->asset.state = kAssetFailedToLoad;
        return;
      }

      for (u32 itexture = 0; itexture < material->textures.size; itexture++)
      {
        if (!material->textures[itexture].is_loaded())
        {
          ASSERT_MSG_FATAL(material->textures[itexture].is_loaded(), "Material textures should have loaded before process_material_dep_request was reached! This is a bug in the asset streamer.");
          return;
        }
      }

      // TODO(bshihabi): We should make this more flexible once we know what our material system looks like.
      ASSERT_MSG_FATAL(material->textures.size == 4, "Only 4 textures are supported per material. This material was built incorrectly by the builder.");

      // Upload the material to the GPU using the staging buffer
      MaterialGpu gpu_material;
      gpu_material.diffuse   = { material->textures[0]->srv_descriptor.index };
      gpu_material.normal    = { material->textures[1]->srv_descriptor.index };
      gpu_material.roughness = { material->textures[2]->srv_descriptor.index };
      gpu_material.metalness = { material->textures[3]->srv_descriptor.index };

      u8* gpu_scratch_mapped_base = (u8*)unwrap(streamer->gpu_staging_buffer.buffer.mapped);
      u8* gpu_scratch_mapped      = gpu_scratch_mapped_base + alloc_gpu_staging_bytes_blocking(streamer, sizeof(gpu_material));

      memcpy(gpu_scratch_mapped, &gpu_material, sizeof(gpu_material));

      u64 dst_offset          = sizeof(MaterialGpu) * material->gpu_id;

      gpu_copy_buffer(
        &streamer->gpu_cmd_buffer,
        // This is thread safe because we're not touching the buffer CPU data
        g_AssetRegistry->material_registry.gpu_material_buffer,
        dst_offset,
        streamer->gpu_staging_buffer.buffer,
        gpu_scratch_mapped - gpu_scratch_mapped_base,
        sizeof(MaterialGpu)
      );

      gpu_memory_barrier(&streamer->gpu_cmd_buffer);

      u64   scratch_size      = sizeof(GpuStreamingCmdHeader) + sizeof(MaterialGpuContentStreamingPacket);
      void* gpu_stream_memory = push_buffer_begin_edit(&streamer->gpu_io_buffer, scratch_size);
      defer { push_buffer_end_edit(&streamer->gpu_io_buffer, gpu_stream_memory); };

      void* scratch_memory        = gpu_stream_memory;

      auto* dst_header            = (GpuStreamingCmdHeader*            )ALLOC_OFF(scratch_memory, sizeof(GpuStreamingCmdHeader));
      dst_header->cmd             = kMaterialGpuStreamContent;
      dst_header->gpu_fence_value = flush_gpu_cmds(streamer);

      auto* dst_pkt               = (MaterialGpuContentStreamingPacket*)ALLOC_OFF(scratch_memory, sizeof(MaterialGpuContentStreamingPacket));
      dst_pkt->material           = material;
      dst_pkt->asset_header       = src_pkt.asset_header;
    } break;
    default: UNREACHABLE; break;
  }
}

static void
process_material_gpu_request(AssetStreamer* streamer, GpuStreamingCmdHeader header)
{
  switch (header.cmd)
  {
    case kMaterialGpuStreamContent:
    {
      MaterialGpuContentStreamingPacket src_pkt;
      push_buffer_pop(&streamer->gpu_io_buffer, &src_pkt, sizeof(src_pkt));

      Material* material = src_pkt.material;
      AssetId   asset_id = material->asset.id;

      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.magic_number == kAssetMagicNumber,     "Material header data is corrupted for asset 0x%x. Expected magic number 0x%x but got 0x%x",    asset_id, kAssetMagicNumber,        src_pkt.asset_header.metadata.magic_number);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.asset_hash   == asset_id,              "Material header data is corrupted for asset 0x%x. Expected asset ID 0x%x but got 0x%x",         asset_id, asset_id,               src_pkt.asset_header.metadata.asset_hash);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.asset_type   == AssetType::kMaterial,  "Material header data is corrupted for asset 0x%x. Expected asset type 0x%x but got 0x%x",       asset_id, AssetType::kMaterial,   src_pkt.asset_header.metadata.asset_type);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.version      == kMaterialAssetVersion, "Material asset version for asset 0x%x mismatched. Expected version 0x%x but got 0x%x. Please run the asset builder on this asset.", asset_id, kMaterialAssetVersion, src_pkt.asset_header.metadata.version);

      bool valid_data = src_pkt.asset_header.metadata.magic_number == kAssetMagicNumber    &&
                        src_pkt.asset_header.metadata.asset_hash   == asset_id             &&
                        src_pkt.asset_header.metadata.asset_type   == AssetType::kMaterial &&
                        src_pkt.asset_header.metadata.version      == kMaterialAssetVersion;
      if (!valid_data)
      {
        dbgln("Skipping corrupted asset 0x%x", asset_id);
        material->asset.state = kAssetFailedToLoad;
        return;
      }

      material->asset.state = kAssetReady;
    } break;
    default: UNREACHABLE; break;
  }
}

//////////////////////////////
//    Texture Streaming     //
//////////////////////////////
static TextureRegistry
init_texture_registry(void)
{
  size_t kTextureRegistrySize = KiB(8);
  void*  texture_registry_mem = HEAP_ALLOC_ALIGNED(g_InitHeap, kTextureRegistrySize, 16);

  TextureRegistry ret;
  ret.allocator  = init_linear_allocator(texture_registry_mem, kTextureRegistrySize);
  ret.asset_map  = init_hash_table<AssetId, Texture>(g_InitHeap, kMaxAssets);
  return ret;
}

struct TextureFileHeaderStreamingPacket
{
  Texture*        texture     = nullptr;
  u64             size        = 0;
  AsyncFileStream file_stream;
  TextureAsset    asset_header;
};

struct TextureFileContentStreamingPacket
{
  Texture*        texture     = nullptr;
  u64             size        = 0;
  void*           buf         = nullptr;
  TextureAsset    asset_header;
  AsyncFileStream file_stream;
};

struct TextureGpuContentStreamingPacket
{
  Texture*     texture = nullptr;
  TextureAsset asset_header;
};

static void
kick_texture_load(TextureRegistry* registry, AssetStreamer* streamer, AssetId asset_id)
{
  Texture* texture = nullptr;
  ACQUIRE(&registry->asset_map, auto* asset_map)
  {
    texture = hash_table_find(asset_map, asset_id);
    ASSERT_MSG_FATAL(texture != nullptr, "Texture 0x%x was not initialized in the TextureRegistry! This indicates that there was some out-of-order event that occurred as this is responsible for the calling kick_texture_load before the asset streaming thread gets it.", asset_id);

    ASSERT_MSG_FATAL(texture->asset.id   == asset_id,             "Possible hash table collision in the TextureRegistry! Expected texture to have asset ID 0x%llx but found 0x%llx", asset_id, texture->asset.id);
    ASSERT_MSG_FATAL(texture->asset.type == AssetType::kTexture,  "TextureRegistry is in a bad state, found non-texture asset in the asset map with type %u", texture->asset.type);
  };

  if (InterlockedCompareExchange(&texture->asset.state, kAssetLoadRequested, kAssetUnloaded) == kAssetUnloaded)
  {
    char asset_path[kAssetPathSize];
    asset_id_to_path(asset_path, asset_id);
    Result<AsyncFileStream, FileError> file_open_ok = open_file_async(asset_path, kFileStreamRead);
    if (!file_open_ok)
    {
      dbgln("Failed to open file for asset 0x%x.", asset_id);
      texture->asset.state = kAssetFailedToLoad;
      return;
    }

    u64   scratch_size   = sizeof(FileStreamingCmdHeader)           +
                           sizeof(TextureFileHeaderStreamingPacket);
    void* file_io_memory = push_buffer_begin_edit(&streamer->file_io_buffer, scratch_size);
    defer { push_buffer_end_edit(&streamer->file_io_buffer, file_io_memory); };

    void* scratch_memory = file_io_memory;

    FileStreamingCmdHeader*           header = (FileStreamingCmdHeader*          )ALLOC_OFF(scratch_memory, sizeof(FileStreamingCmdHeader));
    TextureFileHeaderStreamingPacket* pkt    = (TextureFileHeaderStreamingPacket*)ALLOC_OFF(scratch_memory, sizeof(TextureFileHeaderStreamingPacket));

    header->cmd          = kTextureCpuStreamHeader;
    pkt->texture         = texture;
    pkt->size            = sizeof(TextureAsset);
    pkt->file_stream     = file_open_ok.value();

    Result<void, FileError> file_read_ok = read_file(pkt->file_stream, &header->file_promise, &pkt->asset_header, pkt->size, 0);
    if (!file_read_ok)
    {
      dbgln("Failed to read file for asset 0x%x.", asset_id);
      texture->asset.state = kAssetFailedToLoad;
      return;
    }
  }
}

static void
process_texture_file_request(AssetStreamer* streamer, FileStreamingCmdHeader header, AwaitError await_result)
{
  switch (header.cmd)
  {
    case kTextureCpuStreamHeader:
    {
      TextureFileHeaderStreamingPacket src_pkt;
      push_buffer_pop(&streamer->file_io_buffer, &src_pkt, sizeof(src_pkt));

      Texture* texture  = src_pkt.texture;
      AssetId  asset_id = texture->asset.id;

      ASSERT_MSG_FATAL(await_result != kAwaitInFlight, "In flight requests should be handled earlier up the call stack, something went wrong in the asset streamer.");
      if (await_result == kAwaitFailed)
      {
        texture->asset.state = kAssetFailedToLoad;
        return;
      }
      else if (await_result == kAwaitInFlight)
      {
        return;
      }

      char asset_id_str[512];
      asset_id_to_path(asset_id_str, asset_id);
      CPU_PROFILE_SCOPE("Texture CPU Stream Header", asset_id_str);

      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.magic_number == kAssetMagicNumber,    "Texture header data is corrupted for asset 0x%x. Expected magic number 0x%x but got 0x%x",    asset_id, kAssetMagicNumber,       src_pkt.asset_header.metadata.magic_number);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.asset_hash   == asset_id,             "Texture header data is corrupted for asset 0x%x. Expected asset ID 0x%x but got 0x%x",         asset_id, asset_id,              src_pkt.asset_header.metadata.asset_hash);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.asset_type   == AssetType::kTexture,  "Texture header data is corrupted for asset 0x%x. Expected asset type 0x%x but got 0x%x",       asset_id, AssetType::kTexture,  src_pkt.asset_header.metadata.asset_type);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.version      == kTextureAssetVersion, "Texture asset version for asset 0x%x mismatched. Expected version 0x%x but got 0x%x. Please run the asset builder on this asset.", asset_id, kTextureAssetVersion, src_pkt.asset_header.metadata.version);

      bool valid_data = src_pkt.asset_header.metadata.magic_number == kAssetMagicNumber   &&
                        src_pkt.asset_header.metadata.asset_hash   == asset_id            &&
                        src_pkt.asset_header.metadata.asset_type   == AssetType::kTexture &&
                        src_pkt.asset_header.metadata.version      == kTextureAssetVersion;
      if (!valid_data)
      {
        dbgln("Skipping corrupted asset 0x%x", asset_id);
        texture->asset.state = kAssetFailedToLoad;
        return;
      }

      u64   read_size    = src_pkt.asset_header.compressed_size;

      u64   scratch_size = sizeof(FileStreamingCmdHeader)            +
                           sizeof(TextureFileContentStreamingPacket) +
                           read_size;

      void* file_io_memory = push_buffer_begin_edit(&streamer->file_io_buffer, scratch_size);
      defer { push_buffer_end_edit(&streamer->file_io_buffer, file_io_memory); };

      void* scratch_memory = file_io_memory;

      auto* dst_header         = (FileStreamingCmdHeader*           )ALLOC_OFF(scratch_memory, sizeof(FileStreamingCmdHeader));
      dst_header->cmd          = kTextureCpuStreamContent;
      dst_header->file_promise = {0};

      auto* dst_pkt            = (TextureFileContentStreamingPacket*)ALLOC_OFF(scratch_memory, sizeof(TextureFileContentStreamingPacket));
      dst_pkt->texture         = texture;
      dst_pkt->file_stream     = src_pkt.file_stream;
      dst_pkt->asset_header    = src_pkt.asset_header;
      dst_pkt->buf             = ALLOC_OFF(scratch_memory, read_size);
      dst_pkt->size            = read_size;

      Result<void, FileError> stream_ok = read_file(dst_pkt->file_stream, &dst_header->file_promise, dst_pkt->buf, dst_pkt->size, src_pkt.size);
      if (!stream_ok)
      {
        dbgln("Failed to stream asset 0x%x. File read failed.", asset_id);
        texture->asset.state     = kAssetFailedToLoad;
        return;
      }
    } break;
    case kTextureCpuStreamContent:
    {
      TextureFileContentStreamingPacket src_pkt;
      push_buffer_pop(&streamer->file_io_buffer, &src_pkt, sizeof(src_pkt));

      Texture* texture  = src_pkt.texture;
      AssetId  asset_id = texture->asset.id;

      texture->asset.state = kAssetStreaming;

      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.magic_number == kAssetMagicNumber,    "Texture header data is corrupted for asset 0x%x. Expected magic number 0x%x but got 0x%x",    asset_id, kAssetMagicNumber,       src_pkt.asset_header.metadata.magic_number);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.asset_hash   == asset_id,             "Texture header data is corrupted for asset 0x%x. Expected asset ID 0x%x but got 0x%x",         asset_id, asset_id,              src_pkt.asset_header.metadata.asset_hash);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.asset_type   == AssetType::kTexture,  "Texture header data is corrupted for asset 0x%x. Expected asset type 0x%x but got 0x%x",       asset_id, AssetType::kTexture,  src_pkt.asset_header.metadata.asset_type);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.version      == kTextureAssetVersion, "Texture asset version for asset 0x%x mismatched. Expected version 0x%x but got 0x%x. Please run the asset builder on this asset.", asset_id, kTextureAssetVersion, src_pkt.asset_header.metadata.version);

      bool valid_data = src_pkt.asset_header.metadata.magic_number == kAssetMagicNumber   &&
                        src_pkt.asset_header.metadata.asset_hash   == asset_id            &&
                        src_pkt.asset_header.metadata.asset_type   == AssetType::kTexture &&
                        src_pkt.asset_header.metadata.version      == kTextureAssetVersion;
      if (!valid_data)
      {
        dbgln("Skipping corrupted asset 0x%x", asset_id);
        texture->asset.state = kAssetFailedToLoad;
        return;
      }

      defer { push_buffer_pop(&streamer->file_io_buffer, src_pkt.size); };

      ASSERT_MSG_FATAL(await_result != kAwaitInFlight, "In flight requests should be handled earlier up the call stack, something went wrong in the asset streamer.");
      if (await_result == kAwaitFailed)
      {
        texture->asset.state = kAssetFailedToLoad;
        return;
      }
      else if (await_result == kAwaitInFlight)
      {
        return;
      }

      // TODO(bshihabi): Allocate a GpuTexture, copy src_pkt.buf into the GPU staging buffer, and issue the upload command

      u64   scratch_size      = sizeof(GpuStreamingCmdHeader) + sizeof(TextureGpuContentStreamingPacket);
      void* gpu_stream_memory = push_buffer_begin_edit(&streamer->gpu_io_buffer, scratch_size);
      defer { push_buffer_end_edit(&streamer->gpu_io_buffer, gpu_stream_memory); };

      void* scratch_memory        = gpu_stream_memory;

      auto* dst_header            = (GpuStreamingCmdHeader*           )ALLOC_OFF(scratch_memory, sizeof(GpuStreamingCmdHeader));
      dst_header->cmd             = kTextureGpuStreamContent;
      dst_header->gpu_fence_value = flush_gpu_cmds(streamer);

      auto* dst_pkt               = (TextureGpuContentStreamingPacket*)ALLOC_OFF(scratch_memory, sizeof(TextureGpuContentStreamingPacket));
      dst_pkt->texture            = texture;
      dst_pkt->asset_header       = src_pkt.asset_header;
    } break;
    default: UNREACHABLE; break;
  }
}

static void
process_texture_gpu_request(AssetStreamer* streamer, GpuStreamingCmdHeader header)
{
  switch (header.cmd)
  {
    case kTextureGpuStreamContent:
    {
      TextureGpuContentStreamingPacket src_pkt;
      push_buffer_pop(&streamer->gpu_io_buffer, &src_pkt, sizeof(src_pkt));

      Texture* texture  = src_pkt.texture;
      AssetId  asset_id = texture->asset.id;

      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.magic_number == kAssetMagicNumber,    "Texture header data is corrupted for asset 0x%x. Expected magic number 0x%x but got 0x%x",    asset_id, kAssetMagicNumber,       src_pkt.asset_header.metadata.magic_number);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.asset_hash   == asset_id,             "Texture header data is corrupted for asset 0x%x. Expected asset ID 0x%x but got 0x%x",         asset_id, asset_id,              src_pkt.asset_header.metadata.asset_hash);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.asset_type   == AssetType::kTexture,  "Texture header data is corrupted for asset 0x%x. Expected asset type 0x%x but got 0x%x",       asset_id, AssetType::kTexture,  src_pkt.asset_header.metadata.asset_type);
      ASSERT_MSG_FATAL(src_pkt.asset_header.metadata.version      == kTextureAssetVersion, "Texture asset version for asset 0x%x mismatched. Expected version 0x%x but got 0x%x. Please run the asset builder on this asset.", asset_id, kTextureAssetVersion, src_pkt.asset_header.metadata.version);

      bool valid_data = src_pkt.asset_header.metadata.magic_number == kAssetMagicNumber   &&
                        src_pkt.asset_header.metadata.asset_hash   == asset_id            &&
                        src_pkt.asset_header.metadata.asset_type   == AssetType::kTexture &&
                        src_pkt.asset_header.metadata.version      == kTextureAssetVersion;
      if (!valid_data)
      {
        dbgln("Skipping corrupted asset 0x%x", asset_id);
        texture->asset.state = kAssetFailedToLoad;
        return;
      }

      // TODO(bshihabi): Register the GpuTexture with the bindless descriptor heap

      texture->asset.state = kAssetReady;
    } break;
    default: UNREACHABLE; break;
  }
}

static void
process_file_io(AssetStreamer* streamer)
{
  while (true)
  {
    // Pop the next file I/O command off the stack if ready and we don't already have one that we're waiting for
    if (streamer->next_file_io_cmd.cmd == kNullStreamingCmd)
    {
      if (!try_push_buffer_pop(&streamer->file_io_buffer, &streamer->next_file_io_cmd, sizeof(FileStreamingCmdHeader)))
      {
        return;
      }
    }

    AwaitError ready = await_io(streamer->next_file_io_cmd.file_promise, kFileIOBlockRateMs);
    // If it's still in flight after some time, then move on and try again later
    if (ready == kAwaitInFlight)
    {
      return;
    }

    if      (streamer->next_file_io_cmd.cmd <= kModelCpuStreamContent)
    {
      process_model_file_request(streamer, streamer->next_file_io_cmd, ready);
      zero_memory(&streamer->next_file_io_cmd, sizeof(streamer->next_file_io_cmd));
    }
    else if (streamer->next_file_io_cmd.cmd <= kMaterialCpuStreamContent)
    {
      process_material_file_request(streamer, streamer->next_file_io_cmd, ready);
      zero_memory(&streamer->next_file_io_cmd, sizeof(streamer->next_file_io_cmd));
    }
    else if (streamer->next_file_io_cmd.cmd <= kTextureCpuStreamContent)
    {
      process_texture_file_request(streamer, streamer->next_file_io_cmd, ready);
      zero_memory(&streamer->next_file_io_cmd, sizeof(streamer->next_file_io_cmd));
    }
    else
    {
      ASSERT_MSG_FATAL(false, "Invalid streaming command received %u!", streamer->next_file_io_cmd.cmd);
    }
  }
}

static void
process_gpu_io(AssetStreamer* streamer)
{
  while (true)
  {
    // Pop the next file I/O command off the stack if ready and we don't already have one that we're waiting for
    if (streamer->next_gpu_cmd.cmd == kNullStreamingCmd)
    {
      if (!try_push_buffer_pop(&streamer->gpu_io_buffer, &streamer->next_gpu_cmd, sizeof(GpuStreamingCmdHeader)))
      {
        return;
      }
    }

    FenceValue value = poll_gpu_fence_value(&streamer->gpu_cmd_buffer_allocator.fence);
    // If we're still waiting for the GPU to finish this command, then move on and try again later
    if (value < streamer->next_gpu_cmd.gpu_fence_value)
    {
      return;
    }

    if      (streamer->next_gpu_cmd.cmd <= kModelCmdEnd)
    {
      process_model_gpu_request(streamer, streamer->next_gpu_cmd);
      zero_memory(&streamer->next_gpu_cmd, sizeof(streamer->next_gpu_cmd));
    }
    else if (streamer->next_gpu_cmd.cmd <= kMaterialCmdEnd)
    {
      process_material_gpu_request(streamer, streamer->next_gpu_cmd);
      zero_memory(&streamer->next_gpu_cmd, sizeof(streamer->next_gpu_cmd));
    }
    else if (streamer->next_gpu_cmd.cmd <= kTextureCmdEnd)
    {
      process_texture_gpu_request(streamer, streamer->next_gpu_cmd);
      zero_memory(&streamer->next_gpu_cmd, sizeof(streamer->next_gpu_cmd));
    }
    else
    {
      ASSERT_MSG_FATAL(false, "Invalid streaming command received %u!", streamer->next_file_io_cmd.cmd);
    }
  }
}

static void
process_asset_dependencies(AssetStreamer* streamer)
{
  while (true)
  {
    if (streamer->next_asset_dependency_cmd.dependency_count == 0)
    {
      if (!try_push_buffer_pop(&streamer->asset_dependency_queue, &streamer->next_asset_dependency_cmd, sizeof(AssetDependencyCmdHeader)))
      {
        return;
      }
    }

    AssetDependencyCmdHeader* cmd          = &streamer->next_asset_dependency_cmd;
    const Asset**       dependencies = cmd->dependencies;
    for (u32 iasset = 0; iasset < cmd->dependency_count; iasset++)
    {
      if (dependencies[iasset]->state < kAssetReady)
      {
        return;
      }
    }

    // Pop off the rest of the packet since it's ready now
    push_buffer_pop(&streamer->asset_dependency_queue, sizeof(const Asset*) * cmd->dependency_count);

    if (streamer->next_gpu_cmd.cmd <= kMaterialCmdEnd)
    {
    }
  }
}


MaterialHandle
kick_material_load(AssetId asset_id)
{
  Material* material = nullptr;
  ACQUIRE(&g_AssetRegistry->material_registry.asset_map, auto* asset_map)
  {
    material = hash_table_find(asset_map, asset_id);
    if (material == nullptr)
    {
      material = hash_table_insert(asset_map, asset_id);
      zero_struct(material);

      material->asset.id    = asset_id;
      material->asset.type  = AssetType::kMaterial;
      material->asset.state = kAssetUnloaded;
    }
    else
    {
      ASSERT_MSG_FATAL(material->asset.id   == asset_id,             "Possible hash table collision in the MaterialRegistry! Expected material to have asset ID 0x%llx but found 0x%llx", asset_id, material->asset.id);
      ASSERT_MSG_FATAL(material->asset.type == AssetType::kMaterial, "MaterialRegistry is in a bad state, found non-material asset in the asset map with type %u", material->asset.type);
    }
  };

  MaterialHandle ret;
  ret.m_Id  = asset_id;
  ret.m_Ptr = material;

  for (u32 itry = 0; /*TODO(bshihabi): Potentially put a max amount here in case of deadlock...*/ ; itry++)
  {
    bool ok = ACQUIRE(&g_AssetStreamer->asset_stream_requests, auto* asset_stream_requests)
    {
      AssetStreamRequest request;
      request.asset_type = AssetType::kMaterial;
      request.asset_id   = asset_id;
      return try_ring_queue_push(asset_stream_requests, request);
    };

    if (ok)
    {
      break;
    }

    if (itry == 0)
    {
      dbgln("Asset stream request queue is full! This will stall the calling thread requesting asset 0x%x (this should not be the case as it could hold up other work). Consider increasing the size of the ring queue for g_AssetStreamer->asset_streaming_requests.", asset_id);
    }
    _mm_pause();
    _mm_pause();
    _mm_pause();
    _mm_pause();
  }

  return ret;
}

TextureHandle
kick_texture_load(AssetId asset_id)
{
  Texture* texture = nullptr;
  ACQUIRE(&g_AssetRegistry->texture_registry.asset_map, auto* asset_map)
  {
    texture = hash_table_find(asset_map, asset_id);
    if (texture == nullptr)
    {
      texture = hash_table_insert(asset_map, asset_id);
      zero_struct(texture);

      texture->asset.id    = asset_id;
      texture->asset.type  = AssetType::kTexture;
      texture->asset.state = kAssetUnloaded;
    }
    else
    {
      ASSERT_MSG_FATAL(texture->asset.id   == asset_id,            "Possible hash table collision in the TextureRegistry! Expected texture to have asset ID 0x%llx but found 0x%llx", asset_id, texture->asset.id);
      ASSERT_MSG_FATAL(texture->asset.type == AssetType::kTexture, "TextureRegistry is in a bad state, found non-texture asset in the asset map with type %u", texture->asset.type);
    }
  };

  TextureHandle ret;
  ret.m_Id  = asset_id;
  ret.m_Ptr = texture;

  for (u32 itry = 0; /*TODO(bshihabi): Potentially put a max amount here in case of deadlock...*/ ; itry++)
  {
    bool ok = ACQUIRE(&g_AssetStreamer->asset_stream_requests, auto* asset_stream_requests)
    {
      AssetStreamRequest request;
      request.asset_type = AssetType::kTexture;
      request.asset_id   = asset_id;
      return try_ring_queue_push(asset_stream_requests, request);
    };

    if (ok)
    {
      break;
    }

    if (itry == 0)
    {
      dbgln("Asset stream request queue is full! This will stall the calling thread requesting asset 0x%x (this should not be the case as it could hold up other work). Consider increasing the size of the ring queue for g_AssetStreamer->asset_streaming_requests.", asset_id);
    }
    _mm_pause();
    _mm_pause();
    _mm_pause();
    _mm_pause();
  }

  return ret;
}

static u32
asset_streaming_thread(void* param)
{
  AssetStreamer* streamer = (AssetStreamer*)param;
  while (!atomic_load(streamer->kill))
  {
      // Consume stuff from the asset stream queue to kick off early asset loads as soon as possible and to avoid the buffer filling up
    while (true)
    {
      AssetStreamRequest request;
      bool               got_request = false;
      ACQUIRE(&streamer->asset_stream_requests, auto* asset_streaming_requests)
      {
        got_request = try_ring_queue_pop(asset_streaming_requests, &request);
      };

      if (got_request)
      {
        switch (request.asset_type)
        {
          case AssetType::kModel:    kick_model_load   (&g_AssetRegistry->model_registry,    streamer, request.asset_id); break;
          case AssetType::kMaterial: kick_material_load(&g_AssetRegistry->material_registry, streamer, request.asset_id); break;
          case AssetType::kTexture:  kick_texture_load (&g_AssetRegistry->texture_registry,  streamer, request.asset_id); break;
          default: UNREACHABLE;
        }

        _mm_pause();
      }
      else
      {
        break;
      }
    }

    // Once there are no more for some period of time, start processing the file I/O
    process_file_io(streamer);
    process_gpu_io(streamer);
  }

  return 0;
}

static AssetStreamer*
init_asset_streamer_impl(void)
{
  AssetStreamer* ret            = HEAP_ALLOC(AssetStreamer, g_InitHeap, 1);
  ret->asset_stream_requests    = init_ring_queue<AssetStreamRequest>(g_InitHeap, kMaxAssetLoadRequests);


  // TODO(bshihabi): These should probably be adjusted
  u64 kFileIOBufferSize         = MiB(256);
  u64 kGpuStreamQueueSize       = MiB(128);
  u64 kAssetDependencyQueueSize = KiB(4);
  u32 kGpuStagingBufferSize     = MiB(64);
  u32 kGpuScratchBufferSize     = MiB(8);

  ret->file_io_buffer           = init_push_buffer(MiB(16), kFileIOBufferSize,         GiB(1));
  ret->gpu_io_buffer            = init_push_buffer(KiB(1),  kGpuStreamQueueSize,       GiB(1));
  ret->asset_dependency_queue   = init_push_buffer(KiB(1),  kAssetDependencyQueueSize, MiB(1));
  ret->next_file_io_cmd.cmd     = kNullStreamingCmd;
  ret->next_gpu_cmd.cmd         = kNullStreamingCmd;
  ret->next_asset_dependency_cmd.dependency_count = 0;

  GpuBufferDesc staging_desc    = {0};
  staging_desc.size             = kGpuStagingBufferSize;
  ret->gpu_staging_buffer       = alloc_gpu_ring_buffer_no_heap(g_InitHeap, staging_desc, kGpuHeapSysRAMCpuToGpu, "Streaming Upload Buffer");

  GpuBufferDesc scratch_desc    = {0};
  staging_desc.size             = kGpuScratchBufferSize;
  scratch_desc.flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ret->gpu_scratch_buffer       = alloc_gpu_buffer_no_heap(g_GpuDevice, staging_desc, kGpuHeapGpuOnly, "BLAS Scratch Buffer");

  ret->gpu_cmd_buffer_allocator = init_cmd_list_allocator(g_InitHeap, g_GpuDevice, &g_GpuDevice->compute_queue, 64);
  ret->gpu_cmd_buffer           = alloc_cmd_list(&ret->gpu_cmd_buffer_allocator);

  u64 kModelSubsetAllocatorSize = KiB(16);
  ret->metadata_allocator       = init_linear_allocator(kModelSubsetAllocatorSize, GiB(1));


  static constexpr u64 kAssetStreamerStackSize = MiB(4);
  static constexpr u32 kAssetStreamingCoreIdx  = 7;
  ret->thread = init_thread(g_InitHeap, kAssetStreamerStackSize, &asset_streaming_thread, (void*)ret, kAssetStreamingCoreIdx);

  set_thread_name(&ret->thread, L"Asset Streaming Thread");
  return ret;
}

void
init_asset_streamer(void)
{
  g_AssetStreamer = init_asset_streamer_impl();
}

void
destroy_asset_streamer(void)
{
  atomic_store(&g_AssetStreamer->kill, true);
  join_threads(&g_AssetStreamer->thread, 1);
}

static void
asset_streamer_flush(AssetStreamer* streamer)
{
  push_buffer_flush(&streamer->file_io_buffer);
  push_buffer_flush(&streamer->gpu_io_buffer);
}

void
asset_streamer_update(void)
{
  asset_streamer_flush(g_AssetStreamer);
}

void
init_asset_registry(void)
{
  AssetRegistry* registry     = HEAP_ALLOC(AssetRegistry, g_InitHeap, 1);
  registry->model_registry    = init_model_registry();
  registry->material_registry = init_material_registry();
  registry->texture_registry  = init_texture_registry();

  g_AssetRegistry = registry;
}


ModelHandle
kick_model_load(AssetId asset_id)
{
  Model* model = nullptr;
  // NOTE(bshihabi): There is a little contention here as everyone ends up touching the registry at the same time to initialize stuff. The hope is that this code is so quick that it doesn't matter.
  ACQUIRE(&g_AssetRegistry->model_registry.asset_map, auto* asset_map)
  {
    model = hash_table_find(asset_map, asset_id);
    if (model == nullptr)
    {
      // Initialize the model if it doesn't exist
      model = hash_table_insert(asset_map, asset_id);
      zero_struct(model);

      model->asset.id    = asset_id;
      model->asset.type  = AssetType::kModel;
      model->asset.state = kAssetUnloaded;
    }
    else
    {
      // Validate it if we already have the metadata
      ASSERT_MSG_FATAL(model->asset.id   == asset_id,          "Possible hash table collision in the ModelRegistry! Expected model to have asset ID 0x%llx but found 0x%llx", asset_id, model->asset.id);
      ASSERT_MSG_FATAL(model->asset.type == AssetType::kModel, "ModelRegistry is in a bad state, found non-model asset in the asset map with type %u", model->asset.type);
    }
  };

  ModelHandle ret;
  ret.m_Id  = asset_id;
  ret.m_Ptr = model;

  for (u32 itry = 0; /*TODO(bshihabi): Potentially put a max amount here in case of deadlock...*/ ; itry++)
  {
    bool ok = ACQUIRE(&g_AssetStreamer->asset_stream_requests, auto* asset_stream_requests)
    {
      AssetStreamRequest request;
      request.asset_type = AssetType::kModel;
      request.asset_id   = asset_id;
      return try_ring_queue_push(asset_stream_requests, request);
    };

    if (ok)
    {
      break;
    }

    if (itry == 0)
    {
      dbgln("Asset stream request queue is full! This will stall the calling thread requesting asset 0x%x (this should not be the case as it could hold up other work). Consider increasing the size of the ring queue for g_AssetStreamer->asset_streaming_requests.", asset_id);
    }
    _mm_pause();
    _mm_pause();
    _mm_pause();
    _mm_pause();
  }

  return ret;
}

