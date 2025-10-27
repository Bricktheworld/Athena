#pragma once
#include "Core/Foundation/Networking/socket.h"

struct AssetServer
{
  TcpClient tcp_client;
};

Result<AssetServer, SocketErr> init_asset_server(const char* addr, u16 port);
void destroy_asset_server(AssetServer* asset_server);
