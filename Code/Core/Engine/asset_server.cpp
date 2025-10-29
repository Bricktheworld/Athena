#include "Core/Foundation/Networking/packets.h"

#include "Core/Engine/memory.h"
#include "Core/Engine/asset_server.h"
#include "Core/Engine/Render/renderer.h"

static AssetServer* g_AssetServer = nullptr;

Result<void, SocketErr>
init_asset_server(const char* addr, u16 port)
{
  g_AssetServer = HEAP_ALLOC(AssetServer, g_InitHeap, 1);

  auto client_or_err = init_tcp_client();
  if (!client_or_err)
  {
    return Err(client_or_err.error());
  }

  g_AssetServer->tcp_client = client_or_err.value();

  static constexpr u32 kRetryCount = 5;

  for (u32 itry = 0; itry < kRetryCount; itry++)
  {
    SocketErr res = tcp_client_connect(&g_AssetServer->tcp_client, addr, port);
    if (res == kSocketOk)
    {
      dbgln("Connected to asset server on %s:%u", addr, port);
      break;
    }
    else if (res == kSocketConnecting)
    {
      dbgln("Connecting to asset server...");
    }
    else
    {
      ASSERT_MSG_FATAL(false, "TCP connection error when connecting to asset server: %s", kSocketErrStr[res]);
      return Err(res);
    }

    // TODO(bshihabi): Make this not a platform specific function
    Sleep(1000);
  }

  if (!g_AssetServer->tcp_client.connected)
  {
    dbgln("Failed to connect to asset server!");
    return Err(kSocketFailedToConnect);
  }

  g_AssetServer->packet_type = kPacketTypeUnknown;
  g_AssetServer->metadata    = {0};
  g_AssetServer->buf         = nullptr;
  g_AssetServer->dst         = (u8*)&g_AssetServer->metadata;
  g_AssetServer->dst_size    = sizeof(g_AssetServer->metadata);

  return Ok();
}

void
asset_server_update()
{
  defer {
    if (g_AssetServer->packet_type == kPacketTypeUnknown && g_AssetServer->buf)
    {
      HEAP_FREE(GLOBAL_HEAP, g_AssetServer->buf);
      g_AssetServer->buf      = nullptr;
      g_AssetServer->dst      = (u8*)&g_AssetServer->metadata;
      g_AssetServer->dst_size = sizeof(g_AssetServer->metadata);
    }
  };
  for (;;)
  {
    Result<u64, SocketErr> res = tcp_client_recv(&g_AssetServer->tcp_client, g_AssetServer->dst, g_AssetServer->dst_size);
    if (!res)
    {
      dbgln("Asset server update failed: %s", res.error());
      return;
    }

    u64 packet_len = res.value();
    if (packet_len == 0)
    {
      return;
    }

    g_AssetServer->dst_size -= packet_len;
    g_AssetServer->dst      += packet_len;

    if (g_AssetServer->dst_size > 0)
    {
      continue;
    }

    switch (g_AssetServer->packet_type)
    {
      case kPacketTypeUnknown:
      {
        PacketMetadata* metadata = &g_AssetServer->metadata;
        if (metadata->magic_number != kPacketMagicNumber)
        {
          dbgln("Received unknown/corrupted packet with invalid magic number 0x%x discarding\n", metadata->magic_number);
          return;
        }

        g_AssetServer->buf         = HEAP_ALLOC_ALIGNED(GLOBAL_HEAP, metadata->length, alignof(u64));
        g_AssetServer->dst         = g_AssetServer->buf + sizeof(PacketMetadata);
        g_AssetServer->dst_size    = metadata->length - sizeof(PacketMetadata);
        g_AssetServer->packet_type = metadata->type;

        memcpy(g_AssetServer->buf, metadata, sizeof(PacketMetadata));
      } break;
      case kHotReloadShaderRequest:
      {
        PacketHotReloadShader* packet      = (PacketHotReloadShader*)g_AssetServer->buf;
        const char*            shader_name = (const char*)((u8*)packet + packet->name);
        const u8*              shader_bin  =               (u8*)packet + packet->shader_bin;
        dbgln("Received shader hot reload request for shader %s with size %u", shader_name, packet->shader_bin_size);

        // Reset everything we're done here
        g_AssetServer->packet_type = kPacketTypeUnknown;

        reload_engine_shader(shader_name, shader_bin, packet->shader_bin_size);
      } break;
      default:
      {
        dbgln("Received unknown packet type from server 0x%x, discarding\n", g_AssetServer->packet_type);
      } break;
    }
  }
}

void
destroy_asset_server()
{
  destroy_tcp_client(&g_AssetServer->tcp_client);
  zero_memory(g_AssetServer, sizeof(AssetServer));
}
