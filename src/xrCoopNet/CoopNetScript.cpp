#include "stdafx.h"
#include "CoopNet.h"

// ---------------------------------------------------------------------------
// Lua-callable functions
// ---------------------------------------------------------------------------

static int l_host(lua_State* L)
{
    int port      = luaL_checkint(L, 1);
    int max_peers = luaL_optint(L, 2, COOP_MAX_PEERS);
    lua_pushboolean(L, CoopNet_Host(port, max_peers) ? 1 : 0);
    return 1;
}

static int l_connect(lua_State* L)
{
    const char* addr = luaL_checkstring(L, 1);
    int         port = luaL_checkint(L, 2);
    lua_pushboolean(L, CoopNet_Connect(addr, port) ? 1 : 0);
    return 1;
}

static int l_disconnect(lua_State* L)
{
    CoopNet_Disconnect();
    return 0;
}

static int l_send_reliable(lua_State* L)
{
    int peer_id    = luaL_checkint(L, 1);
    int channel    = luaL_checkint(L, 2);
    size_t len     = 0;
    const char* d  = luaL_checklstring(L, 3, &len);
    lua_pushboolean(L, CoopNet_Send(peer_id, channel, d, len, true) ? 1 : 0);
    return 1;
}

static int l_send_unreliable(lua_State* L)
{
    int peer_id    = luaL_checkint(L, 1);
    int channel    = luaL_checkint(L, 2);
    size_t len     = 0;
    const char* d  = luaL_checklstring(L, 3, &len);
    lua_pushboolean(L, CoopNet_Send(peer_id, channel, d, len, false) ? 1 : 0);
    return 1;
}

static int l_broadcast_reliable(lua_State* L)
{
    int channel    = luaL_checkint(L, 1);
    size_t len     = 0;
    const char* d  = luaL_checklstring(L, 2, &len);
    int exclude    = luaL_optint(L, 3, -1);
    lua_pushboolean(L, CoopNet_Broadcast(channel, d, len, true, exclude) ? 1 : 0);
    return 1;
}

static int l_broadcast_unreliable(lua_State* L)
{
    int channel    = luaL_checkint(L, 1);
    size_t len     = 0;
    const char* d  = luaL_checklstring(L, 2, &len);
    int exclude    = luaL_optint(L, 3, -1);
    lua_pushboolean(L, CoopNet_Broadcast(channel, d, len, false, exclude) ? 1 : 0);
    return 1;
}

static int l_pump(lua_State* L)
{
    lua_pushinteger(L, CoopNet_Pump());
    return 1;
}

// Returns an array of event tables queued since the last call:
//   {type="connect"|"disconnect", peer_id=N}
//   {type="receive", peer_id=N, channel=N, data="..."}
// Deliberately the only way Lua learns about connect/disconnect/receive --
// see the comment on CoopEvent in CoopNet.h for why native code no longer
// calls back into Lua directly.
static int l_poll_events(lua_State* L)
{
    std::vector<CoopEvent> events = CoopNet_PollEvents();

    lua_newtable(L);
    int idx = 1;
    for (auto& e : events)
    {
        lua_newtable(L);

        switch (e.type)
        {
        case CoopEventType::Connect:    lua_pushstring(L, "connect");    break;
        case CoopEventType::Disconnect: lua_pushstring(L, "disconnect"); break;
        case CoopEventType::Receive:    lua_pushstring(L, "receive");    break;
        }
        lua_setfield(L, -2, "type");

        lua_pushinteger(L, e.peer_id);
        lua_setfield(L, -2, "peer_id");

        if (e.type == CoopEventType::Receive)
        {
            lua_pushinteger(L, e.channel);
            lua_setfield(L, -2, "channel");

            lua_pushlstring(L, e.data.data(), e.data.size());
            lua_setfield(L, -2, "data");
        }

        lua_rawseti(L, -2, idx++);
    }
    return 1;
}

static int l_peer_count(lua_State* L)
{
    lua_pushinteger(L, CoopNet_PeerCount());
    return 1;
}

static int l_is_host(lua_State* L)
{
    lua_pushboolean(L, CoopNet_IsServer() ? 1 : 0);
    return 1;
}

static int l_is_connected(lua_State* L)
{
    lua_pushboolean(L, CoopNet_IsConnected() ? 1 : 0);
    return 1;
}

static int l_get_ping(lua_State* L)
{
    int peer_id = luaL_checkint(L, 1);
    lua_pushinteger(L, CoopNet_GetPing(peer_id));
    return 1;
}

// Exposed channel constants so Lua doesn't hardcode numbers
static int l_channel_rel(lua_State* L)   { lua_pushinteger(L, COOP_CHANNEL_REL);   return 1; }
static int l_channel_unrel(lua_State* L) { lua_pushinteger(L, COOP_CHANNEL_UNREL); return 1; }

// ---------------------------------------------------------------------------
// Module registration
// ---------------------------------------------------------------------------

static const luaL_Reg coop_lib[] = {
    {"host",                l_host},
    {"connect",             l_connect},
    {"disconnect",          l_disconnect},
    {"send_reliable",       l_send_reliable},
    {"send_unreliable",     l_send_unreliable},
    {"broadcast_reliable",  l_broadcast_reliable},
    {"broadcast_unreliable",l_broadcast_unreliable},
    {"pump",                l_pump},
    {"poll_events",         l_poll_events},
    {"peer_count",          l_peer_count},
    {"is_host",             l_is_host},
    {"is_connected",        l_is_connected},
    {"get_ping",            l_get_ping},
    {"channel_rel",         l_channel_rel},
    {"channel_unrel",       l_channel_unrel},
    {nullptr, nullptr}
};

extern "C" __declspec(dllexport) int luaopen_coop(lua_State* L)
{
    if (!CoopNet_Init())
    {
        luaL_error(L, "[coop] ENet initialization failed");
        return 0;
    }

    lua_newtable(L);
    luaL_register(L, nullptr, coop_lib);
    return 1;
}

// ---------------------------------------------------------------------------
// DLL entry point
// ---------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_DETACH)
        CoopNet_Shutdown();
    return TRUE;
}
