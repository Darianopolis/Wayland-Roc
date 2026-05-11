#pragma once

#include <core/object.hpp>
#include <scene/scene.hpp>

struct ExecContext;
struct Gpu;
struct IoContext;

struct WmSeat;
struct WmServer;
struct WmSurface;
struct WmWindow;
struct WmPointerConstraint;
struct WmDataSource;

// -----------------------------------------------------------------------------

enum class WmModifier
{
    super = 1 << 0,
    shift = 1 << 1,
    ctrl  = 1 << 2,
    alt   = 1 << 3,
    num   = 1 << 4,
    caps  = 1 << 5,
};

// -----------------------------------------------------------------------------

struct WmServerCreateInfo
{
    ExecContext* exec;
    Gpu*         gpu;

    WmModifier main_mod;
};

auto wm_create(const WmServerCreateInfo&) -> Ref<WmServer>;

enum class WmLayer
{
    background,
    window,
    overlay,
};

auto wm_get_scene(WmServer*) -> Scene*;
auto wm_get_layer(WmServer*, WmLayer) -> SceneTree*;

// -----------------------------------------------------------------------------

struct WmOutput;
struct WmOutputInterface
{
    void(*request_frame)(void*);
};

auto wm_output_create(WmServer*, void*, WmOutputInterface) -> Ref<WmOutput>;
void wm_output_set_pixel_size(WmOutput*, vec2i32);
void wm_output_frame(WmOutput*);

// -----------------------------------------------------------------------------

auto wm_seat_get_name(WmSeat*) -> const char*;

auto wm_get_seats(WmServer*) -> std::span<WmSeat* const>;

// -----------------------------------------------------------------------------

struct WmKeyboardInfo
{
    xkb_context* context;
    xkb_state*   state;
    xkb_keymap*  keymap;
    i32          rate;
    i32          delay;
};

void wm_keyboard_press(WmSeat*, u32 key);
void wm_keyboard_release(WmSeat*, u32 key);
void wm_keyboard_focus(WmSeat*, WmSurface*);

auto wm_keyboard_get_pressed(WmSeat*) -> std::span<const u32>;
auto wm_keyboard_get_info(WmSeat*) -> const WmKeyboardInfo&;
auto wm_keyboard_get_modifiers(WmSeat*) -> Flags<WmModifier>;
auto wm_keyboard_get_focus(WmSeat*) -> WmSurface*;

// -----------------------------------------------------------------------------

void wm_pointer_press(WmSeat*, u32 button);
void wm_pointer_release(WmSeat*, u32 button);
void wm_pointer_scroll(WmSeat*, vec2f32 delta);
void wm_pointer_move(WmSeat*, vec2f32 rel_accel, vec2f32 rel_unaccel);

auto wm_pointer_get_pressed(WmSeat*) -> std::span<const u32>;
auto wm_pointer_get_position(WmSeat*) -> vec2f32;
auto wm_pointer_get_focus(WmSeat*) -> WmSurface*;

void wm_pointer_set_cursor(WmSeat*, WmSurface*);
void wm_pointer_set_xcursor(WmSeat*, const char*);

// -----------------------------------------------------------------------------

enum class WmEventType
{
    window_created,
    window_destroyed,
    window_mapped,
    window_unmapped,
    window_repositioned,

    window_reposition_requested,
    window_close_requested,

    output_added,
    output_configured,
    output_removed,
    output_layout,
    output_frame,

    keyboard_enter,
    keyboard_leave,
    keyboard_key,
    keyboard_modifier,

    pointer_enter,
    pointer_leave,
    pointer_motion,
    pointer_button,
    pointer_scroll,

    selection,

    pointer_constraint_enabled,
    pointer_constraint_disabled,
};

struct WmWindowEvent
{
    WmEventType type;
    WmWindow* window;
    union {
        struct {
            rect2f32 frame;
            vec2f32  gravity;
        } reposition;
    };
};

struct WmOutputEvent
{
    WmEventType type;
    WmOutput* output;
};

struct WmKeyboardEvent
{
    WmEventType type;
    WmSeat* seat;
    union {
        struct {
            u32 code;
            xkb_keysym_t sym;
            const char* utf8;
            bool pressed;
        } key;
        WmSurface* focus;
    };
};

