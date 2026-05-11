#include "internal.hpp"

auto wm_get_seats(WmServer* wm) -> std::span<WmSeat* const>
{
    return wm->seats;
}

void wm_init_seat(WmServer* wm)
{
    wm->seat.server = wm;
    wm->seat.name = "seat0";
    wm_init_keyboard(&wm->seat);
    wm_init_pointer(&wm->seat);
    wm->seats[0] = &wm->seat;
}

auto wm_seat_get_name(WmSeat* seat) -> const char*
{
    return seat->name.c_str();
}
