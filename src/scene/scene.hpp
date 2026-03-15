#pragma once

#include "core/region.hpp"
#include "core/object.hpp"
#include "gpu/gpu.hpp"

#include "render.h"

// -----------------------------------------------------------------------------

namespace scene
{
    struct Output;
    struct Node;
    struct Tree;
    struct Texture;
    struct InputRegion;
    struct Window;

    struct Context;
}

namespace io
{
    struct Context;
    struct Event;
    struct Output;
}

namespace scene
{
    auto create(gpu::Context*, io::Context*) -> core::Ref<scene::Context>;

    enum class Layer
    {
        background,
        window,
        overlay,
    };

    auto get_layer(scene::Context*, scene::Layer) -> scene::Tree*;

    // TODO: Requests should be handled per-output
    void request_frame(scene::Context*);
}

// -----------------------------------------------------------------------------

namespace scene
{
    void push_io_event(scene::Context* ctx, io::Event*);
}

// -----------------------------------------------------------------------------

namespace scene
{
    auto render(scene::Context* ctx, gpu::Image* target, rect2f32 viewport) -> gpu::Syncpoint;
}

// -----------------------------------------------------------------------------

namespace scene
{
    enum class SystemId : u32 {};
    auto register_system(scene::Context*) -> scene::SystemId;
}

// -----------------------------------------------------------------------------

namespace scene
{
    struct Client;

    namespace client
    {
        auto create(scene::Context*) -> core::Ref<scene::Client>;
    }
}

// -----------------------------------------------------------------------------

namespace scene::output
{
    auto create(scene::Client*) -> core::Ref<scene::Output>;
    void set_viewport(scene::Output*, rect2f32 viewport);
    auto get_viewport(scene::Output*) -> rect2f32;
}

namespace scene
{
    auto list_outputs(scene::Context*) -> std::span<scene::Output* const>;

    struct FindOutputResult
    {
        scene::Output* output;
        vec2f32       position;
    };
    auto find_output_for_point(scene::Context*, vec2f32 point) -> scene::FindOutputResult;

    void frame(scene::Context* ctx, scene::Output*, io::Output* output, gpu::ImagePool* pool);
}

// -----------------------------------------------------------------------------

namespace scene
{
    enum class Modifier : u32
    {
        super = 1 << 0,
        shift = 1 << 1,
        ctrl  = 1 << 2,
        alt   = 1 << 3,
        num   = 1 << 4,
        caps  = 1 << 5,
    };

    enum class ModifierFlags
    {
        ignore_locked = 1 << 0
    };

    using Scancode = u32;

    auto get_modifiers(scene::Context*, core::Flags<scene::ModifierFlags> = {}) -> core::Flags<scene::Modifier>;
}

namespace scene
{
    enum class InputDeviceType
    {
        invalid,
        keyboard,
        pointer,
    };

    struct InputDevice;
    struct Keyboard;
    struct Pointer;
}

namespace scene::input_device
{
    auto get_type(    scene::InputDevice*) -> scene::InputDeviceType;
    auto get_pointer( scene::InputDevice*) -> scene::Pointer*;
    auto get_keyboard(scene::InputDevice*) -> scene::Keyboard*;
}

namespace scene
{
    auto get_pointer( scene::Context*) -> scene::Pointer*;
    auto get_keyboard(scene::Context*) -> scene::Keyboard*;
}

namespace scene::pointer
{
    void focus(       scene::Pointer*, scene::Client*, scene::InputRegion* = nullptr);
    auto get_position(scene::Pointer*) -> vec2f32;
    auto get_pressed( scene::Pointer*) -> std::span<const scene::Scancode>;

    void set_cursor( scene::Pointer*, scene::Node*);
    void set_xcursor(scene::Pointer*, const char* xcursor_semantic);
}

namespace scene
{
    struct KeyboardInfo
    {
        xkb_context* context;
        xkb_state*   state;
        xkb_keymap*  keymap;
        i32          rate;
        i32          delay;
    };
}

namespace scene::keyboard
{
    void clear_focus(  scene::Keyboard*);
    auto get_modifiers(scene::Keyboard*, core::Flags<scene::ModifierFlags> = {}) -> core::Flags<scene::Modifier>;
    auto get_pressed(  scene::Keyboard*) -> std::span<const scene::Scancode>;
    auto get_sym(      scene::Keyboard*, scene::Scancode) -> xkb_keysym_t;
    auto get_utf8(     scene::Keyboard*, scene::Scancode) -> std::string;
    auto get_info(     scene::Keyboard*) -> const scene::KeyboardInfo&;
}

// -----------------------------------------------------------------------------

namespace scene
{
    struct PointerDriverIn
    {
        vec2f32 position;
        vec2f32 delta;
    };

