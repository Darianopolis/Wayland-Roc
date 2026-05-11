#include "internal.hpp"

void wm_data_source_offer(WmDataSource* source, const char* mime_type)
{
    source->offered.insert(mime_type);
}

auto wm_data_source_get_offered(WmDataSource* source) -> std::span<const std::string>
{
    return source->offered;
}

void wm_data_source_receive(WmDataSource* source, const char* mime_type, fd_t fd)
{
    log_debug("wm.data_source.receieve({}, {}, {})", (void*)source, mime_type, fd);
    source->on_send(mime_type, fd);
}

void wm_set_selection(WmSeat* seat, WmDataSource* source)
{
    if (seat->selection.get() == source) return;
    if (seat->selection) {
        seat->selection->on_cancel();
    }
    seat->selection = source;

    if (auto* focus = seat->keyboard.focus.get()) {
        wm_client_post_event(focus->client, ptr_to(WmEvent {
            .selection {
                .type = WmEventType::selection,
                .seat = seat,
                .data_source = source,
            },
        }));
    }
}

auto wm_get_selection(WmSeat* seat) -> WmDataSource*
{
    return seat->selection.get();
}
