#pragma once
#include "Core/Foundation/pool_allocator.h"
#include "Core/Foundation/Containers/array.h"
#include "Core/Foundation/Containers/error_or.h"

enum SocketErr : u8
{
  kSocketOk,
  kSocketConnecting,
  kSocketDisconnected,
  kSocketFailedToStartup,
  kSocketFailedToGetAddrInfo,
  kSocketFailedToOpenSocket,
  kSocketFailedToBindSocket,
  kSocketFailedToListen,
  kSocketFailedToAccept,
  kSocketFailedToConnect,
  kSocketFailedToSend,
  kSocketFailedToRecv,

  kSocketErrCount,
};

static const char* kSocketErrStr[kSocketErrCount] =
{
  "Ok",
  "Connecting...",
  "Disconnected",
  "Failed to startup",
  "Failed get address info",
  "Failed to open socket",
  "Failed to bind socket",
  "Failed to listen on port",
  "Failed to accept client",
  "Failed to connect to server",
  "Failed to send",
  "Failed to recv",
};

struct TcpServer
{
  u64        listen_fd = 0;
  Array<u64> client_fds;
};

struct TcpClient
{
  u64        client_fd = 0;

  bool       connected = false;
};

FOUNDATION_API Result<TcpServer, SocketErr> init_tcp_server(AllocHeap heap, u16 port, u32 max_connections);
// Returns the number of new connections
FOUNDATION_API Result<u32, SocketErr> tcp_server_accept_connections(TcpServer* server);
FOUNDATION_API SocketErr tcp_server_send(TcpServer* server, u32 client_idx, const u8* src, u64 len);
FOUNDATION_API Result<u64, SocketErr> tcp_server_recv(TcpServer* server, u32 client_idx, u8* dst, u64 len);
FOUNDATION_API void destroy_tcp_server(TcpServer* server);

FOUNDATION_API Result<TcpClient, SocketErr> init_tcp_client();
// Returns whether socket connected or not
FOUNDATION_API DONT_IGNORE_RETURN SocketErr tcp_client_connect(TcpClient* client, const char* addr, u16 port);
FOUNDATION_API DONT_IGNORE_RETURN SocketErr tcp_client_send(TcpClient* client, const u8* src, u64 len);
// Returns number of bytes read from TCP socket, or 0 if nothing was read
FOUNDATION_API DONT_IGNORE_RETURN Result<u64, SocketErr> tcp_client_recv(TcpClient* client, u8* dst, u64 len);
FOUNDATION_API void destroy_tcp_client(TcpClient* client);
