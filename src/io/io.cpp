#include "internal.hpp"

auto io::create(core::EventLoop* event_loop, gpu::Context* gpu) -> core::Ref<io::Context>
{
    auto ctx = core::create<io::Context>();

    ctx->event_loop = event_loop;
    ctx->gpu = gpu;

    io::session::init( ctx.get());
    io::libinput::init(ctx.get());
    io::evdev::init(   ctx.get());
    io::drm::init(     ctx.get());
    io::wayland::init( ctx.get());

    return ctx;
}

static
void shutdown(io::Context* ctx)
{
    ctx->wayland.destroy();
    ctx->drm.destroy();
    ctx->evdev.destroy();
    ctx->libinput.destroy();
    ctx->session.destroy();

    core::event_loop::stop(ctx->event_loop);
}

io::Context::~Context()
{
    core_assert(!wayland);
    core_assert(!drm);
    core_assert(!evdev);
    core_assert(!libinput);
    core_assert(!session);
}

void io::set_event_handler(io::Context* ctx, std::move_only_function<io::EventHandler>&& handler)
{
    ctx->event_handler = std::move(handler);
}

static
core::Weak<io::Context> signal_context;

static
void signal_handler(int sig)
{
    if (sig == SIGINT) {
        // Immediately unregister SIGINT in case of unresponsive event loop
        std::signal(sig, SIG_DFL);
    }

    if (!signal_context) return;

    switch (sig) {
        break;case SIGTERM: io::request_shutdown(signal_context.get(), io::ShutdownReason::terminate_received);
        break;case SIGINT:  io::request_shutdown(signal_context.get(), io::ShutdownReason::interrupt_received);
    }
}

void io::run(io::Context* ctx)
{
    core_assert(ctx->event_handler);

    signal_context = ctx;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (ctx->wayland) {
        io::wayland::start(ctx);
    }

    core::event_loop::run(ctx->event_loop);

    signal_context = nullptr;
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_IGN);
}

void io::request_shutdown(io::Context* ctx, io::ShutdownReason reason)
{
    core::event_loop::enqueue(ctx->event_loop, [ctx, reason] {
        io::post_event(ctx, core::ptr_to(io::Event {
            .type = io::EventType::shutdown_requested,
            .shutdown {
                .reason = reason,
            }
        }));
    });
}

void io::stop(io::Context* ctx)
{
    if (ctx->stop_requested) return;
    ctx->stop_requested = true;

    core::event_loop::enqueue(ctx->event_loop, [ctx] {
        shutdown(ctx);
    });
}

void io::post_event(io::Context* ctx, io::Event* event)
{
    ctx->event_handler(event);
}
