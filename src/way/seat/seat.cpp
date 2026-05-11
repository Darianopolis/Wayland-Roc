#include "seat.hpp"

#include "../data/data.hpp"
#include "../surface/surface.hpp"
#include "../client.hpp"

// -----------------------------------------------------------------------------

static
void init_seat(WayServer* server, WmSeat* scene_seat)
{
    auto seat = ref_create<WaySeat>();
    seat->server = server;
    seat->seat = scene_seat;

    server->seats.emplace_back(seat.get());

    way_seat_keyboard_init(seat.get());

    seat->global = way_global(server, wl_seat, seat.get());

    way_global(server, zwp_relative_pointer_manager_v1);
    way_global(server, zwp_pointer_constraints_v1);
}

WaySeat::~WaySeat()
{
    wl_global_remove(global);
}

void way_seat_init(WayServer* server)
{
    for (auto* seat : wm_get_seats(server->wm)) {
        init_seat(server, seat);
    }
}

// -----------------------------------------------------------------------------

WAY_INTERFACE(wl_seat) = {
    .get_pointer = way_seat_get_pointer,
    .get_keyboard = way_seat_get_keyboard,
    WAY_STUB(get_touch),
    .release = way_simple_destroy,
};

static
auto find_client_seat(WayClient* client, WmSeat* seat) -> WayClientSeat*
{
    for (auto* cs : client->seats) {
        if (cs->seat->seat == seat) {
            return cs;
        }
    }
    return nullptr;
}

WAY_BIND_GLOBAL(wl_seat, bind)
{
    auto* seat = way_get_userdata<WaySeat>(bind.server, bind.data);
    auto* client = way_client_from(bind.client);

    Ref client_seat = find_client_seat(client, seat->seat);
    if (!client_seat) {
        client_seat = ref_create<WayClientSeat>();
        client_seat->client = client;
        client_seat->seat = seat;
        client->seats.emplace_back(client_seat.get());
    }

    auto* resource = way_resource_create_refcounted(wl_seat, bind.client, bind.version, bind.id, client_seat.get());

    way_send<wl_seat_send_capabilities>(resource, WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);

    if (bind.version >= WL_SEAT_NAME_SINCE_VERSION) {
        way_send<wl_seat_send_name>(resource, wm_seat_get_name(seat->seat));
    }

    // TODO: Synchronize with current seat keyboard/pointer/data state
}

WayClientSeat::~WayClientSeat()
{
    std::erase(client->seats, this);
}

// ---------------------------------------------------------------------------------------

void way_seat_handle_event(WayClient* client, WmEvent* event)
{
#define HANDLE_EVENT(Event, Function) \
    if (auto* client_seat = find_client_seat(client, (Event).seat)) { \
        Function(client_seat, &(Event)); \
    }

    switch (event->type) {
        break;case WmEventType::keyboard_enter:    HANDLE_EVENT(event->keyboard, way_seat_on_keyboard_enter);
        break;case WmEventType::keyboard_leave:    HANDLE_EVENT(event->keyboard, way_seat_on_keyboard_leave);
        break;case WmEventType::keyboard_key:      HANDLE_EVENT(event->keyboard, way_seat_on_key);
        break;case WmEventType::keyboard_modifier: HANDLE_EVENT(event->keyboard, way_seat_on_modifier);

        break;case WmEventType::pointer_enter:  HANDLE_EVENT(event->pointer, way_seat_on_pointer_enter);
        break;case WmEventType::pointer_leave:  HANDLE_EVENT(event->pointer, way_seat_on_pointer_leave);
        break;case WmEventType::pointer_motion: HANDLE_EVENT(event->pointer, way_seat_on_motion);
        break;case WmEventType::pointer_button: HANDLE_EVENT(event->pointer, way_seat_on_button);
        break;case WmEventType::pointer_scroll: HANDLE_EVENT(event->pointer, way_seat_on_scroll);

        break;case WmEventType::selection:
            if (auto* client_seat = find_client_seat(client, event->selection.seat)) {
                way_data_offer_selection(client_seat);
            }

        break;default:
            debug_unreachable();
    }

#undef HANDLE_EVENT
}
