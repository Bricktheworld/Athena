#include <winsock2.h>
#include <ws2tcpip.h>

#include "Core/Foundation/types.h"
#include "Core/Foundation/Networking/socket.h"


#pragma comment(lib, "ws2_32.lib")

Result<TcpServer, SocketErr>
init_tcp_server(AllocHeap heap, u16 port, u32 max_connections)
{
  TcpServer ret     = {0};

  ASSERT_MSG_FATAL(max_connections <= SOMAXCONN, "%u connections not supported on win32 for TCP server! Maximum is %u", max_connections, SOMAXCONN);

  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
  {
    return Err(kSocketFailedToStartup);
  }
  defer { if (!ret.listen_fd) WSACleanup(); };

  sockaddr_in addr     = {0};
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port        = htons(port);

  SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listen_socket == INVALID_SOCKET)
  {
    ASSERT_MSG(false, "Failed to open listen socket %d", WSAGetLastError());
    return Err(kSocketFailedToOpenSocket);
  }
  defer { if (!ret.listen_fd) closesocket(listen_socket); };

  // Set socket to be non-blocking
  u_long mode = 1;
  if (ioctlsocket(listen_socket, FIONBIO, &mode) == SOCKET_ERROR)
  {
    ASSERT_MSG(false, "Failed to set listen socket as non-blocking %d", WSAGetLastError());
    return Err(kSocketFailedToOpenSocket);
  }

  if (bind(listen_socket, (sockaddr*)&addr, (s32)sizeof(addr)) == SOCKET_ERROR)
  {
    ASSERT_MSG(false, "Failed to bind socket %d", WSAGetLastError());
    return Err(kSocketFailedToBindSocket);
  }

  if (listen(listen_socket, max_connections))
  {
    ASSERT_MSG(false, "Failed to listen on socket %d", WSAGetLastError());
    return Err(kSocketFailedToListen);
  }

  ret.listen_fd  = listen_socket;
  ret.client_fds = init_array<u64>(heap, max_connections);
  return Ok(ret);
}

Result<u32, SocketErr>
tcp_server_accept_connections(TcpServer* server)
{
  // Remove old connections
  for (s64 iclient = (s64)server->client_fds.size - 1; iclient >= 0; iclient--)
  {
    if (server->client_fds[iclient] == INVALID_SOCKET)
    {
      array_remove(&server->client_fds, iclient);
    }
  }

  u32 new_connection_count = 0;
  for (;;)
  {
    SOCKET client_fd = accept(server->listen_fd, nullptr, nullptr);
    if (client_fd == SOCKET_ERROR)
    {
      s32 err = WSAGetLastError();
      if (err != WSAEWOULDBLOCK)
      {
        ASSERT_MSG(false, "Failed to accept TCP socket %d", err);
        // This is an actual error trying to accept
        return Err(kSocketFailedToAccept);
      }
      else
      {
        // Will get accepted next time around
        return Ok(new_connection_count);
      }
    }

    if (server->client_fds.size >= server->client_fds.capacity)
    {
      ASSERT_MSG(false, "Cannot accept any more clients! Already have maximum %u connections.", server->client_fds.size);
      break;
    }

    *array_add(&server->client_fds) = client_fd;
    new_connection_count++;
    // Set the client socket to be non-blocking
    u_long mode = 1;
    ioctlsocket(client_fd, FIONBIO, &mode);
  }

  return Ok(new_connection_count);
}

SocketErr
tcp_server_send(TcpServer* server, u32 client_idx, const u8* src, u64 len)
{
  u64 client_fd = server->client_fds[client_idx];
  if (client_fd == INVALID_SOCKET)
  {
    return kSocketDisconnected;
  }

  while (len > 0)
  {
    s64 res = (s64)send(client_fd, (const char*)src, (s32)len, 0);
    // An error occured
    if (res == SOCKET_ERROR)
    {
      s32 err = WSAGetLastError();
      if (err == WSAENOTCONN)
      {
        server->client_fds[client_idx] = INVALID_SOCKET;
        return kSocketDisconnected;
      }
      else
      {
        ASSERT_MSG(false, "Failed to send TCP message %d", err);
        return kSocketFailedToSend;
      }
    }

    ASSERT_MSG_FATAL(res <= (s64)len, "It should not be possible for WSA send to send more than the number of bytes specified. %lld > %llu", res, len);

    len  -= (u64)res;
    src += res;
  }

  return kSocketOk;
}

