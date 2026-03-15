#pragma once

#include "scene.hpp"

#include "io/io.hpp"

// -----------------------------------------------------------------------------

struct scene::Output {
    scene::Client* client;
    rect2f32      viewport;

    ~Output();
};

// -----------------------------------------------------------------------------

struct scene_cursor_manager;

void scene_cursor_manager_init(scene::Context*);

struct scene::Context
{
    gpu::Context* gpu;

    struct {
        core::Ref<gpu::Shader> vertex;
        core::Ref<gpu::Shader> fragment;
        core::Ref<gpu::Image>    white;
        core::Ref<gpu::Sampler>  sampler;
    } render;

    scene::SystemId prev_system_id = {};

    core::Ref<scene::Tree> root_tree;
    core::EnumMap<scene::Layer, core::Ref<scene::Tree>> layers;

    std::vector<scene::Output*> outputs;

    std::vector<scene::Client*> clients;
    std::vector<scene::Window*> windows;
    scene::SystemId            window_system;

    struct {
        core::Ref<scene::Keyboard> keyboard;
        core::Ref<scene::Pointer>  pointer;
        std::vector<io::InputDevice*> led_devices;
    } seat;

    core::Ref<scene::DataSource> selection;

    core::Ref<scene_cursor_manager> cursor_manager;

    ~Context();
};

void scene_broadcast_event(scene::Context*, scene::Event*);

void scene_render_init(scene::Context*);

// -----------------------------------------------------------------------------

struct scene::Client
{
    scene::Context* ctx;

    std::move_only_function<scene::EventHandlerFn> event_handler;

    u32 input_regions = 0;

    ~Client();
};

void scene_client_post_event(scene::Client*, scene::Event*);

// -----------------------------------------------------------------------------

struct scene::Window
{
    scene::Client* client;

    vec2f32 extent;
    bool mapped;

    std::string title;

    core::Ref<scene::Tree> tree;

    ~Window();
};

// -----------------------------------------------------------------------------

namespace scene
{
    struct HotkeyPressState
    {
        core::Flags<scene::Modifier> modifiers;
        scene::Client*         client;
    };

    struct HotkeyMap {
        core::Map<scene::Hotkey, scene::Client*> registered;
        core::Map<scene::Scancode, scene::HotkeyPressState> pressed;
    };
}

struct scene::InputDevice
{
    scene::InputDeviceType type;
    scene::Context* ctx;

    scene::HotkeyMap hotkeys;
};

namespace scene
{
    struct Focus
    {
        scene::Client*       client = nullptr;
        scene::InputRegion* region = nullptr;

        constexpr bool operator==(const scene::Focus&) const noexcept = default;
    };
}

// -----------------------------------------------------------------------------

struct scene::Keyboard : scene::InputDevice, scene::KeyboardInfo
{
    core::CountingSet<u32> pressed;

    core::Flags<scene::Modifier> depressed;
    core::Flags<scene::Modifier> latched;
    core::Flags<scene::Modifier> locked;

    core::EnumMap<scene::Modifier, xkb_mod_mask_t> mod_masks;

    scene::Focus focus;

    ~Keyboard();
};

namespace scene::keyboard
{
    auto create(scene::Context*) -> core::Ref<scene::Keyboard>;
}

// -----------------------------------------------------------------------------

struct scene::Pointer : scene::InputDevice
{
    core::CountingSet<u32> pressed;

    core::Ref<scene::Tree> tree;

    std::move_only_function<scene::PointerDriverFn> driver;

    scene::Focus focus;
};

namespace scene
{
    void update_pointer_focus(scene::Context*);
}

namespace scene::pointer
{
    auto create(scene::Context*) -> core::Ref<scene::Pointer>;
}

// -----------------------------------------------------------------------------

namespace scene
{
    auto find_input_region_at(scene::Tree* tree, vec2f32 pos) -> scene::InputRegion*;
}

// -----------------------------------------------------------------------------

struct scene::DataSource
{
    scene::Client* client;

    core::FlatSet<std::string> offered;

    scene::DataSourceOps ops;

    ~DataSource();
};

namespace scene
{
    void offer_selection(scene::Client*, scene::DataSource*);
}

// -----------------------------------------------------------------------------

namespace scene::output
{
    void request_frame(scene::Output*);
}

// -----------------------------------------------------------------------------

namespace scene
{
    void handle_input_added(  scene::Context*, io::InputDevice*);
    void handle_input_removed(scene::Context*, io::InputDevice*);
    void handle_input(        scene::Context*, const io::InputEvent&);
}

// -----------------------------------------------------------------------------

namespace scene::keyboard
{
    void set_focus(scene::Keyboard* keyboard, scene::Focus new_focus);
}

namespace scene::pointer
{
    void set_focus( scene::Pointer*  pointer,  scene::Focus new_focus);
}