    struct PointerDriverOut
    {
        vec2f32 position;
        vec2f32 accel;
        vec2f32 unaccel;
    };

    using PointerDriverFn = auto(scene::PointerDriverIn) -> scene::PointerDriverOut;

    namespace pointer
    {
        void set_driver(scene::Pointer*, std::move_only_function<scene::PointerDriverFn>&&);
    }
}

// -----------------------------------------------------------------------------

namespace scene
{
    struct DataSource;

    struct DataSourceOps
    {
        std::move_only_function<void()>                 cancel = [] {};
        std::move_only_function<void(const char*, int)> send;
    };

    namespace data_source
    {
        auto create(scene::Client*, scene::DataSourceOps&&) -> core::Ref<scene::DataSource>;

        void offer(      scene::DataSource*, const char* mime_type);
        auto get_offered(scene::DataSource*) -> std::span<const std::string>;

        void send(scene::DataSource*, const char* mime_type, int fd);
    }

    void set_selection(scene::Context*, scene::DataSource*);
    auto get_selection(scene::Context*) -> scene::DataSource*;
}

// -----------------------------------------------------------------------------

namespace scene
{
    enum class NodeType
    {
        tree,
        texture,
        mesh,
        input_region,
    };

    struct Node
    {
        scene::NodeType type;

        scene::Tree* parent;

        ~Node();
    };

    namespace node
    {
        void unparent(scene::Node*);
    }
}

namespace scene
{
    struct Tree : scene::Node
    {
        scene::Context* ctx;

        vec2f32 translation;

        bool enabled;

        scene::SystemId system;
        void*           userdata;

        core::RefVector<scene::Node> children;

        ~Tree();
    };

    namespace tree
    {
        auto create(scene::Context*) -> core::Ref<scene::Tree>;

        void set_enabled(scene::Tree*, bool enabled);
        void place_below(scene::Tree*, scene::Node* reference, scene::Node* to_place);
        void place_above(scene::Tree*, scene::Node* reference, scene::Node* to_place);

        void set_translation(scene::Tree*, vec2f32 translation);

        inline
        auto get_position(scene::Tree* tree) -> vec2f32
        {
            return tree->translation + (tree->parent ? scene::tree::get_position(tree->parent) : vec2f32{});
        }
    }
}

namespace scene
{
    struct Texture : scene::Node
    {
        core::Ref<gpu::Image>   image;
        core::Ref<gpu::Sampler> sampler;
        gpu::BlendMode   blend;

        vec4u8   tint;
        aabb2f32 src;
        rect2f32 dst;
    };

    namespace texture
    {
        auto create(scene::Context*) -> core::Ref<scene::Texture>;
        void set_image(scene::Texture*, gpu::Image*, gpu::Sampler*, gpu::BlendMode);
        void set_tint( scene::Texture*, vec4u8   tint);
        void set_src(  scene::Texture*, aabb2f32 src);
        void set_dst(  scene::Texture*, rect2f32 dst);
        void damage(   scene::Texture*, aabb2i32 damage);
    }
}

namespace scene
{
    struct Mesh : scene::Node
    {
        core::Ref<gpu::Image>   image;
        core::Ref<gpu::Sampler> sampler;
        gpu::BlendMode   blend;

        aabb2f32 clip;

        std::vector<scene_vertex> vertices;
        std::vector<u16>          indices;
    };

    namespace mesh
    {
        auto create(scene::Context*) -> core::Ref<scene::Mesh>;
        void update(scene::Mesh*, gpu::Image*, gpu::Sampler*, gpu::BlendMode, aabb2f32 clip, std::span<const scene_vertex> vertices, std::span<const u16> indices);
    }
}

namespace scene
{
    struct InputRegion : scene::Node
    {
        scene::Client* client;

        region2f32 region;

        ~InputRegion();
    };

    namespace input_region
    {
        auto create(scene::Client*) -> core::Ref<scene::InputRegion>;
        void set_region(scene::InputRegion*, region2f32);
    }
}

// -----------------------------------------------------------------------------

namespace scene
{
    // Represents a normal interactable "toplevel" window.
    struct Window;

    namespace window
    {
        auto create(scene::Client*) -> core::Ref<scene::Window>;

        void set_title(scene::Window*, std::string_view title);

        void map(  scene::Window*);
        void unmap(scene::Window*);
        void raise(scene::Window*);

        auto get_tree(scene::Window*) -> scene::Tree*;

        void request_reposition(scene::Window*, rect2f32 frame, vec2f32 gravity);
        void set_frame(scene::Window*, rect2f32 frame);
        auto get_frame(scene::Window*) -> rect2f32;
    }

    auto find_window_at(scene::Context*, vec2f32 point) -> scene::Window*;
}

// -----------------------------------------------------------------------------

