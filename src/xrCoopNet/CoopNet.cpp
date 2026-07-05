#include "stdafx.h"
#include "CoopNet.h"

ENetHost*             g_host       = nullptr;
bool                  g_is_server  = false;
std::vector<CoopPeer> g_peers;

static int s_next_peer_id = 1;
static std::vector<CoopEvent> g_event_queue;

bool CoopNet_Init()
{
    return enet_initialize() == 0;
}

void CoopNet_Shutdown()
{
    CoopNet_Disconnect();
    enet_deinitialize();
}

bool CoopNet_Host(int port, int max_peers)
{
    CoopNet_Disconnect();

    ENetAddress addr;
    addr.host = ENET_HOST_ANY;
    addr.port = static_cast<enet_uint16>(port);

    g_host = enet_host_create(&addr, max_peers, COOP_CHANNELS, 0, 0);
    if (!g_host) return false;

    g_is_server  = true;
    s_next_peer_id = 1;
    g_peers.clear();
    g_event_queue.clear();
    return true;
}

bool CoopNet_Connect(const char* address, int port)
{
    CoopNet_Disconnect();

    g_host = enet_host_create(nullptr, 1, COOP_CHANNELS, 0, 0);
    if (!g_host) return false;

    ENetAddress addr;
    enet_address_set_host(&addr, address);
    addr.port = static_cast<enet_uint16>(port);

    ENetPeer* peer = enet_host_connect(g_host, &addr, COOP_CHANNELS, 0);
    if (!peer)
    {
        enet_host_destroy(g_host);
        g_host = nullptr;
        return false;
    }

    g_is_server = false;
    g_peers.clear();
    g_event_queue.clear();
    // NOTE: the peer_id=0 CoopPeer entry is added once CoopNet_Pump observes
    // the real ENET_EVENT_TYPE_CONNECT event (below), not eagerly here --
    // adding it here too used to create a duplicate g_peers entry for the
    // same connection (both id=0, same underlying ENetPeer*).
    return true;
}

void CoopNet_Disconnect()
{
    if (!g_host) return;

    // Defer the actual disconnect until all reliable data queued for the peer
    // has been acknowledged (enet_peer_disconnect_later falls back to an
    // immediate disconnect if nothing is pending). A plain enet_peer_disconnect
    // sends the DISCONNECT command right away; if the peer hasn't yet
    // dispatched a reliable packet we just sent, ENet discards that
    // undelivered data as part of processing the DISCONNECT, so it never
    // reaches the peer's application.
    for (auto& cp : g_peers)
        if (cp.peer) enet_peer_disconnect_later(cp.peer, 0);

    int pending = static_cast<int>(g_peers.size());
    ENetEvent ev;
    while (pending > 0 && enet_host_service(g_host, &ev, COOP_DISCONNECT_GRACE_MS) > 0)
    {
        switch (ev.type)
        {
        case ENET_EVENT_TYPE_DISCONNECT:
            --pending;
            break;
        case ENET_EVENT_TYPE_RECEIVE:
            enet_packet_destroy(ev.packet);
            break;
        default:
            break;
        }
    }

    // Anything that didn't confirm within the grace period gets force-dropped.
    for (auto& cp : g_peers)
        if (cp.peer) enet_peer_reset(cp.peer);

    g_peers.clear();
    g_event_queue.clear();
    enet_host_destroy(g_host);
    g_host = nullptr;
}

CoopPeer* CoopNet_FindPeer(int peer_id)
{
    for (auto& cp : g_peers)
        if (cp.id == peer_id) return &cp;
    return nullptr;
}

bool CoopNet_Send(int peer_id, int channel, const void* data, size_t len, bool reliable)
{
    CoopPeer* cp = CoopNet_FindPeer(peer_id);
    if (!cp || !cp->peer) return false;

    enet_uint32 flags = reliable ? ENET_PACKET_FLAG_RELIABLE : 0;
    ENetPacket* pkt   = enet_packet_create(data, len, flags);
    return enet_peer_send(cp->peer, static_cast<enet_uint8>(channel), pkt) == 0;
}

bool CoopNet_Broadcast(int channel, const void* data, size_t len, bool reliable, int exclude_id)
{
    if (!g_host) return false;

    enet_uint32 flags = reliable ? ENET_PACKET_FLAG_RELIABLE : 0;

    if (exclude_id < 0)
    {
        ENetPacket* pkt = enet_packet_create(data, len, flags);
        enet_host_broadcast(g_host, static_cast<enet_uint8>(channel), pkt);
        return true;
    }

    // Broadcast with exclusion: send individually
    for (auto& cp : g_peers)
    {
        if (cp.id == exclude_id) continue;
        ENetPacket* pkt = enet_packet_create(data, len, flags);
        enet_peer_send(cp.peer, static_cast<enet_uint8>(channel), pkt);
    }
    return true;
}

int CoopNet_Pump()
{
    if (!g_host) return 0;

    int count = 0;
    ENetEvent ev;

    while (enet_host_service(g_host, &ev, 0) > 0)
    {
        ++count;
        switch (ev.type)
        {
        case ENET_EVENT_TYPE_CONNECT:
        {
            CoopPeer cp;
            cp.peer = ev.peer;
            cp.id   = g_is_server ? s_next_peer_id++ : 0;

            char ipbuf[64];
            enet_address_get_host_ip(&ev.peer->address, ipbuf, sizeof(ipbuf));
            snprintf(cp.addr, sizeof(cp.addr), "%s:%u", ipbuf, ev.peer->address.port);

            ev.peer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(cp.id));
            g_peers.push_back(cp);

            CoopEvent e;
            e.type    = CoopEventType::Connect;
            e.peer_id = cp.id;
            g_event_queue.push_back(std::move(e));
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT:
        {
            int peer_id = static_cast<int>(reinterpret_cast<uintptr_t>(ev.peer->data));

            CoopEvent e;
            e.type    = CoopEventType::Disconnect;
            e.peer_id = peer_id;
            g_event_queue.push_back(std::move(e));

            for (auto it = g_peers.begin(); it != g_peers.end(); ++it)
            {
                if (it->id == peer_id) { g_peers.erase(it); break; }
            }
            ev.peer->data = nullptr;
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE:
        {
            int peer_id = static_cast<int>(reinterpret_cast<uintptr_t>(ev.peer->data));

            CoopEvent e;
            e.type    = CoopEventType::Receive;
            e.peer_id = peer_id;
            e.channel = ev.channelID;
            e.data.assign(reinterpret_cast<const char*>(ev.packet->data), ev.packet->dataLength);
            g_event_queue.push_back(std::move(e));

            enet_packet_destroy(ev.packet);
            break;
        }
        default: break;
        }
    }

    return count;
}

std::vector<CoopEvent> CoopNet_PollEvents()
{
    std::vector<CoopEvent> out;
    out.swap(g_event_queue);
    return out;
}

int CoopNet_PeerCount()  { return static_cast<int>(g_peers.size()); }
bool CoopNet_IsServer()  { return g_is_server && g_host != nullptr; }
bool CoopNet_IsConnected()
{
    if (!g_host || g_peers.empty()) return false;
    return g_peers[0].peer != nullptr;
}

int CoopNet_GetPing(int peer_id)
{
    CoopPeer* cp = CoopNet_FindPeer(peer_id);
    if (!cp || !cp->peer) return -1;
    return static_cast<int>(cp->peer->roundTripTime);
}
