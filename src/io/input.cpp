#include "internal.hpp"

#include "core/stack.hpp"

void io::input_device::add(io::InputDeviceBase* device)
{
    core_assert(!std::ranges::contains(device->ctx->input_devices, device));
    device->ctx->input_devices.emplace_back(device);
    io::post_event(device->ctx, core::ptr_to(io::Event {
        .type = io::EventType::input_added,
        .input = io::InputEvent {
            .device = device,
        },
    }));
}

void io::input_device::remove(io::InputDeviceBase* device)
{
    if (std::erase(device->ctx->input_devices, device)) {
        io::post_event(device->ctx, core::ptr_to(io::Event {
            .type = io::EventType::input_removed,
            .input = io::InputEvent {
                .device = device,
            },
        }));
    }
}

static
void post_input(io::InputDeviceBase* device, bool quiet, std::span<const io::InputChannel> channels)
{
    io::post_event(device->ctx, core::ptr_to(io::Event {
        .type = io::EventType::input_event,
        .input = io::InputEvent {
            .device = device,
            .quiet = quiet,
            .channels = channels,
        },
    }));
}

void io::input_device::leave(io::InputDeviceBase* device)
{
    core::ThreadStack stack;
    auto* events = stack.allocate<io::InputChannel>(device->pressed.size());
    usz count = 0;
    for (auto key : device->pressed) {
        events[count++] = {EV_KEY, key, 0};
    }
    if (count) {
        post_input(device, true, {events, count});
    }
    device->pressed.clear();
}

void io::input_device::key_enter(io::InputDeviceBase* device, std::span<const u32> keys)
{
    core::ThreadStack stack;
    auto* events = stack.allocate<io::InputChannel>(keys.size());
    usz count = 0;
    for (auto key : keys) {
        if (device->pressed.insert(key).second) {
            events[count++] = {EV_KEY, key, 1};
        }
    }
    if (count) {
        post_input(device, true, {events, count});
    }
}

void io::input_device::key_press(io::InputDeviceBase* device, u32 key)
{
    if (device->pressed.insert(key).second) {
        post_input(device, false, {{EV_KEY, key, 1}});
    }
}

void io::input_device::key_release(io::InputDeviceBase* device, u32 key)
{
    if (device->pressed.erase(key)) {
        post_input(device, false, {{EV_KEY, key, 0}});
    }
}

static
void post_rel2(io::InputDeviceBase* device, vec2u32 code, vec2f32 delta)
{
    io::InputChannel events[2];
    u32 count = 0;
    if (delta.x) events[count++] = {EV_REL, code.x, delta.x};
    if (delta.y) events[count++] = {EV_REL, code.y, delta.y};
    post_input(device, false, std::span(events, count));
}

void io::input_device::pointer_motion(io::InputDeviceBase* device, vec2f32 delta)
{
    post_rel2(device, {REL_X, REL_Y}, delta);
}

void io::input_device::pointer_scroll(io::InputDeviceBase* device, vec2f32 delta)
{
    post_rel2(device, {REL_HWHEEL, REL_WHEEL}, delta);
}
