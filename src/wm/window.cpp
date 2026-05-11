#include "internal.hpp"

#include <core/math.hpp>
#include <core/color.hpp>

WmWindow::~WmWindow()
{
    wm_window_post_event(ptr_to(WmWindowEvent {
        .type = WmEventType::window_destroyed,
        .window = this,
    }));

    root_tree->userdata = {};
    wm_window_unmap(this);
    std::erase(client->wm->windows, this);
}

static constexpr vec2f32 border_size    = vec2f32(2);
static constexpr auto    border_normal  = color_from_hex("#4C4C4C");
static constexpr auto    border_focused = color_from_hex("#6666FF");

auto wm_window_create(WmSurface* surface) -> Ref<WmWindow>
{
    auto* wm = surface->client->wm;

    auto window = ref_create<WmWindow>();
    window->client = surface->client;
    window->surface = surface;

    wm->windows.emplace_back(window.get());

    window->root_tree = scene_tree_create();
    window->root_tree->userdata = {wm->window_system_id, window.get()};

    window->borders = scene_texture_create();
    scene_tree_place_above(window->root_tree.get(), nullptr, window->borders.get());
    scene_texture_set_tint(window->borders.get(), border_normal);

    scene_tree_place_above(window->root_tree.get(), nullptr, surface->tree.get());

    wm_window_post_event(ptr_to(WmWindowEvent {
        .type = WmEventType::window_created,
        .window = window.get(),
    }));

    return window;
}

void wm_window_set_title(WmWindow* window, std::string_view title)
{
    window->title = title;
}

void wm_window_set_app_id(WmWindow* window, std::string_view app_id)
{
    window->app_id = app_id;
}

void wm_window_post_event(WmWindowEvent* event)
{
    wm_client_post_event(event->window->client, reinterpret_cast<WmEvent*>(event));
}

void wm_window_request_reposition(WmWindow* window, rect2f32 frame, vec2f32 gravity)
{
    wm_window_post_event(ptr_to(WmWindowEvent {
        .type = WmEventType::window_reposition_requested,
        .window = window,
        .reposition = {
            .frame = frame,
            .gravity = gravity,
        },
    }));
}

void wm_window_request_close(WmWindow* window)
{
    wm_window_post_event(ptr_to(WmWindowEvent {
        .type = WmEventType::window_close_requested,
        .window = window,
    }));
}

void wm_window_set_frame(WmWindow* window, rect2f32 frame)
{
    window->extent = frame.extent;
    scene_tree_set_translation(window->root_tree.get(), frame.origin);

    scene_texture_set_dst(window->borders.get(), {-border_size, frame.extent + border_size * 2.f, xywh});

    wm_window_post_event(ptr_to(WmWindowEvent {
        .type = WmEventType::window_repositioned,
        .window = window,
    }));
}

auto wm_window_get_frame(WmWindow* window) -> rect2f32
{
    return {
        scene_tree_get_position(window->root_tree.get()),
        window->extent,
        xywh
    };
}

void wm_window_map(WmWindow* window)
{
    if (window->mapped) return;

    window->mapped = true;
    wm_arrange_windows(window->client->wm);

    wm_window_post_event(ptr_to(WmWindowEvent {
        .type = WmEventType::window_mapped,
        .window = window,
    }));
}

void wm_window_raise(WmWindow* window)
{
    if (!window->mapped) return;

    auto* wm = window->client->wm;

    std::erase(wm->windows, window);
    wm->windows.emplace_back(window);

    wm_arrange_windows(wm);
}

void wm_window_unmap(WmWindow* window)
{
    if (!window->mapped) return;

    window->mapped = false;
    wm_arrange_windows(window->client->wm);

    wm_window_post_event(ptr_to(WmWindowEvent {
        .type = WmEventType::window_unmapped,
        .window = window,
    }));
}

// -----------------------------------------------------------------------------

static
void update_border_colors(WmServer* wm)
{
    for (auto* w : wm->windows) {
        scene_texture_set_tint(w->borders.get(), wm_window_is_focused(w) ? border_focused : border_normal);
    }
}

void wm_decoration_init(WmServer* wm)
{
    wm_add_event_filter(wm, [wm](WmEvent* event) -> WmEventFilterResult {
        if (event->type == WmEventType::keyboard_enter || event->type == WmEventType::keyboard_leave) {
            update_border_colors(wm);
        }
        return WmEventFilterResult::passthrough;
    });
}

// -----------------------------------------------------------------------------

auto wm_find_window_at(WmServer* wm, vec2f32 point) -> WmWindow*
{
    // TODO: This will ignore any `input_plane`s currently.
    //       Should we provide (optional) mappings from `input_plane` back to windows
    //       and then intersect against both `input_plane`s and `window` frames?

    WmWindow* window = nullptr;

    scene_iterate<SceneIterateDirection::front_to_back>(
        wm_get_layer(wm, WmLayer::window),
        scene_iterate_default,
        scene_iterate_default,
        [&](SceneTree* tree) {
            if (tree->userdata.id == wm->window_system_id) {
                auto w = static_cast<WmWindow*>(tree->userdata.data);
                if (rect_contains(wm_window_get_frame(w), point)) {
                    window = w;
                    return SceneIterateAction::stop;
                }
            }
            return SceneIterateAction::next;
        });

    return window;
}

// -----------------------------------------------------------------------------

void wm_window_focus(WmWindow* window)
{
    if (!window->surface) return;

    auto* wm = window->client->wm;

    for (auto* seat : wm_get_seats(wm)) {
        wm_keyboard_focus(seat, window->surface.get());
    }

    wm_window_raise(window);
}

auto wm_window_is_focused(WmWindow* window) -> bool
{
    if (!window->surface) return false;
    auto* wm = window->client->wm;
    return std::ranges::any_of(wm_get_seats(wm), [&](auto* seat) {
        auto* focus = wm_keyboard_get_focus(seat);
        return wm_surface_contains(window->surface.get(), focus);
    });
}

auto wm_find_window_for(WmServer* wm, WmSurface* focus) -> WmWindow*
{
    for (auto* window : wm->windows) {
        if (!window->surface) continue;
        if (wm_surface_contains(window->surface.get(), focus)) {
            return window;
        }
    }
    return nullptr;
}
