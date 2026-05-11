#include "internal.hpp"

auto wm_connect(WmServer* server) -> Ref<WmClient>
{
    auto client = ref_create<WmClient>();
    client->wm = server;
    server->clients.emplace_back(client.get());

    return client;
}

WmClient::~WmClient()
{
    std::erase(wm->clients, this);
}

void wm_listen(WmClient* client, std::move_only_function<void(WmClient*, WmEvent*)> listener)
{
    client->listener = std::move(listener);
}

auto wm_filter_event(WmServer* wm, WmEvent* event) -> WmEventFilterResult
{
    for (auto& fn : wm->event_filters) {
        if (fn(event) == WmEventFilterResult::capture) {
            return WmEventFilterResult::capture;
        }
    }

    return WmEventFilterResult::passthrough;
}

void wm_client_post_event_unfiltered(WmClient* client, WmEvent* event)
{
    if (client->listener) {
        client->listener(client, event);
    }
}

void wm_client_post_event(WmClient* client, WmEvent* event)
{
    if (wm_filter_event(client->wm, event) == WmEventFilterResult::capture) return;
    wm_client_post_event_unfiltered(client, event);
}

void wm_broadcast_event(WmServer* server, WmEvent* event)
{
    if (wm_filter_event(server, event) == WmEventFilterResult::capture) return;

    for (auto* client : server->clients) {
        wm_client_post_event_unfiltered(client, event);
    }
}
