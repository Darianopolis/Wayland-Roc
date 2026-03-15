#include "internal.hpp"

auto scene::data_source::create(scene::Client* client, scene::DataSourceOps&& ops) -> core::Ref<scene::DataSource>
{
    auto source = core::create<scene::DataSource>();
    source->ops = std::move(ops);
    source->client = client;
    return source;
}

void scene::data_source::offer(scene::DataSource* source, const char* mime_type)
{
    source->offered.insert(mime_type);
}

static
void offer_selection(scene::Context* ctx, scene::DataSource* source)
{
    for (auto* client : ctx->clients) {
        scene::offer_selection(client, source);
    }
}

void scene::set_selection(scene::Context* ctx, scene::DataSource* source)
{
    if (ctx->selection) {
        ctx->selection->ops.cancel();
    }
    ctx->selection = source;
    ::offer_selection(ctx, source);
}

auto scene::get_selection(scene::Context* ctx) -> scene::DataSource*
{
    return ctx->selection.get();
}

scene::DataSource::~DataSource()
{
}

void scene::offer_selection(scene::Client* client, scene::DataSource* source)
{
    scene_client_post_event(client, core::ptr_to(scene::Event {
        .type = scene::EventType::selection,
        .data {
            .source =source,
        },
    }));
}

auto scene::data_source::get_offered(scene::DataSource* source) -> std::span<const std::string>
{
    return source->offered;
}

// -----------------------------------------------------------------------------

void scene::data_source::send(scene::DataSource* source, const char* mime_type, int fd)
{
    log_debug("scene::data_source::send({}, {}, {})", (void*)source, mime_type, fd);
    source->ops.send(mime_type, fd);
}
