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
  kModelStreamDependencies,
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
  kTextureMainThreadInitialize,
  kTextureCmdEnd,         // Leave this at the end here, so that we can determine streaming cmd type
};

struct FileStreamingCmdHeader
{
  StreamingCmd     cmd               = kNullStreamingCmd;
  AsyncFilePromise file_promise;

  // Used for statistics
  u64              io_byte_count     = 0;
  u64              request_timestamp = 0;
};

struct GpuStreamingCmdHeader
{
  StreamingCmd     cmd              = kNullStreamingCmd;
  FenceValue       gpu_fence_value  = 0;

  // Used for statistics
  u64              io_byte_count    = 0;
  u64              request_timestamp = 0;
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

  u32              pkt_size         = 0;
  void*            pkt              = nullptr;

};

struct AssetStreamer
{
  SpinLocked<RingQueue<AssetStreamRequest>> asset_stream_requests;

  PushBuffer                                header_file_io_buffer;
  PushBuffer                                content_file_io_buffer;
  PushBuffer                                gpu_io_buffer;
  PushBuffer                                asset_dependency_queue;
  u32                                       num_asset_waiting_for_dependencies = 0;

  // Asset Streaming Thread (producer) -> Main Thread (consumer)
  //   Use this for any work that isn't thread safe. It will be done at the beginning of the frame.
  PushBuffer                                main_thread_cmd_queue;

  // Caches the next in queue file I/O and Gpu commands
  FileStreamingCmdHeader                    next_header_file_io_cmd;
  FileStreamingCmdHeader                    next_content_file_io_cmd;
  u32                                       file_io_assets_in_flight = 0;
  GpuStreamingCmdHeader                     next_gpu_cmd;

  GpuRingBuffer                             gpu_staging_buffer;
  GpuBuffer                                 gpu_scratch_buffer;

