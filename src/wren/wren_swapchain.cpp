#include "wren_internal.hpp"

static
void request_acquire(wren_swapchain* swapchain)
{
    swapchain->acquire_semaphore = wren_semaphore_create(swapchain->ctx, VK_SEMAPHORE_TYPE_BINARY);
    swapchain->current_index = wren_swapchain::invalid_index;

    swapchain->can_acquire = true;
    swapchain->can_acquire.notify_one();
}

static
void acquire_thread(wren_swapchain* swapchain)
{
    auto* ctx = swapchain->ctx;
    for (;;) {

        swapchain->can_acquire.wait(false);

        if (swapchain->destroy_requested) {
            log_error("DESTROY REQUESTED BEFORE ACQUIRE, CLOSING");
            swapchain->current_index = wren_swapchain::invalid_index;
            return;
        }

        u32 image_index = wren_swapchain::invalid_index;
        wren_check(ctx->vk.AcquireNextImageKHR(ctx->device, swapchain->swapchain, UINT64_MAX, swapchain->acquire_semaphore->semaphore, nullptr, &image_index));

        swapchain->current_index = image_index;

        if (swapchain->destroy_requested) {
            log_error("DESTROY REQUESTED AFTER ACQUIRE, CLOSING");
            return;
        }

        swapchain->can_acquire = false;

        swapchain->acquire_callback({
            .userdata = swapchain->acquire_callback_data,
            .swapchain = swapchain,
            .index = image_index,
        });
    }
}

void wren_swapchain_confirm_acquire(const wren_swapchain_acquire_data& acquire)
{
    auto* swapchain = acquire.swapchain;
    swapchain->ctx->pending_acquires.emplace_back(std::move(swapchain->acquire_semaphore));
}

wren_swapchain::~wren_swapchain()
{
    log_error("SWAPCHAIN BEING DESTROYED");

    destroy_requested = true;
    can_acquire = true;
    can_acquire.notify_one();

    acquire_thread.join();

    log_error("ACQUIRE THREAD JOINED");

    if (current_index != invalid_index) {
        log_error("releasing swapchain images");
        ctx->vk.ReleaseSwapchainImagesKHR(ctx->device, wrei_ptr_to(VkReleaseSwapchainImagesInfoKHR {
            .sType = VK_STRUCTURE_TYPE_RELEASE_SWAPCHAIN_IMAGES_INFO_KHR,
            .swapchain = swapchain,
            .imageIndexCount = 1,
            .pImageIndices = &current_index,
        }));
    }

    images.clear();
    present_semaphores.clear();
    ctx->vk.DestroySwapchainKHR(ctx->device, swapchain, nullptr);
}

static
void swapchain_recreate(wren_swapchain* swapchain)
{
    auto* ctx = swapchain->ctx;

    // u32 min_image_count = 2;
    // VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;

    u32 min_image_count = 4;
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_MAILBOX_KHR;

    VkExtent2D extent { swapchain->pending.extent.x, swapchain->pending.extent.y };

    auto old_swapchain = swapchain->swapchain;
    wren_check(ctx->vk.CreateSwapchainKHR(ctx->device, wrei_ptr_to(VkSwapchainCreateInfoKHR {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = swapchain->surface,
        .minImageCount = min_image_count,
        .imageFormat = swapchain->format->vk,
        .imageColorSpace = swapchain->colorspace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
            // TODO: DONT DO THIS - Use an intermediary buffer or multi-view rendering for screenshots
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .clipped = true,
        .oldSwapchain = old_swapchain,
    }), nullptr, &swapchain->swapchain));

    swapchain->extent = swapchain->pending.extent;

    ctx->vk.DestroySwapchainKHR(ctx->device, old_swapchain, nullptr);

    u32 image_count;
    wren_check(ctx->vk.GetSwapchainImagesKHR(ctx->device, swapchain->swapchain, &image_count, nullptr), VK_INCOMPLETE);

    std::vector<VkImage> images(image_count);
    wren_check(ctx->vk.GetSwapchainImagesKHR(ctx->device, swapchain->swapchain, &image_count, images.data()));

    swapchain->images.clear();
    for (auto& vk_image : images) {
        auto image = swapchain->images.emplace_back(wrei_create<wren_image>()).get();
        image->image = vk_image;
        image->ctx = ctx;
        image->extent = vec2u32(extent.width, extent.height);
        image->format = swapchain->format;

        wren_check(ctx->vk.CreateImageView(ctx->device, wrei_ptr_to(VkImageViewCreateInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image->image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = image->format->vk,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        }), nullptr, &image->view));
    }

    swapchain->present_semaphores.resize(image_count);
    for (auto& sema : swapchain->present_semaphores) {
        if (!sema) sema = wren_semaphore_create(ctx, VK_SEMAPHORE_TYPE_BINARY);
    }
}

ref<wren_swapchain> wren_swapchain_create(
    wren_context* ctx,
    VkSurfaceKHR surface,
    wren_format format,
    wren_acquire_callback_fn callback,
    void* userdata)
{
    ref swapchain = wrei_create<wren_swapchain>();
    swapchain->ctx = ctx;
    swapchain->format = format;
    swapchain->surface = surface;
    swapchain->acquire_callback = callback;
    swapchain->acquire_callback_data = userdata;

    std::vector<VkSurfaceFormatKHR> surface_formats;
    wren_vk_enumerate(surface_formats, ctx->vk.GetPhysicalDeviceSurfaceFormatsKHR, ctx->physical_device, surface);
    bool found = false;
    for (auto& f : surface_formats) {
        // TODO: Better format selection
        if (f.format == format->vk) {
            swapchain->colorspace = f.colorSpace;
            found = true;
            break;
        }
    }
    if (!found) {
        log_error("Could not find appropriate swapchain format");
        return nullptr;
    }

    return swapchain;
}

void wren_swapchain_resize(wren_swapchain* swapchain, vec2u32 size)
{
    if (swapchain->swapchain) {
        swapchain->pending.extent = size;
    } else {
        swapchain->pending.extent = size;
        swapchain_recreate(swapchain);

        swapchain->acquire_thread = std::jthread{acquire_thread, swapchain};
        request_acquire(swapchain);
    }
}

wren_image* wren_swapchain_get_current(wren_swapchain* swapchain)
{
    return swapchain->images[swapchain->current_index].get();
}

wren_syncpoint wren_submit_and_present(
    wren_context* ctx,
    VkCommandBuffer cmd,
    std::span<wrei_object* const> _objects,
    wren_swapchain* swapchain)
{
    auto semaphore = swapchain->present_semaphores[swapchain->current_index].get();

    std::vector<wrei_object*> objects(_objects.begin(), _objects.end());
    objects.emplace_back(swapchain);
    auto sync = wren_submit(ctx, cmd, objects, semaphore);

    // log_trace("presenting");
    wren_check(ctx->vk.QueuePresentKHR(ctx->queue, wrei_ptr_to(VkPresentInfoKHR {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &semaphore->semaphore,
        .swapchainCount = 1,
        .pSwapchains = &swapchain->swapchain,
        .pImageIndices = &swapchain->current_index,
    })));
    // log_trace("present complete");

    if (swapchain->pending.extent != swapchain->extent) {
        ctx->vk.QueueWaitIdle(ctx->queue);
        swapchain_recreate(swapchain);
    }

    request_acquire(swapchain);

    return sync;
}
