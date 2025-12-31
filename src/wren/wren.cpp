#include "wren_internal.hpp"

#include "wrei/types.hpp"
#include "wrei/util.hpp"

wren_context::~wren_context()
{
    log_info("Wren context destroyed");

    assert(submissions.empty());

    pending_acquires.clear();

    assert(stats.active_images == 0);
    assert(stats.active_buffers == 0);
    assert(stats.active_samplers == 0);

    vkwsi_context_destroy(vkwsi);

    timeline = nullptr;

    vmaDestroyAllocator(vma);

    vk.DestroyPipelineLayout(device, pipeline_layout, nullptr);
    vk.DestroyDescriptorSetLayout(device, set_layout, nullptr);
    vk.DestroyDescriptorPool(device, pool, nullptr);

    vk.DestroyCommandPool(device, cmd_pool, nullptr);
    vk.DestroyDevice(device, nullptr);
    vk.DestroyInstance(instance, nullptr);
}

VkCommandBuffer wren_begin_commands(wren_context* ctx)
{
    VkCommandBuffer cmd;
    wren_check(ctx->vk.AllocateCommandBuffers(ctx->device, wrei_ptr_to(VkCommandBufferAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = ctx->cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    }), &cmd));

    wren_check(ctx->vk.BeginCommandBuffer(cmd, wrei_ptr_to(VkCommandBufferBeginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    })));

    return cmd;
}

static
void check_complete_submissions(wren_context* ctx)
{
    u64 value;
    ctx->vk.GetSemaphoreCounterValue(ctx->device, ctx->timeline->semaphore, &value);

    while (!ctx->submissions.empty()) {
        auto& sub = ctx->submissions.front();
        if (sub.syncpoint.value <= value) {
            // log_debug("RECLAIMING PREVIOUS SUBMISSION");

            ctx->vk.FreeCommandBuffers(ctx->device, ctx->cmd_pool, 1, &sub.cmd);
            ctx->submissions.pop_front();
        } else {
            break;
        }
    }
}

wren_syncpoint wren_submit(wren_context* ctx, VkCommandBuffer cmd, std::span<wrei_object* const> objects, wren_semaphore* signal)
{
    assert(!signal || signal->type == VK_SEMAPHORE_TYPE_BINARY);

    check_complete_submissions(ctx);

    auto& submission = ctx->submissions.emplace_back();
    submission.cmd = cmd;
    for (auto& o : objects) submission.objects.emplace_back(o);

    wren_check(ctx->vk.EndCommandBuffer(cmd));

    wren_syncpoint sync { ++ctx->timeline->value };
    submission.syncpoint = sync;

    // log_warn("submitting, sync point: {}", sync.value);

    // Flush pending acquire waits

    std::vector<VkSemaphoreSubmitInfo> waits;
    waits.reserve(ctx->pending_acquires.size());
    for (auto& acquire : ctx->pending_acquires) {
        // log_warn("  WAITING ON ACQUIRE SEMPAHORE: {}", (void*)acquire->semaphore);
        submission.objects.emplace_back(acquire);
        waits.emplace_back(VkSemaphoreSubmitInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = acquire->semaphore,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        });
    }
    ctx->pending_acquires.clear();

    wren_check(ctx->vk.QueueSubmit2(ctx->queue, 1, wrei_ptr_to(VkSubmitInfo2 {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = u32(waits.size()),
        .pWaitSemaphoreInfos = waits.data(),
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = wrei_ptr_to(VkCommandBufferSubmitInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = cmd,
        }),
        .signalSemaphoreInfoCount = signal ? 2u : 1u,
        .pSignalSemaphoreInfos = std::array {
            VkSemaphoreSubmitInfo {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = ctx->timeline->semaphore,
                .value = sync.value,
                .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            },
            VkSemaphoreSubmitInfo {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = signal ? signal->semaphore : nullptr,
                .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            }
        }.data(),
    }), nullptr));

    return sync;
}

void wren_flush(wren_context* ctx)
{
    ctx->vk.QueueWaitIdle(ctx->queue);
    check_complete_submissions(ctx);
}