#include "internal.hpp"

io::OutputBase::~OutputBase()
{
    io::output::remove(this);
}

void io::output::add(io::OutputBase* output)
{
    core_assert(!std::ranges::contains(output->ctx->outputs, output));
    output->ctx->outputs.emplace_back(output);
    io::post_event(output->ctx, core::ptr_to(io::Event {
        .type = io::EventType::output_added,
        .output = {
            .output = output
        },
    }));
}

void io::output::remove(io::OutputBase* output)
{
    if (std::erase(output->ctx->outputs, output)) {
        io::post_event(output->ctx, core::ptr_to(io::Event {
            .type = io::EventType::output_removed,
            .output = {
                .output = output
            },
        }));
    }
}

// -----------------------------------------------------------------------------

void io::output::try_redraw(io::OutputBase* output)
{
    if (!output->frame_requested) return;
    if (!output->commit_available) return;
    if (!output->size.x || !output->size.y) return;

    output->frame_requested = false;

    io::post_event(output->ctx, core::ptr_to(io::Event {
        .type = io::EventType::output_frame,
        .output = {
            .output = output,
        }
    }));
}

void io::output::try_redraw_later(io::OutputBase* output)
{
    core::event_loop::enqueue(output->ctx->event_loop, [output = core::Weak(output)] {
        if (output) {
            io::output::try_redraw(output.get());
        }
    });
}

void io::OutputBase::request_frame()
{
    frame_requested = true;
    io::output::try_redraw_later(this);
}

void io::output::post_configure(io::OutputBase* output)
{
    io::post_event(output->ctx, core::ptr_to(io::Event {
        .type = io::EventType::output_configure,
        .output = {
            .output = output,
        }
    }));
}