Result<u64, SocketErr>
tcp_server_recv(TcpServer* server, u32 client_idx, u8* dst, u64 len)
{
  if (server->client_fds[client_idx] == INVALID_SOCKET)
  {
    return Err(kSocketDisconnected);
  }

  s64 res = (s64)recv(server->client_fds[client_idx], (char*)dst, (s32)len, 0);
  if (res == SOCKET_ERROR)
  {
    s32 err = WSAGetLastError();
    // Means that there's nothing to receive
    if (err == WSAEWOULDBLOCK)
    {
      return Ok(0ull);
    }
    else if (err == WSAEMSGSIZE)
    {
      return Ok(len);
    }
    else
    {
      ASSERT_MSG(false, "Failed to receive TCP message %d", WSAGetLastError());
      return Err(kSocketFailedToRecv);
    }
  }
  else if (res == 0)
  {
    closesocket(server->client_fds[client_idx]);
    server->client_fds[client_idx] = INVALID_SOCKET;
    return Err(kSocketDisconnected);
  }
  else
  {
    return Ok((u64)res);
  }
}

void
destroy_tcp_server(TcpServer* server)
{
  closesocket(server->listen_fd);
  WSACleanup();
}

Result<TcpClient, SocketErr>
init_tcp_client()
{
  TcpClient ret = {0};

  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
  {
    return Err(kSocketFailedToStartup);
  }
  defer { if (!ret.client_fd) WSACleanup(); };

  SOCKET client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (client_socket == INVALID_SOCKET)
  {
    ASSERT_MSG(false, "Failed to open client socket %d", WSAGetLastError());
    return Err(kSocketFailedToOpenSocket);
  }
  defer { if (!ret.client_fd) closesocket(client_socket); };

  // Set socket to be non-blocking
  u_long mode = 1;
  if (ioctlsocket(client_socket, FIONBIO, &mode) == SOCKET_ERROR)
  {
    ASSERT_MSG(false, "Failed to set client socket as non-blocking %d", WSAGetLastError());
    return Err(kSocketFailedToOpenSocket);
  }

  ret.connected = false;
  ret.client_fd = client_socket;

  return Ok(ret);
}

SocketErr
tcp_client_connect(TcpClient* client, const char* addr, u16 port)
{
  sockaddr_in server_addr = {0};
  server_addr.sin_family  = AF_INET;
  server_addr.sin_port    = htons(port);
  inet_pton(AF_INET, addr, &server_addr.sin_addr);

  if (connect(client->client_fd, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR)
  {
    s32 err = WSAGetLastError();
    if (err == WSAEISCONN)
    {
      // Handled by cleanup, we're already connected
    }
    else if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS || err == WSAEALREADY)
    {
      return kSocketConnecting;
    }
    else
    {
      ASSERT_MSG(false, "Failed to connect client socket %d", WSAGetLastError());
      return kSocketFailedToConnect;
    }
  }

  client->connected = true;
  return kSocketOk;
}

SocketErr
tcp_client_send(TcpClient* client, const u8* src, u64 len)
{
  while (len > 0)
  {
    s64 ret = send(client->client_fd, (const char*)src, (s32)len, 0);
    // An error occured
    if (ret == SOCKET_ERROR)
    {
      s32 err = WSAGetLastError();
      if (err == WSAENOTCONN)
      {
        destroy_tcp_client(client);
        return kSocketDisconnected;
      }
      else
      {
        ASSERT_MSG(false, "Failed to send TCP message %d", err);
        return kSocketFailedToSend;
      }
    }

    ASSERT_MSG_FATAL(ret <= (s64)len, "It should not be possible for WSA send to send more than the number of bytes specified. %lld > %llu", ret, len);

    len  -= (u64)ret;
    src += ret;
  }

  return kSocketOk;
}

Result<u64, SocketErr>
tcp_client_recv(TcpClient* client, u8* dst, u64 len)
{
  s64 ret = (s64)recv(client->client_fd, (char*)dst, (s32)len, 0);
  if (ret == SOCKET_ERROR)
  {
    s32 err = WSAGetLastError();
    // Means that there's nothing to receive
    if (err == WSAEWOULDBLOCK)
    {
      return Ok(0ull);
    }
    else if (err == WSAEMSGSIZE)
    {
      return Ok(len);
    }
    else
    {
      ASSERT_MSG(false, "Failed to receive TCP message %d", WSAGetLastError());
      return Err(kSocketFailedToRecv);
    }
  }
  else if (ret == 0)
  {
    destroy_tcp_client(client);
    return Err(kSocketDisconnected);
  }
  else
  {
    return Ok((u64)ret);
  }
}

void
destroy_tcp_client(TcpClient* client)
{
  closesocket(client->client_fd);
  WSACleanup();

  zero_memory(client, sizeof(TcpClient));
}