namespace scene
{
    enum class IterateAction
    {
        next, // Continue to next iteration action.
        skip, // Skip children.
        stop, // Stop iteration. If called in pre-visit, post-visit will be skipped.
    };

    static constexpr auto iterate_default = [](auto*) -> scene::IterateAction { return scene::IterateAction::next; };

    enum class IterateDirection
    {
        front_to_back,
        back_to_front,
    };

    template<typename Pre, typename Leaf, typename Post>
    auto iterate(scene::Tree* tree, scene::IterateDirection dir, Pre&& pre, Leaf&& leaf, Post&& post) -> scene::IterateAction
    {
        if (!tree->enabled) return scene::IterateAction::next;

        auto pre_action = pre(tree);
        if (pre_action == scene::IterateAction::stop) return scene::IterateAction::stop;
        if (pre_action == scene::IterateAction::skip) return scene::IterateAction::next;

        auto for_each = [&](auto&& children) -> scene::IterateAction {
            for (auto* child : children) {
                if (child->type == scene::NodeType::tree) {
                    if (scene::iterate(static_cast<scene::Tree*>(child), dir,
                            std::forward<Pre>(pre), std::forward<Leaf>(leaf), std::forward<Post>(post))
                                == scene::IterateAction::stop) {
                        return scene::IterateAction::stop;
                    }
                } else {
                    if (leaf(child) == scene::IterateAction::stop) return scene::IterateAction::stop;
                }
            }
            return scene::IterateAction::next;
        };

        auto action = dir == scene::IterateDirection::front_to_back
            ? for_each(tree->children | std::views::reverse)
            : for_each(tree->children);
        if (action == scene::IterateAction::stop) return action;

        return post(tree);
    }
}

// -----------------------------------------------------------------------------

namespace scene
{
    struct Hotkey
    {
        core::Flags<scene::Modifier> mod;
        scene::Scancode        code;

        constexpr bool operator==(const scene::Hotkey&) const noexcept = default;
    };
}

CORE_MAKE_STRUCT_HASHABLE(scene::Hotkey, v.mod, v.code)

namespace scene::client
{
    auto hotkey_register(  scene::Client*, scene::Hotkey) -> bool;
    void hotkey_unregister(scene::Client*, scene::Hotkey);
}

// -----------------------------------------------------------------------------

namespace scene
{
    enum class EventType
    {
        hotkey,

        keyboard_enter,
        keyboard_leave,
        keyboard_key,
        keyboard_modifier,

        pointer_enter,
        pointer_leave,
        pointer_motion,
        pointer_button,
        pointer_scroll,

        // Requests that a client adjust its position/size as requested.
        // This request does not need to be honoured, clients may update
        // their window frames at any time for any reason.
        window_reposition,

        output_added,
        output_configured,
        output_removed,
        output_layout,

        // Requests the output owner to make a `scene::frame` call at the
        // next time that the output would accept a content update.
        output_frame_request,

        // Sent before a frame may be composited to an output.
        // This may be sent even if there is no new scene graph changes
        // to commit, in response to a scene frame request.
        // Scene graph changes made directly in response to this event
        // will be applied immediately.
        output_frame,

        selection,
    };

    struct HotkeyEvent
    {
        scene::InputDevice* input_device;

        scene::Hotkey hotkey;
        bool         pressed;
    };

    struct KeyboardEvent
    {
        scene::Keyboard* keyboard;
        union {
            struct {
                scene::Scancode code;
                bool           pressed;
                bool           quiet;
            } key;
            struct {
                scene::InputRegion* region;
            } focus;
        };
    };

    struct PointerEvent
    {
        scene::Pointer* pointer;
        union {
            struct {
                scene::Scancode code;
                bool           pressed;
                bool           quiet;
            } button;
            struct {
                vec2f32 rel_accel;
                vec2f32 rel_unaccel;
            } motion;
            struct {
                vec2f32 delta;
            } scroll;
            struct {
                scene::InputRegion* region;
            } focus;
        };
    };

    struct WindowEvent
    {
        scene::Window* window;
        union {
            struct {
                rect2f32 frame;
                vec2f32  gravity;
            } reposition;
        };
    };

    struct RedrawEvent
    {
        scene::Output* output;
    };

    struct DataEvent
    {
        scene::DataSource* source;
    };

    struct Event
    {
        scene::EventType type;

        union {
            scene::HotkeyEvent   hotkey;
            scene::WindowEvent   window;
            scene::KeyboardEvent keyboard;
            scene::PointerEvent  pointer;
            scene::RedrawEvent   redraw;
            scene::Output*        output;
            scene::DataEvent     data;
        };
    };

    using EventHandlerFn = void(scene::Event*);

    namespace client
    {
        void set_event_handler(scene::Client*, std::move_only_function<scene::EventHandlerFn>&&);
    }
}
