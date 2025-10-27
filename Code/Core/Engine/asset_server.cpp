#include "Core/Engine/asset_server.h"

Result<AssetServer, SocketErr>
init_asset_server(const char* addr, u16 port)
{
  AssetServer ret = {0};
  auto client_or_err = init_tcp_client();
  if (!client_or_err)
  {
    return Err(client_or_err.error());
  }

  ret.tcp_client = client_or_err.value();

  static constexpr u32 kRetryCount = 5;

  for (u32 itry = 0; itry < kRetryCount; itry++)
  {
    SocketErr res = tcp_client_connect(&ret.tcp_client, addr, port);
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

  if (!ret.tcp_client.connected)
  {
    dbgln("Failed to connect to asset server!");
    return Err(kSocketFailedToConnect);
  }

  return Ok(ret);
}

void
destroy_asset_server(AssetServer* asset_server)
{
  destroy_tcp_client(&asset_server->tcp_client);
  zero_memory(asset_server, sizeof(AssetServer));
}
