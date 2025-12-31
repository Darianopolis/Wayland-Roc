#include "wren_internal.hpp"

ref<wren_semaphore> wren_semaphore_create(wren_context* ctx, VkSemaphoreType type, u64 initial_value)
{
    auto semaphore = wrei_create<wren_semaphore>();
    semaphore->ctx = ctx;
    semaphore->type = type;
    semaphore->value = initial_value;

    wren_check(ctx->vk.CreateSemaphore(ctx->device, wrei_ptr_to(VkSemaphoreCreateInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = wrei_ptr_to(VkSemaphoreTypeCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .semaphoreType = type,
            .initialValue = initial_value,
        }),
    }), nullptr, &semaphore->semaphore));

    return semaphore;
}

wren_semaphore::~wren_semaphore()
{
    ctx->vk.DestroySemaphore(ctx->device, semaphore, nullptr);
}
