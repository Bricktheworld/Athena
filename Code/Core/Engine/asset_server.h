#pragma once
#include "Core/Foundation/Networking/socket.h"
#include "Core/Foundation/Networking/packets.h"

struct AssetServer
{
  TcpClient       tcp_client;

  PacketType      packet_type  = kPacketTypeUnknown;
  PacketMetadata  metadata;

  u8*             buf;
  u8*             dst;
  u64             dst_size;
};

Result<void, SocketErr> init_asset_server(const char* addr, u16 port);
void asset_server_update();
void destroy_asset_server();