  // TODO(bshihabi): We should really use virtual memory for this
  GpuLinearAllocator                        gpu_texture_allocator;

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
alloc_gpu_staging_bytes_blocking(AssetStreamer* streamer, u32 size, u32 alignment = 1)
{
  while (true)
  {
    Result<u64, FenceValue> ret = gpu_ring_buffer_alloc(&streamer->gpu_staging_buffer, size, alignment);
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
  GpuDescriptor                            gpu_material_grv;
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
AssetStreamingStatistics g_AssetStreamingStats;

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

struct ModelDependencyStreamingPacket
{
  Model*          model  = nullptr;
  ModelAsset      asset_header;
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
    void* file_io_memory = push_buffer_begin_edit(&streamer->header_file_io_buffer, scratch_size);
    defer { push_buffer_end_edit(&streamer->header_file_io_buffer, file_io_memory); };

    void* scratch_memory = file_io_memory;

    // Allocate the command header and packet data
    FileStreamingCmdHeader*         header = (FileStreamingCmdHeader*        )ALLOC_OFF(scratch_memory, sizeof(FileStreamingCmdHeader));
    ModelFileHeaderStreamingPacket* pkt    = (ModelFileHeaderStreamingPacket*)ALLOC_OFF(scratch_memory, sizeof(ModelFileHeaderStreamingPacket));

    // Initialize all of the packet data
    header->cmd               = kModelCpuStreamHeader;
    header->file_promise      = kAsyncFileError;
    pkt->model                = model;
    pkt->size                 = sizeof(ModelAsset);
    pkt->file_stream          = file_open_ok.value();

    // Fill in the statistics
    header->io_byte_count     = pkt->size;
    header->request_timestamp = begin_cpu_profiler_timestamp();

    // If the file read fails, then we already allocated the memory so we can't abort now, we just mark the file promise as failed and the consumer will ignore the packet.
    Result<void, FileError> file_read_ok = read_file(pkt->file_stream, &header->file_promise, &pkt->asset_header, pkt->size, 0);
    if (!file_read_ok)
    {
      dbgln("Failed to read file for asset 0x%x.", asset_id);
      model->asset.state = kAssetFailedToLoad;
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
      push_buffer_pop(&streamer->header_file_io_buffer, &src_pkt, sizeof(src_pkt));

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
      u64   read_size    = src_pkt.asset_header.num_model_subsets * sizeof(ModelAsset::ModelSubset)                                                                      +
                           src_pkt.asset_header.num_model_subsets * src_pkt.asset_header.lod_count * sizeof(ModelAsset::ModelSubsetLod) +
                           src_pkt.asset_header.vertices_size                                                                           +
                           src_pkt.asset_header.indices_size;

      // Allocate some scratch memory in the ring buffer to read the file data
      u64   scratch_size = sizeof(FileStreamingCmdHeader)          +
                           sizeof(ModelFileContentStreamingPacket) +
                           read_size;

      void* file_io_memory = push_buffer_begin_edit(&streamer->content_file_io_buffer, scratch_size);
      defer { push_buffer_end_edit(&streamer->content_file_io_buffer, file_io_memory); };

      // Initialize the packets to push to the queue
      void* scratch_memory = file_io_memory;

      auto* dst_header          = (FileStreamingCmdHeader*         )ALLOC_OFF(scratch_memory, sizeof(FileStreamingCmdHeader));
      dst_header->cmd           = kModelCpuStreamContent;
      dst_header->file_promise  = {0};

      auto* dst_pkt             = (ModelFileContentStreamingPacket*)ALLOC_OFF(scratch_memory, sizeof(ModelFileContentStreamingPacket));
      dst_pkt->model            = model;
      dst_pkt->file_stream      = src_pkt.file_stream;
      dst_pkt->asset_header     = src_pkt.asset_header;
      dst_pkt->buf              = ALLOC_OFF(scratch_memory, read_size);
      dst_pkt->size             = read_size;

      // Fill in the statistics
      dst_header->io_byte_count     = dst_pkt->size;
      dst_header->request_timestamp = begin_cpu_profiler_timestamp();

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
      push_buffer_pop(&streamer->content_file_io_buffer, &src_pkt, sizeof(src_pkt));

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
      defer { push_buffer_pop(&streamer->content_file_io_buffer, src_pkt.size); };

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

      // For statistics
      u64 gpu_io_byte_count = 0;

      // Initialize all the model subsets in the metadata
      ModelAsset::ModelSubset* asset_subset = (ModelAsset::ModelSubset*)(buf + src_pkt.asset_header.model_subsets);
      for (u32 isubset = 0; isubset < src_pkt.asset_header.num_model_subsets; isubset++, asset_subset++)
      {
        ModelSubset* runtime_subset = array_add(&model->subsets);
        runtime_subset->center      = asset_subset->center;
        runtime_subset->radius      = asset_subset->radius;
        runtime_subset->lods        = init_array<ModelSubsetLod>(streamer->metadata_allocator, src_pkt.asset_header.lod_count);

        // Copy vertex/index data and populate runtime LODs
        ModelAsset::ModelSubsetLod* asset_lod = (ModelAsset::ModelSubsetLod*)(buf + asset_subset->lods);
        for (u32 ilod = 0; ilod < src_pkt.asset_header.lod_count; ilod++, asset_lod++)
        {
          u64 vertex_offset_bytes = alloc_uber_vertex(asset_lod->num_vertices * sizeof(Vertex));
          u64 index_offset_bytes  = alloc_uber_index (asset_lod->num_indices  * sizeof(u16));

          ModelSubsetLod* runtime_lod = array_add(&runtime_subset->lods);
          runtime_lod->vertex_start   = (u32)vertex_offset_bytes / sizeof(Vertex);
          runtime_lod->vertex_count   = (u32)asset_lod->num_vertices;
          runtime_lod->index_start    = (u32)index_offset_bytes  / sizeof(u16);
          runtime_lod->index_count    = (u32)asset_lod->num_indices;
          runtime_lod->error          = asset_lod->error;

          u32 lod_vertex_size_in_bytes = (u32)(sizeof(Vertex) * asset_lod->num_vertices);
          u32 lod_index_size_in_bytes  = (u32)(sizeof(u16)    * asset_lod->num_indices);

          u8* gpu_scratch_mapped_base  = (u8*)unwrap(streamer->gpu_staging_buffer.buffer.mapped);
          u8* gpu_scratch_mapped       = gpu_scratch_mapped_base + alloc_gpu_staging_bytes_blocking(streamer, lod_vertex_size_in_bytes + lod_index_size_in_bytes);

          u8* lod_vertex_staging       = ALLOC_OFF(gpu_scratch_mapped, lod_vertex_size_in_bytes);
          u8* lod_index_staging        = ALLOC_OFF(gpu_scratch_mapped, lod_index_size_in_bytes );
          memcpy(lod_vertex_staging, buf + asset_lod->vertices, lod_vertex_size_in_bytes);
          memcpy(lod_index_staging,  buf + asset_lod->indices,  lod_index_size_in_bytes );

          gpu_copy_buffer(
            &streamer->gpu_cmd_buffer,
            g_UnifiedGeometryBuffer.vertex_buffer,
            runtime_lod->vertex_start * sizeof(Vertex),
            streamer->gpu_staging_buffer.buffer,
            lod_vertex_staging - gpu_scratch_mapped_base,
            lod_vertex_size_in_bytes
          );

          gpu_io_byte_count += lod_vertex_size_in_bytes;

          gpu_copy_buffer(
            &streamer->gpu_cmd_buffer,
            g_UnifiedGeometryBuffer.index_buffer,
            runtime_lod->index_start * sizeof(u16),
            streamer->gpu_staging_buffer.buffer,
            lod_index_staging - gpu_scratch_mapped_base,
            lod_index_size_in_bytes
          );

          gpu_io_byte_count += lod_index_size_in_bytes;
        }

        // TODO(bshihabi): We need to add proper debug names for the BLASes
        runtime_subset->rt_blas_lod  = (u32)runtime_subset->lods.size - 1;
        GpuRtBlas* subset_rt_blas    = array_add(&model->subset_rt_blases);
        const ModelSubsetLod* rt_lod = &runtime_subset->lods[runtime_subset->rt_blas_lod];
        *subset_rt_blas              = alloc_uber_blas(rt_lod->vertex_start, rt_lod->vertex_count, rt_lod->index_start, rt_lod->index_count, "Content subset RT BLAS");

        // Kick off the material loads
        {
          MaterialHandle* dst = array_add(&model->materials);
          *dst                = kick_material_load(asset_subset->material);
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
      gpu_memory_barrier(&streamer->gpu_cmd_buffer);

      u64   scratch_size      = sizeof(GpuStreamingCmdHeader) + sizeof(ModelGpuContentStreamingPacket);
      void* gpu_stream_memory = push_buffer_begin_edit(&streamer->gpu_io_buffer, scratch_size);
      defer { push_buffer_end_edit(&streamer->gpu_io_buffer, gpu_stream_memory); };

      // Push the GPU content streaming packet to the queue
      void* scratch_memory           = gpu_stream_memory;

      auto* dst_header               = (GpuStreamingCmdHeader*         )ALLOC_OFF(scratch_memory, sizeof(GpuStreamingCmdHeader));
      dst_header->cmd                = kModelGpuStreamContent;

      // Fill in the statistics
      dst_header->request_timestamp  = begin_cpu_profiler_timestamp();
      dst_header->io_byte_count      = gpu_io_byte_count;

      dst_header->gpu_fence_value    = flush_gpu_cmds(streamer);

      auto* dst_pkt                  = (ModelGpuContentStreamingPacket*)ALLOC_OFF(scratch_memory, sizeof(ModelGpuContentStreamingPacket));
      dst_pkt->model                 = model;
      dst_pkt->asset_header          = src_pkt.asset_header;
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

      // We need to wait for the materials to be initialized to finish initializing the model
      u64          scratch_size      = sizeof(AssetDependencyCmdHeader)          +
                                       sizeof(Asset*) * model->materials.size    +
                                       sizeof(ModelDependencyStreamingPacket);
      void*        dependency_memory = push_buffer_begin_edit(&streamer->asset_dependency_queue, scratch_size);
      defer { push_buffer_end_edit(&streamer->asset_dependency_queue, dependency_memory); };

      void*         scratch_memory   = dependency_memory;
                              
      auto*         dst_header       = (AssetDependencyCmdHeader*      )ALLOC_OFF(scratch_memory, sizeof(AssetDependencyCmdHeader));
      const Asset** dependencies     = (const Asset**                  )ALLOC_OFF(scratch_memory, sizeof(Asset*) * model->materials.size);
      // NOTE(bshihabi): This has to appear after the header and dependencies because the consumer pops the dependencies off first
      auto*         dst_pkt          = (ModelDependencyStreamingPacket*)ALLOC_OFF(scratch_memory, sizeof(ModelDependencyStreamingPacket));

      dst_header->cmd                = kModelStreamDependencies;
      dst_header->dependency_count   = (u32)model->materials.size;
      dst_header->dependencies       = dependencies;
      dst_header->pkt_size           = sizeof(ModelDependencyStreamingPacket);
      dst_header->pkt                = dst_pkt;
      dst_pkt->model                 = model;
      dst_pkt->asset_header          = src_pkt.asset_header;

      // Fill the dependencies
      for (u32 imaterial = 0; imaterial < model->materials.size; imaterial++, dependencies++)
      {
        MaterialHandle material = model->materials[imaterial];
        *dependencies = material.to_asset();
      }

      model->asset.state = kAssetUninitialized;

      streamer->num_asset_waiting_for_dependencies++;
    } break;
    default: UNREACHABLE; break;
  }
}

static void
process_model_dep_request(AssetStreamer* streamer, AssetDependencyCmdHeader header)
{
  switch (header.cmd)
  {
    // Streaming in of the header
    case kModelStreamDependencies:
    {
      // Pop the rest of the packet off of the queue
      ModelDependencyStreamingPacket src_pkt;
      push_buffer_pop(&streamer->asset_dependency_queue, &src_pkt, sizeof(src_pkt));

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

      // Patch all of the material gpu IDs
      for (u32 isubset = 0; isubset < model->subsets.size; isubset++)
      {
        model->subsets[isubset].mat_gpu_id = model->materials[isubset]->gpu_id;
      }

      // The asset is now ready
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
  ret.gpu_material_grv            = alloc_table_descriptor(g_DescriptorCbvSrvUavPool, kGrvTemporalCount * kBackBufferCount + kMaterialBufferSlot);

  GpuBufferSrvDesc srv_desc;
  srv_desc.first_element = 0;
  srv_desc.num_elements  = kMaxAssets;
  srv_desc.stride        = sizeof(MaterialGpu);
  srv_desc.format        = kGpuFormatUnknown;
  srv_desc.is_raw        = false;
  init_buffer_srv(&ret.gpu_material_grv, &ret.gpu_material_buffer, srv_desc);
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
    void* file_io_memory = push_buffer_begin_edit(&streamer->header_file_io_buffer, scratch_size);
    defer { push_buffer_end_edit(&streamer->header_file_io_buffer, file_io_memory); };

    void* scratch_memory = file_io_memory;

    FileStreamingCmdHeader*            header = (FileStreamingCmdHeader*           )ALLOC_OFF(scratch_memory, sizeof(FileStreamingCmdHeader));
    MaterialFileHeaderStreamingPacket* pkt    = (MaterialFileHeaderStreamingPacket*)ALLOC_OFF(scratch_memory, sizeof(MaterialFileHeaderStreamingPacket));


    header->cmd               = kMaterialCpuStreamHeader;
    pkt->material             = material;
    pkt->size                 = sizeof(MaterialAsset);
    pkt->file_stream          = file_open_ok.value();

    // Fill in the statistics
    header->io_byte_count     = pkt->size;
    header->request_timestamp = begin_cpu_profiler_timestamp();

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
      push_buffer_pop(&streamer->header_file_io_buffer, &src_pkt, sizeof(src_pkt));

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

      void* file_io_memory = push_buffer_begin_edit(&streamer->content_file_io_buffer, scratch_size);
      defer { push_buffer_end_edit(&streamer->content_file_io_buffer, file_io_memory); };

      void* scratch_memory = file_io_memory;

      auto* dst_header               = (FileStreamingCmdHeader*            )ALLOC_OFF(scratch_memory, sizeof(FileStreamingCmdHeader));
      dst_header->cmd                = kMaterialCpuStreamContent;
      dst_header->file_promise       = {0};

      auto* dst_pkt                  = (MaterialFileContentStreamingPacket*)ALLOC_OFF(scratch_memory, sizeof(MaterialFileContentStreamingPacket));
      dst_pkt->material              = material;
      dst_pkt->file_stream           = src_pkt.file_stream;
      dst_pkt->asset_header          = src_pkt.asset_header;
      dst_pkt->buf                   = ALLOC_OFF(scratch_memory, read_size);
      dst_pkt->size                  = read_size;

      // Fill in the statistics
      dst_header->io_byte_count      = dst_pkt->size;
      dst_header->request_timestamp  = begin_cpu_profiler_timestamp();

      Result<void, FileError> stream_ok = read_file(dst_pkt->file_stream, &dst_header->file_promise, dst_pkt->buf, dst_pkt->size, src_pkt.size);
      if (!stream_ok)
      {
        dbgln("Failed to stream asset 0x%x. File read failed.", asset_id);
        material->asset.state   = kAssetFailedToLoad;
        return;
      }
    } break;
    case kMaterialCpuStreamContent:
    {
      MaterialFileContentStreamingPacket src_pkt;
      push_buffer_pop(&streamer->content_file_io_buffer, &src_pkt, sizeof(src_pkt));

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

      defer { push_buffer_pop(&streamer->content_file_io_buffer, src_pkt.size); };

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
        if (!dst->is_loaded())
        {
          unstreamed_textures++;
        }
      }

      // We need to wait for the textures to be initialized to finish initializing the material
      u64          scratch_size      = sizeof(AssetDependencyCmdHeader)          +
                                       sizeof(Asset*) * unstreamed_textures      +
                                       sizeof(MaterialDependencyStreamingPacket);
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
      dst_header->pkt_size           = sizeof(MaterialDependencyStreamingPacket);
      dst_header->pkt                = dst_pkt;
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

      streamer->num_asset_waiting_for_dependencies++;
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
        if (!material->textures[itexture].is_loaded() && !material->textures[itexture].is_broken())
        {
          ASSERT_MSG_FATAL(material->textures[itexture].is_loaded(), "Material textures should have loaded before process_material_dep_request was reached! This is a bug in the asset streamer.");
          return;
        }
      }

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

      auto* dst_header               = (GpuStreamingCmdHeader*            )ALLOC_OFF(scratch_memory, sizeof(GpuStreamingCmdHeader));
      dst_header->cmd                = kMaterialGpuStreamContent;
      dst_header->request_timestamp  = begin_cpu_profiler_timestamp();
      dst_header->gpu_fence_value    = flush_gpu_cmds(streamer);

      // Fill in the statistics
      dst_header->io_byte_count      = sizeof(MaterialGpu);

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

struct TextureInitializationPacket
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
    void* file_io_memory = push_buffer_begin_edit(&streamer->header_file_io_buffer, scratch_size);
    defer { push_buffer_end_edit(&streamer->header_file_io_buffer, file_io_memory); };

    void* scratch_memory = file_io_memory;

    FileStreamingCmdHeader*           header = (FileStreamingCmdHeader*          )ALLOC_OFF(scratch_memory, sizeof(FileStreamingCmdHeader));
    TextureFileHeaderStreamingPacket* pkt    = (TextureFileHeaderStreamingPacket*)ALLOC_OFF(scratch_memory, sizeof(TextureFileHeaderStreamingPacket));

    header->cmd               = kTextureCpuStreamHeader;
    pkt->texture              = texture;
    pkt->size                 = sizeof(TextureAsset);
    pkt->file_stream          = file_open_ok.value();

    // Fill in the statistics
    header->io_byte_count     = pkt->size;
    header->request_timestamp = begin_cpu_profiler_timestamp();

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
      push_buffer_pop(&streamer->header_file_io_buffer, &src_pkt, sizeof(src_pkt));

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
      CPU_PROFILE_SCOPE("Texture CPU Stream Content", asset_id_str);

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

      // Copy in all the metadata
      texture->width       = src_pkt.asset_header.width;
      texture->height      = src_pkt.asset_header.height;
      texture->color_space = src_pkt.asset_header.color_space;

      // Allocate the scratch data for the content read
      u64   read_size    = src_pkt.asset_header.compressed_size;

      u64   scratch_size = sizeof(FileStreamingCmdHeader)            +
                           sizeof(TextureFileContentStreamingPacket) +
                           read_size;

      void* file_io_memory = push_buffer_begin_edit(&streamer->content_file_io_buffer, scratch_size);
      defer { push_buffer_end_edit(&streamer->content_file_io_buffer, file_io_memory); };

      void* scratch_memory = file_io_memory;

      // Fill in the packet data
      auto* dst_header               = (FileStreamingCmdHeader*           )ALLOC_OFF(scratch_memory, sizeof(FileStreamingCmdHeader));
      dst_header->cmd                = kTextureCpuStreamContent;
      dst_header->file_promise       = {0};

      auto* dst_pkt                  = (TextureFileContentStreamingPacket*)ALLOC_OFF(scratch_memory, sizeof(TextureFileContentStreamingPacket));
      dst_pkt->texture               = texture;
      dst_pkt->file_stream           = src_pkt.file_stream;
      dst_pkt->asset_header          = src_pkt.asset_header;
      dst_pkt->buf                   = ALLOC_OFF(scratch_memory, read_size);
      dst_pkt->size                  = read_size;

      // Fill in the statistics
      dst_header->io_byte_count      = dst_pkt->size;
      dst_header->request_timestamp  = begin_cpu_profiler_timestamp();

      // Issue the async file I/O read request
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
      push_buffer_pop(&streamer->content_file_io_buffer, &src_pkt, sizeof(src_pkt));

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
      u8*      buf          = (u8*)src_pkt.buf - sizeof(TextureAsset);
      Texture* texture      = src_pkt.texture;
      AssetId  asset_id     = texture->asset.id;
      u8*      texture_data = buf + src_pkt.asset_header.data;

      texture->asset.state = kAssetStreaming;

      char asset_id_str[512];
      asset_id_to_path(asset_id_str, asset_id);
      CPU_PROFILE_SCOPE("Texture GPU Stream Content", asset_id_str);

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

      defer { push_buffer_pop(&streamer->content_file_io_buffer, src_pkt.size); };

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
      u8* gpu_scratch_mapped_base = (u8*)unwrap(streamer->gpu_staging_buffer.buffer.mapped);
      u8* gpu_scratch_mapped      = gpu_scratch_mapped_base + alloc_gpu_staging_bytes_blocking(streamer, src_pkt.asset_header.uncompressed_size, kGpuTextureAlignment);

      // Copy the data to staging buffer
      memcpy(gpu_scratch_mapped, texture_data, src_pkt.asset_header.uncompressed_size);

      // Allocate the GpuTexture
      // TODO(bshihabi): Make this reserve virtual memory and only make required mips resident
      GpuTextureDesc gpu_texture_desc = {0};
      gpu_texture_desc.width             = texture->width;
      gpu_texture_desc.height            = texture->height;
      gpu_texture_desc.array_size        = 1;
      gpu_texture_desc.format            = src_pkt.asset_header.gpu_format;
      gpu_texture_desc.color_clear_value = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
      texture->gpu_texture               = alloc_gpu_texture(g_GpuDevice, streamer->gpu_texture_allocator, gpu_texture_desc, "Content Gpu Texture");

      gpu_copy_texture(
        &streamer->gpu_cmd_buffer,
        &texture->gpu_texture,
        streamer->gpu_staging_buffer.buffer,
        gpu_scratch_mapped - gpu_scratch_mapped_base,
        src_pkt.asset_header.uncompressed_size
      );
      gpu_texture_layout_transition(&streamer->gpu_cmd_buffer, &texture->gpu_texture, kGpuTextureLayoutGeneral);

      u64   scratch_size      = sizeof(GpuStreamingCmdHeader) + sizeof(TextureGpuContentStreamingPacket);
      void* gpu_stream_memory = push_buffer_begin_edit(&streamer->gpu_io_buffer, scratch_size);
      defer { push_buffer_end_edit(&streamer->gpu_io_buffer, gpu_stream_memory); };

      void* scratch_memory        = gpu_stream_memory;

      auto* dst_header               = (GpuStreamingCmdHeader*           )ALLOC_OFF(scratch_memory, sizeof(GpuStreamingCmdHeader));
      dst_header->cmd                = kTextureGpuStreamContent;

      // Fill in the statistics
      dst_header->io_byte_count      = src_pkt.asset_header.uncompressed_size;
      dst_header->request_timestamp  = begin_cpu_profiler_timestamp();

      dst_header->gpu_fence_value    = flush_gpu_cmds(streamer);

      auto* dst_pkt                  = (TextureGpuContentStreamingPacket*)ALLOC_OFF(scratch_memory, sizeof(TextureGpuContentStreamingPacket));
      dst_pkt->texture               = texture;
      dst_pkt->asset_header          = src_pkt.asset_header;
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

      // Now that the Gpu is done initialization, we need to allocate the descriptor on the main thread
      u64   scratch_size      = sizeof(MainThreadCmdHeader) + sizeof(TextureInitializationPacket);
      void* gpu_stream_memory = push_buffer_begin_edit(&streamer->main_thread_cmd_queue, scratch_size);
      defer { push_buffer_end_edit(&streamer->main_thread_cmd_queue, gpu_stream_memory); };

      void* scratch_memory    = gpu_stream_memory;

      auto* dst_header        = (MainThreadCmdHeader*        )ALLOC_OFF(scratch_memory, sizeof(MainThreadCmdHeader));
      dst_header->cmd         = kTextureMainThreadInitialize;
                              
      auto* dst_pkt           = (TextureInitializationPacket*)ALLOC_OFF(scratch_memory, sizeof(TextureInitializationPacket));
      dst_pkt->texture        = texture;
      dst_pkt->asset_header   = src_pkt.asset_header;

      texture->asset.state    = kAssetUninitialized;
    } break;
    default: UNREACHABLE; break;
  }
}

static void
process_texture_main_thread(PushBuffer* main_thread_cmd_queue, MainThreadCmdHeader header)
{
  switch (header.cmd)
  {
    case kTextureMainThreadInitialize:
    {
      TextureGpuContentStreamingPacket src_pkt;
      push_buffer_pop(main_thread_cmd_queue, &src_pkt, sizeof(src_pkt));

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

      // Allocate the descriptor on the main thread and initialize the SRV.
      texture->srv_descriptor = alloc_descriptor(g_DescriptorCbvSrvUavPool);
      GpuTextureSrvDesc desc;
      desc.mip_levels        = 1;
      desc.most_detailed_mip = 0;
      desc.array_size        = 1;
      desc.format            = texture->gpu_texture.desc.format;
      init_texture_srv(&texture->srv_descriptor, &texture->gpu_texture, desc);

      texture->asset.state   = kAssetReady;
    } break;
    default: UNREACHABLE; break;
  }
}

static void
process_header_file_io(AssetStreamer* streamer)
{
  static constexpr u32 kMaxContentFileIoAssetsInFlight = 8;

  // Rate limit the header file requests because file I/O buffer is often bottleneck and we don't want to fill it up too fast
  while (streamer->file_io_assets_in_flight < kMaxContentFileIoAssetsInFlight)
  {
    // Pop the next file I/O command off the stack if ready and we don't already have one that we're waiting for
    if (streamer->next_header_file_io_cmd.cmd == kNullStreamingCmd)
    {
      if (!try_push_buffer_pop(&streamer->header_file_io_buffer, &streamer->next_header_file_io_cmd, sizeof(FileStreamingCmdHeader)))
      {
        return;
      }
    }

    AwaitError ready = await_io(streamer->next_header_file_io_cmd.file_promise, kFileIOBlockRateMs);
    // If it's still in flight after some time, then move on and try again later
    if (ready == kAwaitInFlight)
    {
      return;
    }

    // Add the statistics
    u64 io_time_ms = (u64)end_cpu_profiler_timestamp(streamer->next_header_file_io_cmd.request_timestamp);
    atomic_add(&g_AssetStreamingStats.file_io_bpf,        streamer->next_header_file_io_cmd.io_byte_count);
    atomic_add(&g_AssetStreamingStats.file_io_elapsed_ms, io_time_ms);

    switch (streamer->next_header_file_io_cmd.cmd)
    {
      case kModelCpuStreamHeader:    process_model_file_request   (streamer, streamer->next_header_file_io_cmd, ready);  break;
      case kMaterialCpuStreamHeader: process_material_file_request(streamer, streamer->next_header_file_io_cmd, ready);  break;
      case kTextureCpuStreamHeader:  process_texture_file_request (streamer, streamer->next_header_file_io_cmd, ready);  break;
      default: ASSERT_MSG_FATAL(false, "Invalid streaming command received %u!", streamer->next_header_file_io_cmd.cmd); return;
    }
    zero_memory(&streamer->next_header_file_io_cmd, sizeof(streamer->next_header_file_io_cmd));
    streamer->file_io_assets_in_flight++;
  }
}

static void
process_content_file_io(AssetStreamer* streamer)
{
  // Consume in-flight content file I/O requests as fast as possible since they are the bottleneck typically
  while (true)
  {
    // Pop the next file I/O command off the stack if ready and we don't already have one that we're waiting for
    if (streamer->next_content_file_io_cmd.cmd == kNullStreamingCmd)
    {
      if (!try_push_buffer_pop(&streamer->content_file_io_buffer, &streamer->next_content_file_io_cmd, sizeof(FileStreamingCmdHeader)))
      {
        return;
      }
    }

    AwaitError ready = await_io(streamer->next_content_file_io_cmd.file_promise, kFileIOBlockRateMs);
    // If it's still in flight after some time, then move on and try again later
    if (ready == kAwaitInFlight)
    {
      return;
    }

    // Add the statistics
    u64 io_time_ms = (u64)end_cpu_profiler_timestamp(streamer->next_content_file_io_cmd.request_timestamp);
    atomic_add(&g_AssetStreamingStats.file_io_bpf,        streamer->next_content_file_io_cmd.io_byte_count);
    atomic_add(&g_AssetStreamingStats.file_io_elapsed_ms, io_time_ms);

    switch (streamer->next_content_file_io_cmd.cmd)
    {
      case kModelCpuStreamContent:    process_model_file_request   (streamer, streamer->next_content_file_io_cmd, ready); break;
      case kMaterialCpuStreamContent: process_material_file_request(streamer, streamer->next_content_file_io_cmd, ready); break;
      case kTextureCpuStreamContent:  process_texture_file_request (streamer, streamer->next_content_file_io_cmd, ready); break;
      default: ASSERT_MSG_FATAL(false, "Invalid streaming command received %u!", streamer->next_content_file_io_cmd.cmd); return;
    }
    zero_memory(&streamer->next_content_file_io_cmd, sizeof(streamer->next_content_file_io_cmd));

    ASSERT_MSG_FATAL(streamer->file_io_assets_in_flight > 0, "file_io_assets_in_flight is 0 which means there is a mismatch between increments and decrements for the rate limiter. This is a bug in the asset streamer.");
    streamer->file_io_assets_in_flight--;
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

    // Add the statistics
    u64 io_time_ms = (u64)end_cpu_profiler_timestamp(streamer->next_gpu_cmd.request_timestamp);
    atomic_add(&g_AssetStreamingStats.gpu_io_bpf,        streamer->next_gpu_cmd.io_byte_count);
    atomic_add(&g_AssetStreamingStats.gpu_io_elapsed_ms, io_time_ms);

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
      ASSERT_MSG_FATAL(false, "Invalid streaming command received %u!", streamer->next_content_file_io_cmd.cmd);
    }
  }
}

static void
process_main_thread(AssetStreamer* streamer)
{
  while (true)
  {
    MainThreadCmdHeader header;
    if (!try_push_buffer_pop(&streamer->main_thread_cmd_queue, &header, sizeof(header)))
    {
      return;
    }

    switch (header.cmd)
    {
      case kTextureMainThreadInitialize: process_texture_main_thread(&streamer->main_thread_cmd_queue, header); break;
      default: ASSERT_MSG_FATAL(false, "Invalid streaming command received %u!", header.cmd); break;
    }
  }
}

static void
process_asset_dependencies(AssetStreamer* streamer)
{
  // TODO(bshihabi): This is a bit hacky, but we basically copy all of the packets around to avoid cyclical dependencies.
  // This is my temporary solution for "out of order" consumption queue.
  u32 asset_count = streamer->num_asset_waiting_for_dependencies;
  for (u32 iasset = 0; iasset < asset_count; iasset++)
  {
    AssetDependencyCmdHeader header;
    push_buffer_pop(&streamer->asset_dependency_queue, &header, sizeof(header));

    const Asset** dependencies = header.dependencies;
    for (u32 iasset = 0; iasset < header.dependency_count; iasset++)
    {
      // If one of the assets isn't ready
      if (dependencies[iasset]->state < kAssetReady)
      {
        // Push the packet back onto the queue so that we can try again next time around
        u64   scratch_size      = sizeof(AssetDependencyCmdHeader)         +
                                  sizeof(Asset*) * header.dependency_count +
                                  header.pkt_size;
        void* dependency_memory = push_buffer_begin_edit(&streamer->asset_dependency_queue, scratch_size);

        void* scratch_memory    = dependency_memory;

        auto* dst_header        = (AssetDependencyCmdHeader*)ALLOC_OFF(scratch_memory, sizeof(AssetDependencyCmdHeader));
        void* dst_dependencies  =                            ALLOC_OFF(scratch_memory, sizeof(Asset*) * header.dependency_count);
        void* dst_pkt           =                            ALLOC_OFF(scratch_memory, header.pkt_size);

        memcpy(dst_header,       &header,      sizeof(AssetDependencyCmdHeader));
        memcpy(dst_dependencies, dependencies, sizeof(Asset*) * header.dependency_count);
        memcpy(dst_pkt,          header.pkt,   header.pkt_size);

        // Patch up all of the pointers since everything moved
        dst_header->dependencies = (const Asset**)dst_dependencies;
        dst_header->pkt          = dst_pkt;

        // End the edit before popping: write semaphore must be 0 for pop to read through the old data
        push_buffer_end_edit(&streamer->asset_dependency_queue, dependency_memory);

        // Pop off the old packet since we pushed it around the queue
        push_buffer_pop(&streamer->asset_dependency_queue, sizeof(Asset*) * header.dependency_count + header.pkt_size);

        return;
      }
    }

    push_buffer_pop(&streamer->asset_dependency_queue, sizeof(Asset*) * header.dependency_count);

    switch (header.cmd)
    {
      case kModelStreamDependencies:    process_model_dep_request   (streamer, header); break;
      case kMaterialStreamDependencies: process_material_dep_request(streamer, header); break;
      default: ASSERT_MSG_FATAL(false, "Invalid streaming command received %u!", header.cmd);
    }

    streamer->num_asset_waiting_for_dependencies--;
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
      material->asset.state = asset_id == kNullAssetId ? kAssetFailedToLoad : kAssetUnloaded;
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

  if (asset_id != kNullAssetId)
  {
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
      texture->asset.state = asset_id == kNullAssetId ? kAssetFailedToLoad : kAssetUnloaded;

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

  if (asset_id != kNullAssetId)
  {
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
  }

  return ret;
}

static u32
asset_streaming_thread(void* param)
{
  AssetStreamer* streamer = (AssetStreamer*)param;
  while (!atomic_load(streamer->kill))
  {
    // Consume stuff from the asset stream queue to kick off asset loads
    //
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
    process_header_file_io(streamer);
    process_content_file_io(streamer);
    process_gpu_io(streamer);
    process_asset_dependencies(streamer);
  }

  return 0;
}

static AssetStreamer*
init_asset_streamer_impl(void)
{
  AssetStreamer* ret            = HEAP_ALLOC(AssetStreamer, g_InitHeap, 1);
  ret->asset_stream_requests    = init_ring_queue<AssetStreamRequest>(g_InitHeap, kMaxAssetLoadRequests);


  // TODO(bshihabi): These should probably be adjusted
  u64 kHeaderFileIOBufferSize   = MiB(4);
  u64 kContentFileIOBufferSize  = MiB(256);
  u64 kGpuStreamQueueSize       = MiB(128);
  u64 kAssetDependencyQueueSize = MiB(8);
  u64 kMainThreadQueueSize      = MiB(1);
  u32 kGpuStagingBufferSize     = MiB(128);
  u32 kGpuScratchBufferSize     = MiB(8);

  u32 kGpuTextureHeapSize       = MiB(800);

  ret->header_file_io_buffer              = init_push_buffer(KiB(1),  kHeaderFileIOBufferSize,   MiB(8));
  ret->content_file_io_buffer             = init_push_buffer(MiB(32), kContentFileIOBufferSize,  GiB(1));
  ret->gpu_io_buffer                      = init_push_buffer(KiB(1),  kGpuStreamQueueSize,       GiB(1));
  ret->asset_dependency_queue             = init_push_buffer(KiB(4),  kAssetDependencyQueueSize, MiB(16));
  ret->main_thread_cmd_queue              = init_push_buffer(KiB(4),  kMainThreadQueueSize,      MiB(1));
  ret->next_header_file_io_cmd.cmd        = kNullStreamingCmd;
  ret->next_content_file_io_cmd.cmd       = kNullStreamingCmd;
  ret->next_gpu_cmd.cmd                   = kNullStreamingCmd;
  ret->num_asset_waiting_for_dependencies = 0;

  GpuBufferDesc staging_desc    = {0};
  staging_desc.size             = kGpuStagingBufferSize;
  ret->gpu_staging_buffer       = alloc_gpu_ring_buffer_no_heap(g_InitHeap, staging_desc, kGpuHeapSysRAMCpuToGpu, "Streaming Upload Buffer");

  GpuBufferDesc scratch_desc    = {0};
  staging_desc.size             = kGpuScratchBufferSize;
  scratch_desc.flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ret->gpu_scratch_buffer       = alloc_gpu_buffer_no_heap(g_GpuDevice, staging_desc, kGpuHeapGpuOnly, "BLAS Scratch Buffer");

  ret->gpu_texture_allocator    = init_gpu_linear_allocator(kGpuTextureHeapSize, kGpuHeapGpuOnly);

  ret->gpu_cmd_buffer_allocator = init_cmd_list_allocator(g_InitHeap, g_GpuDevice, &g_GpuDevice->compute_queue, 64);
  ret->gpu_cmd_buffer           = alloc_cmd_list(&ret->gpu_cmd_buffer_allocator);

  u64 kModelSubsetAllocatorSize = MiB(16);
  ret->metadata_allocator       = init_linear_allocator(kModelSubsetAllocatorSize, GiB(1));


  static constexpr u64 kAssetStreamerStackSize = MiB(4);
  static constexpr u32 kAssetStreamingCoreIdx  = 7;
  ret->thread = init_thread(g_InitHeap, kAssetStreamerStackSize, &asset_streaming_thread, (void*)ret, kAssetStreamingCoreIdx);

  set_thread_name(&ret->thread, L"Asset Streaming Thread");

  g_AssetStreamingStats.file_io_bpf           = 0;
  g_AssetStreamingStats.file_io_elapsed_ms = 0;
  g_AssetStreamingStats.gpu_io_bpf            = 0;
  g_AssetStreamingStats.gpu_io_elapsed_ms  = 0;
  g_AssetStreamingStats.file_io_bps           = 0.0;
  g_AssetStreamingStats.gpu_io_bps            = 0.0;

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
  push_buffer_flush(&streamer->content_file_io_buffer);
  push_buffer_flush(&streamer->gpu_io_buffer);
  push_buffer_flush(&streamer->main_thread_cmd_queue);
}

void
asset_streamer_update(void)
{
  asset_streamer_flush(g_AssetStreamer);
  process_main_thread(g_AssetStreamer);

  u64 file_io_bytes       = atomic_exchange(&g_AssetStreamingStats.file_io_bpf,        0);
  u64 file_io_elapsed_ms  = atomic_exchange(&g_AssetStreamingStats.file_io_elapsed_ms, 0);
  f64 file_io_elapsed_sec = (f64)file_io_elapsed_ms / (f64)1000.0f;
  u64 gpu_io_bytes        = atomic_exchange(&g_AssetStreamingStats.gpu_io_bpf,         0);
  u64 gpu_io_elapsed_ms   = atomic_exchange(&g_AssetStreamingStats.gpu_io_elapsed_ms,  0);
  f64 gpu_io_elapsed_sec  = (f64)gpu_io_elapsed_ms  / (f64)1000.0f;

  // Moving average
  static constexpr f64 kHysteresis = 0.5;

  if (file_io_bytes == 0)
  {
    g_AssetStreamingStats.file_io_bps = kHysteresis * 0.0 + (1.0 - kHysteresis) * g_AssetStreamingStats.file_io_bps;
  }
  else if (file_io_elapsed_sec > 0)
  {
    f64 sample_bytes_per_sec          = (f64)file_io_bytes / (f64)file_io_elapsed_sec;
    g_AssetStreamingStats.file_io_bps = kHysteresis * sample_bytes_per_sec + (1.0 - kHysteresis) * g_AssetStreamingStats.file_io_bps;
  }

  if (gpu_io_bytes == 0)
  {
    g_AssetStreamingStats.gpu_io_bps = kHysteresis * 0.0 + (1.0 - kHysteresis) * g_AssetStreamingStats.gpu_io_bps;
  }
  else if (gpu_io_elapsed_sec > 0)
  {
    f64 sample                       = (f64)gpu_io_bytes / (f64)gpu_io_elapsed_sec;
    g_AssetStreamingStats.gpu_io_bps = kHysteresis * sample + (1.0 - kHysteresis) * g_AssetStreamingStats.gpu_io_bps;
  }
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
      model->asset.state = asset_id == kNullAssetId ? kAssetFailedToLoad : kAssetUnloaded;

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

  if (asset_id != kNullAssetId)
  {
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
  }

  return ret;
}

