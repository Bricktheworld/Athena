#include "Core/Foundation/memory.h"
#include "Core/Foundation/context.h"
#include "Core/Foundation/Networking/socket.h"

static AllocHeap g_InitHeap;

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

    static constexpr u32 kMaxPacketSize = KiB(4);
    u8 buf[kMaxPacketSize];
    for (u32 iclient = 0; iclient < server.client_fds.size; iclient++)
    {
      Result<u64, SocketErr> res = tcp_server_recv(&server, iclient, buf, kMaxPacketSize);
      if (!res)
      {
        if (res.error() == kSocketDisconnected)
        {
          printf("Client disconnected!\n");
        }
        else
        {
          printf("Failed to recv from client: %s\n", kSocketErrStr[res.error()]);
        }
      }
      else
      {
        u64 len = res.value();
        if (len > 0 && len < kMaxPacketSize)
        {
          // Make sure its null terminated
          buf[len] = 0;
          printf("Got message from client: %s\n", (char*)buf);
        }
      }
    }
    Sleep(100);
  }
}