struct WmSelectionEvent
{
    WmEventType type;
    WmSeat* seat;
    WmDataSource* data_source;
};

struct WmPointerEvent
{
    WmEventType type;
    WmSeat* seat;
    union {
        struct {
            u32 code;
            bool pressed;
        } button;
        struct {
            vec2f32 rel_accel;
            vec2f32 rel_unaccel;
        } motion;
        struct {
            vec2f32 delta;
        } scroll;
        struct {
            WmPointerConstraint* constraint;
        } constraint;
        WmSurface* focus;
    };
};

union WmEvent
{
    WmEventType type;
    WmWindowEvent window;
    WmOutputEvent output;
    WmKeyboardEvent keyboard;
    WmSelectionEvent selection;
    WmPointerEvent pointer;
};

struct WmClient;
auto wm_connect(WmServer*) -> Ref<WmClient>;
void wm_listen(WmClient*, std::move_only_function<void(WmClient*, WmEvent*)>);

// -----------------------------------------------------------------------------

enum class WmEventFilterResult
{
    passthrough,
    capture,
};
void wm_add_event_filter(WmServer*, std::move_only_function<WmEventFilterResult(WmEvent*)>);

// -----------------------------------------------------------------------------

struct WmDataSource
{
    std::flat_set<std::string> offered;

    virtual void on_cancel() = 0;
    virtual void on_send(const char* mime_type, fd_t target) = 0;
};

void wm_data_source_offer(      WmDataSource*, const char* mime_type);
auto wm_data_source_get_offered(WmDataSource*) -> std::span<const std::string>;

void wm_data_source_receive(WmDataSource*, const char* mime_type, fd_t fd);

void wm_set_selection(WmSeat*, WmDataSource*);
auto wm_get_selection(WmSeat*) -> WmDataSource*;

// -----------------------------------------------------------------------------

struct WmSurface
{
    WmClient* client;

    WmSurface* parent;

    std::vector<WmSurface*> children;

    Ref<SceneTree> tree;
    Ref<SceneTexture> texture;
    Ref<SceneInputRegion> input_region;

    ~WmSurface();
};

auto wm_surface_create(WmClient*) -> Ref<WmSurface>;

void wm_surface_set_parent(WmSurface*, WmSurface* parent);

auto wm_surface_is_focused(WmSurface*) -> bool;

auto wm_surface_contains(WmSurface*, WmSurface*) -> bool;

// -----------------------------------------------------------------------------

auto wm_window_create(WmSurface*) -> Ref<WmWindow>;

void wm_window_set_title(WmWindow*, std::string_view title);
void wm_window_set_app_id(WmWindow*, std::string_view app_id);

void wm_window_map(  WmWindow*);
void wm_window_unmap(WmWindow*);
void wm_window_raise(WmWindow*);

void wm_window_request_reposition(WmWindow*, rect2f32 frame, vec2f32 gravity);
void wm_window_request_close(     WmWindow*);

void wm_window_set_frame(WmWindow*, rect2f32 frame);
auto wm_window_get_frame(WmWindow*) -> rect2f32;

auto wm_window_is_focused(WmWindow*) -> bool;
void wm_window_focus(     WmWindow*);

auto wm_find_window_at(WmServer*, vec2f32 point) -> WmWindow*;

// -----------------------------------------------------------------------------

enum class WmPointerConstraintType
{
    locked,
    confined
};

auto wm_constrain_pointer(WmSurface*, region2f32, WmPointerConstraintType) -> Ref<WmPointerConstraint>;
void wm_pointer_constraint_set_region(WmPointerConstraint*, region2f32);

// -----------------------------------------------------------------------------

void wm_request_frame(WmServer*);

auto wm_list_outputs(WmServer*) -> std::span<WmOutput* const>;

auto wm_output_get_viewport(WmOutput*) -> rect2f32;

struct WmFindOutputResult
{
    WmOutput* output;
    vec2f32   position;
};

auto wm_find_output_at(WmServer*, vec2f32 point) -> WmFindOutputResult;
