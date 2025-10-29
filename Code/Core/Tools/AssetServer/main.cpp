#include "Core/Foundation/memory.h"
#include "Core/Foundation/context.h"
#include "Core/Foundation/Containers/hash_table.h"
#include "Core/Foundation/Networking/socket.h"
#include "Core/Foundation/Networking/packets.h"

static AllocHeap g_InitHeap;

struct ClientRecvState
{
  PacketType      packet_type  = kPacketTypeUnknown;
  PacketMetadata  metadata;

  u64             buf_size;

  u8*             buf;
  u8*             dst;
  u64             dst_size;
};

int main(int argc, const char** argv)
{
  UNREFERENCED_PARAMETER(argc);
  UNREFERENCED_PARAMETER(argv);
  static constexpr size_t kInitHeapSize = MiB(128);
  u8* init_memory                = HEAP_ALLOC(u8, GLOBAL_HEAP, kInitHeapSize);
  LinearAllocator init_allocator = init_linear_allocator(init_memory, kInitHeapSize);

  g_InitHeap                     = init_allocator;

  init_context(g_InitHeap, GLOBAL_HEAP);

  Result<TcpServer, SocketErr> res = init_tcp_server(g_InitHeap, 8000, 1024);
  ASSERT_MSG_FATAL(res, "Failed to initialize TCP server! %s", kSocketErrStr[res.error()]);
  if (!res)
  {
    printf("Failed to start up asset server! %s\n", kSocketErrStr[res.error()]);
    return -1;
  }
  else
  {
    printf("Asset server listening on port %u\n", 8000);
  }

  TcpServer server = res.value();
  HashTable recv_state = init_hash_table<u64, ClientRecvState>(g_InitHeap, server.client_fds.size);

  for (;;)
  {
    Result<u32, SocketErr> res = tcp_server_accept_connections(&server);
    if (!res)
    {
      printf("TCP server encountered an error! %s\n", kSocketErrStr[res.error()]);
      continue;
    }
    else if (res.value() > 0)
    {
      printf("TCP server got %u new clients!\n", res.value());
    }

    for (u32 iclient = 0; iclient < server.client_fds.size; iclient++)
    {
      u64 client_fd = server.client_fds[iclient];
      ClientRecvState* state = hash_table_find(&recv_state, client_fd);
      if (!state)
      {
        state = hash_table_insert(&recv_state, client_fd);
        state->packet_type = kPacketTypeUnknown;
        state->metadata    = {0};
        state->buf         = nullptr;
        state->buf_size    = 0;
        state->dst         = (u8*)&state->metadata;
        state->dst_size    = sizeof(state->metadata);
      }

      do
      {
        Result<u64, SocketErr> res = tcp_server_recv(&server, iclient, state->dst, state->dst_size);
        if (!res)
        {
          if (res.error() == kSocketDisconnected)
          {
            printf("Client disconnected!\n");
            if (state->buf != nullptr)
            {
              HEAP_FREE(GLOBAL_HEAP, state->buf);
            }
            hash_table_erase(&recv_state, client_fd);
            state = nullptr;
          }
          else
          {
            printf("Failed to recv from client: %s\n", kSocketErrStr[res.error()]);
          }
          break;
        }

        u64 len = res.value();

        // We'll get it the next time around
        if (len == 0)
        {
          break;
        }

        state->dst_size -= len;
        state->dst      += len;

        // There's still more stuff we're expecting
        if (state->dst_size > 0)
        {
          continue;
        }

        switch (state->packet_type)
        {
          case kPacketTypeUnknown:
          {
            PacketMetadata* metadata = &state->metadata;
            if (metadata->magic_number != kPacketMagicNumber)
            {
              printf("Received unknown/corrupted packet with invalid magic number 0x%x discarding\n", metadata->magic_number);
              state->dst_size = 0;
              break;
            }

            switch (metadata->type)
            {
              case kHotReloadShaderRequest:
              {
                state->buf         = HEAP_ALLOC_ALIGNED(GLOBAL_HEAP, metadata->length, alignof(u64));
                state->dst         = state->buf + sizeof(PacketMetadata);
                state->dst_size    = metadata->length - sizeof(PacketMetadata);
                state->packet_type = metadata->type;

                memcpy(state->buf, &state->metadata, sizeof(PacketMetadata));
              } break; 
              default: 
              {
                printf("Received unknown packet type 0x%x, discarding\n", metadata->type);
              } break;
            }
          } break;
          case kHotReloadShaderRequest:
          {
            PacketHotReloadShader* packet      = (PacketHotReloadShader*)state->buf;
            const char*            shader_name = (const char*)((u8*)packet + packet->name);
            printf("Received shader hot reload request for shader %s with size %u\n", shader_name, packet->shader_bin_size);

            for (u32 irecipient = 0; irecipient < (u32)server.client_fds.size; irecipient++)
            {
              if (irecipient == iclient)
              {
                continue;
              }

              SocketErr ret = tcp_server_send(&server, irecipient, state->buf, packet->metadata.length);
              if (ret != kSocketOk)
              {
                printf("Failed to send buffer to client %u! %s\n", irecipient, kSocketErrStr[ret]);
              }
            }

            if (state->buf != nullptr)
            {
              HEAP_FREE(GLOBAL_HEAP, state->buf);
            }
            hash_table_erase(&recv_state, client_fd);
            state = nullptr;
          } break;
        }
      }
      while (state != nullptr && state->dst_size > 0);
    }
    Sleep(10);
  }
}