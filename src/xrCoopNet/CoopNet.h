#pragma once

#include "stdafx.h"
#include <string>

// Maximum number of concurrent peers (clients on host, or 1 on client)
static constexpr int COOP_MAX_PEERS     = 4;
// ENet channels: 0 = unreliable (positions), 1 = reliable (events)
static constexpr int COOP_CHANNELS      = 2;
static constexpr int COOP_CHANNEL_UNREL = 0;
static constexpr int COOP_CHANNEL_REL   = 1;
static constexpr int COOP_TIMEOUT_MS    = 5000;
// Max time to wait for a graceful ENet disconnect handshake to complete
// before force-resetting the peer (CoopNet_Disconnect).
static constexpr int COOP_DISCONNECT_GRACE_MS = 500;

struct CoopPeer {
    ENetPeer* peer   = nullptr;
    int       id     = -1;    // peer_id exposed to Lua (0 = server from client POV)
    char      addr[64] = {};
};

enum class CoopEventType { Connect, Disconnect, Receive };

// Plain C++ event record -- deliberately has no Lua types in it. Lua callback
// storage (luaL_ref into LUA_REGISTRYINDEX) plus later lua_pcall invocation
// from native code turned out to be unreliable in this engine build (calling
// through to a registered closure from CoopNet_Pump intermittently landed on
// an unrelated bound C++ object instead -- "No such operator [__call] defined
// in class [evaluator_contact]", a LuaBind class_rep error -- almost
// certainly because coop.dll statically links its own vendored LuaJIT build,
// ABI-incompatible in some subtle way with whatever exact LuaJIT the real
// game binary uses internally for its own class/registry bookkeeping). Simple
// stack pushes (integers, strings, booleans) are unaffected -- those already
// worked fine for host()/connect()/send() -- so the fix is to never store a
// Lua closure or call back into Lua from native code at all: events are
// queued here in plain C++ and Lua polls for them each frame via
// poll_events(), which only uses that same proven-safe category of calls
// (lua_newtable/pushstring/pushinteger/pushlstring/setfield/rawseti).
struct CoopEvent {
    CoopEventType type;
    int         peer_id = -1;
    int         channel = -1;   // only meaningful for Receive
    std::string data;           // only meaningful for Receive
};

// Global session state - one host, one role at a time
extern ENetHost*              g_host;
extern bool                   g_is_server;
extern std::vector<CoopPeer>  g_peers;

bool CoopNet_Init();
void CoopNet_Shutdown();

// Open as server (host), returns false on failure
bool CoopNet_Host(int port, int max_peers);

// Connect to server, returns false on failure
bool CoopNet_Connect(const char* address, int port);

void CoopNet_Disconnect();

// Send to specific peer (peer_id). channel: COOP_CHANNEL_REL or COOP_CHANNEL_UNREL.
// data is a raw byte buffer, len bytes.
bool CoopNet_Send(int peer_id, int channel, const void* data, size_t len, bool reliable);

// Broadcast to all peers except exclude_id (-1 = broadcast all)
bool CoopNet_Broadcast(int channel, const void* data, size_t len, bool reliable, int exclude_id = -1);

// Process pending ENet events into the internal queue. Call every frame.
// Returns number of events processed.
int CoopNet_Pump();

// Drains and returns all events queued since the last call.
std::vector<CoopEvent> CoopNet_PollEvents();

int  CoopNet_PeerCount();
bool CoopNet_IsServer();
bool CoopNet_IsConnected();
int  CoopNet_GetPing(int peer_id);

// Find peer by id, returns nullptr if not found
CoopPeer* CoopNet_FindPeer(int peer_id);
