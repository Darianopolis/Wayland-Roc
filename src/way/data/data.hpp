#pragma once

#include "../util.hpp"

#include <wm/wm.hpp>

struct WayClient;
struct WayClientSeat;

struct WayDataSource : WmDataSource
{
    WayClient* client;

    WayResource resource;

    virtual void on_cancel() final override;
    virtual void on_send(const char* mime_type, fd_t target) final override;

    ~WayDataSource();
};

struct WayDataOffer
{
    WayClientSeat* client_seat;

    WayResource resource;

    Ref<WmDataSource> source;
};

void way_data_offer_selection(WayClientSeat*);

// -----------------------------------------------------------------------------

WAY_INTERFACE_DECLARE(wl_data_device_manager, 3);
WAY_INTERFACE_DECLARE(wl_data_offer);
WAY_INTERFACE_DECLARE(wl_data_source);
WAY_INTERFACE_DECLARE(wl_data_device);
