#pragma once

#include "io.hpp"

#define IO_BACKEND(Type, Name) \
    struct Type; \
    namespace Name \
    { \
        void init(io::Context*); \
    }

namespace io
{
    IO_BACKEND(Udev, udev);
    IO_BACKEND(Session, session);
    IO_BACKEND(Libinput, libinput);
    IO_BACKEND(Evdev, evdev);
    IO_BACKEND(Drm, drm);
    IO_BACKEND(Wayland, wayland);

    namespace wayland
    {
        void start(io::Context*);
    }

    struct InputDeviceBase;
    struct OutputBase;

    struct Context
    {
        std::move_only_function<io::EventHandler> event_handler;

        bool stop_requested = false;

        core::EventLoop* event_loop;
        gpu::Context*     gpu;

        std::vector<io::InputDeviceBase*> input_devices;
        std::vector<io::OutputBase*>       outputs;

        core::Ref<io::Udev>     udev;
        core::Ref<io::Session>  session;
        core::Ref<io::Libinput> libinput; // input_device
        core::Ref<io::Evdev>    evdev;    // input_device
        core::Ref<io::Drm>      drm;      // output
        core::Ref<io::Wayland>  wayland;  // output | input_device

        ~Context();
    };

    void request_shutdown(io::Context* ctx, io::ShutdownReason reason);

    void post_event(io::Context*, io::Event*);
}

// -----------------------------------------------------------------------------

namespace io
{
    struct OutputBase : io::Output
    {
        io::Context* ctx;

        bool frame_requested;

        vec2u32 size;

        // True if commit will accept a new frame
        bool commit_available = true;

        virtual void request_frame() final override;

        virtual ~OutputBase();
    };
}

namespace io::output
{
    void try_redraw(io::OutputBase*);
    void try_redraw_later(io::OutputBase*);
    void post_configure(io::OutputBase*);

    void add(   io::OutputBase*);
    void remove(io::OutputBase*);
}

// -----------------------------------------------------------------------------

namespace io
{
    /**
    * Base type for input devices. There are three types of input devices:
    * 1. libinput - Handles keyboard/pointer/tablet/gesture/switch devices with seat access.
    * 2. wayland  - Handles the above devices when running in a nested Wayland session.
    * 3. evdev    - Handles all remaining input devices (gamepad/joystick/etc...) that do not require privileged seat access.
    */
    struct InputDeviceBase : io::InputDevice
    {
        io::Context* ctx;

        core::Flags<io::InputDeviceCapability> capabilities;

        core::FlatSet<u32> pressed;

        auto info() -> io::InputDeviceInfo
        {
            return { .capabilities = capabilities };
        }

        virtual ~InputDeviceBase() = default;
    };
}

namespace io::input_device
{
    void add(           io::InputDeviceBase*);
    void remove(        io::InputDeviceBase*);
    void leave(         io::InputDeviceBase*);
    void key_enter(     io::InputDeviceBase*, std::span<const u32> keys);
    void key_press(     io::InputDeviceBase*, u32 key);
    void key_release(   io::InputDeviceBase*, u32 key);
    void pointer_motion(io::InputDeviceBase*, vec2f32 delta);
    void pointer_scroll(io::InputDeviceBase*, vec2f32 delta);
}
