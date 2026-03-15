#pragma once

#include "core/object.hpp"
#include "core/event.hpp"

#include "gpu/gpu.hpp"

// -----------------------------------------------------------------------------

namespace io
{
    struct Context;
}

// -----------------------------------------------------------------------------

namespace io
{
    enum class InputDeviceCapability : u32
    {
        libinput_led = 1 << 0,
    };

    struct InputDeviceInfo
    {
        core::Flags<io::InputDeviceCapability> capabilities;
    };

    /**
    * Generic raw input device interface.
    *
    * These input devices are modelled based on libinput and evdev, with normalized channel values.
    */
    struct InputDevice
    {
        virtual auto info() -> io::InputDeviceInfo = 0;
        virtual void update_leds(core::Flags<libinput_led>) {}
    };
}

// -----------------------------------------------------------------------------

namespace io
{
    enum class OutputCommitFlag : u32
    {
        vsync = 1 << 0,
    };

    struct OutputInfo
    {
        vec2u32 size;
        const gpu::FormatSet* formats;
    };

    /**
    * Generic output device interface.
    *
    * TODO: Support multi-plane configuration query and present.
    *       This interface is mostly temporary - the acquire/present
    *       model won't work for direct scanout of client-owned images.
    *       We'll also likely want to share swapchain logic (from `io/output.cpp`)
    *       with other systems - most notably screen video capture.
    */
    struct Output
    {
        virtual auto info() -> io::OutputInfo = 0;
        virtual void request_frame() = 0;
        virtual void commit(gpu::Image*, gpu::Syncpoint done, core::Flags<io::OutputCommitFlag>) = 0;
    };
}

// -----------------------------------------------------------------------------

namespace io
{
    enum class EventType
    {
        shutdown_requested,

        input_added,
        input_removed,
        input_event,

        output_added,       // Sent when an output is first detected
        output_removed,     // Sent before a output is removed from the output list
        output_configure,   // Sent when an output's configuration changes
        output_frame,       // Sent when an output can accept a new frame of content
    };
}

// -----------------------------------------------------------------------------

namespace io
{
    enum class ShutdownReason
    {
        no_more_outputs,        // Sent when no more outputs will be opened by the backend
        terminate_received,    // Sent when SIGTERM is received
        interrupt_received,    // Sent when SIGINT  is received
    };

    struct ShutdownEvent
    {
        io::ShutdownReason reason;
    };
}

// -----------------------------------------------------------------------------

namespace io
{
    struct InputChannel
    {
        u32 type;   // evdev type
        u32 code;   // evdev code
        f32 value;  // normalized channel value
    };

    /**
    * A grouping of associated input channel events.
    * This event reuses evdev codes for simplicity, with some minor changes:
    * - Values are sent as normalized floating point quantities.
    * - `REL_(H)WHEEL` events are sent with fractional detent deltas (~15 degrees / detent), instead of `REL_(H)WHEEL_HI_RES`.
    * - No `SYN_*` events are sent, instead `io::InputEvent` contains *all* events since the last report, and
    *   io always re-synchronizes internally.
    *
    * The `quiet` flag denotes that actions should not be taken in response to this event. E.g. on input
    * enter/leave events.
    *
    * Event design for touch, tablet and gesture controls is still pending.
    */
    struct InputEvent
    {
        io::InputDevice* device;
        bool quiet;
        std::span<const io::InputChannel> channels;
    };
}

// -----------------------------------------------------------------------------

namespace io
{
    struct OutputEvent
    {
        io::Output* output;
    };
}

// -----------------------------------------------------------------------------

namespace io
{
    struct Event
    {
        io::EventType type;

        union {
            io::ShutdownEvent shutdown;
            io::InputEvent    input;
            io::OutputEvent   output;
        };
    };

    using EventHandler = void(io::Event*);
}

// -----------------------------------------------------------------------------

namespace io
{
    auto create(core::EventLoop*, gpu::Context*) -> core::Ref<io::Context>;
    void set_event_handler(io::Context*, std::move_only_function<io::EventHandler>&&);
    void run( io::Context*);
    void stop(io::Context*);

    void add_output(  io::Context*);
    void close_output(io::Output*);
}
