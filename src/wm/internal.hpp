#pragma once

#include "wm.hpp"

#include <core/types.hpp>
#include <scene/scene.hpp>
#include <way/way.hpp>

enum class WmInteractionMode
{
    none,
    move,
    size,
    zone,
    focus_cycle,
};

struct ShellLauncher;

struct WmOutput
{
    WmServer* server;

    vec2i32 pixel_size;
    rect2f32 viewport;

    void* userdata;
    WmOutputInterface interface;

    ~WmOutput();
};

struct WmKeyboard : WmKeyboardInfo
{
    CountingSet<u32> pressed;

    EnumMap<WmModifier, xkb_mod_mask_t> mod_masks;

    Weak<WmSurface> focus;

    ~WmKeyboard();
};

struct WmPointer
{
    CountingSet<u32> pressed;
    vec2f32 position;

    Ref<SceneTree> tree;
    Weak<SceneNode> visual;

    Weak<WmSurface> focus;
};

struct WmSeat
{
    WmServer* server;

    std::string name;

    WmKeyboard keyboard;
    WmPointer  pointer;

    Ref<WmDataSource> selection;
};

struct WmServer
{
    ExecContext* exec;
    Gpu*         gpu;

    Ref<Scene> scene;
    EnumMap<WmLayer, Ref<SceneTree>> layers;

    std::vector<std::move_only_function<WmEventFilterResult(WmEvent*)>> event_filters;

    WmModifier main_mod;

    WmInteractionMode mode;

    Ref<ShellLauncher> launcher;

    Uid                    window_system_id;
    std::vector<WmWindow*> windows;

    std::vector<WmClient*> clients;

    WmPointerConstraint* active_pointer_constraint;
    std::vector<WmPointerConstraint*> pointer_constraints;

    WmSeat seat;
    std::array<WmSeat*, 1> seats;

    struct {
        Ref<GpuSampler> sampler;

        std::string theme;
        i32         size;

        ankerl::unordered_dense::map<std::string_view, Ref<SceneNode>> cache;
    } xcursor;

    struct {
        std::vector<WmOutput*> outputs;
    } io;

    struct {
        WmSeat* seat;
        Weak<WmWindow> window;
        vec2f32  grab;
        rect2f32 frame;
        vec2f32  relative;
    } movesize;

    struct {
        WmSeat* seat;
        Ref<SceneTexture> texture;

        Weak<WmWindow> window;
        aabb2f64 initial_zone;
        aabb2f64 final_zone;
        bool     selecting = false;
    } zone;

    struct {
        WmSeat* seat;
        Weak<WmWindow> cycled;
    } focus;
};

void wm_init_scene(WmServer*);
void wm_init_xcursor(WmServer*);
void wm_init_seat( WmServer*);
void wm_init_hotkeys(WmServer*);

void wm_init_keyboard(WmSeat*);
void wm_init_pointer( WmSeat*);

void wm_init_movesize(   WmServer*);
void wm_init_zone(       WmServer*);
void wm_init_focus_cycle(WmServer*);

// -----------------------------------------------------------------------------

void wm_decoration_init(WmServer*);

// -----------------------------------------------------------------------------

void wm_arrange_windows(WmServer*);

// -----------------------------------------------------------------------------

struct WmClient
{
    WmServer* wm;

    std::vector<WmSurface*> surfaces;

    std::move_only_function<void(WmClient*, WmEvent*)> listener;

    ~WmClient();
};

// -----------------------------------------------------------------------------

struct WmWindow
{
    WmClient* client;
    Weak<WmSurface> surface;

    vec2f32 extent;
    bool mapped;

    std::string app_id;
    std::string title;

    Ref<SceneTree> root_tree;

    Ref<SceneTexture> borders;

    ~WmWindow();
};

void wm_window_post_event(WmWindowEvent* event);

// -----------------------------------------------------------------------------

struct WmPointerConstraint
{
    WmServer* wm;

    Weak<WmSurface> surface;

    WmPointerConstraintType type;

    region2f32 region;

    ~WmPointerConstraint();
};

void wm_pointer_constraints_init(WmServer*);
void wm_update_active_pointer_constraint(WmServer*);
auto wm_pointer_constraint_apply(WmServer*, vec2f32 position, vec2f32 delta) -> vec2f32;

// -----------------------------------------------------------------------------

auto wm_filter_event(WmServer* wm, WmEvent* event) -> WmEventFilterResult;
void wm_broadcast_event(WmServer*, WmEvent*);
void wm_client_post_event(WmClient*, WmEvent*);
void wm_client_post_event_unfiltered(WmClient*, WmEvent*);
