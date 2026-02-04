#include "internal.hpp"

ref<wren_semaphore> wren_semaphore_create(wren_context* ctx)
{
    auto semaphore = wrei_create<wren_semaphore>();
    semaphore->ctx = ctx;

    unix_check(drmSyncobjCreate(ctx->drm_fd, 0, &semaphore->syncobj));

    return semaphore;
}

ref<wren_semaphore> wren_semaphore_import_syncobj(wren_context* ctx, int syncobj_fd)
{
    auto semaphore = wrei_create<wren_semaphore>();

    unix_check(drmSyncobjFDToHandle(ctx->drm_fd, syncobj_fd, &semaphore->syncobj));

    return semaphore;
}

int wren_semaphore_export_syncobj(wren_semaphore* semaphore)
{
    int fd = -1;
    unix_check(drmSyncobjHandleToFD(semaphore->ctx->drm_fd, semaphore->syncobj, &fd));
    return fd;
}

void wren_semaphore_import_syncfile(wren_semaphore* semaphore, int sync_fd, u64 target_point)
{
    auto* ctx = semaphore->ctx;

    if (sync_fd == -1) {
        unix_check(drmSyncobjTimelineSignal(ctx->drm_fd, &semaphore->syncobj, &target_point, 1));
        return;
    }

    // We can't import from a syncfile directly to a syncobj point, so we import to a non-timeline syncobj first
    // and then transfer to our target point from that.

    u32 syncobj = {};
    unix_check(drmSyncobjCreate(ctx->drm_fd, 0, &syncobj));
    defer { unix_check(drmSyncobjDestroy(ctx->drm_fd, syncobj)); };
    unix_check(drmSyncobjImportSyncFile(ctx->drm_fd, syncobj, sync_fd));
    unix_check(drmSyncobjTransfer(ctx->drm_fd, semaphore->syncobj, target_point, syncobj, 0, 0));
}

int wren_semaphore_export_syncfile(wren_semaphore* semaphore, u64 source_point)
{
    auto* ctx = semaphore->ctx;

    // We can't export directly from a timeline syncobj to a syncfile, so we transfer to a non-timeline syncobj first
    // and then export the syncfile from that.

    u32 syncobj = {};
    unix_check(drmSyncobjCreate(ctx->drm_fd, 0, &syncobj));
    defer { unix_check(drmSyncobjDestroy(ctx->drm_fd, syncobj)); };
    unix_check(drmSyncobjTransfer(ctx->drm_fd, syncobj, 0, semaphore->syncobj, source_point, 0));
    int sync_fd = -1;
    unix_check(drmSyncobjExportSyncFile(ctx->drm_fd, syncobj, &sync_fd));

    return sync_fd;
}

wren_semaphore::~wren_semaphore()
{
    unix_check(drmSyncobjDestroy(ctx->drm_fd, syncobj));
}

u64 wren_semaphore_get_value(wren_semaphore* semaphore)
{
    auto* ctx = semaphore->ctx;

    u64 value = 0;
    unix_check(drmSyncobjQuery2(ctx->drm_fd, &semaphore->syncobj, &value, 1, 0));

    return value;
}

void wren_semaphore_waiter::handle(const epoll_event&)
{
    if (wrei_eventfd_read(fd)) {
        process_signalled(nullptr, 0);
    }
}

void wren_semaphore_waiter::process_signalled(wren_semaphore* semaphore, u64 value)
{
    std::erase_if(waits, [&](wait_item& item) -> bool {
        auto sema = item.semaphore.get();
        if (!sema) {
            log_warn("Semaphore destroyed before value was signalled");
            return true;
        }

        if (semaphore && sema != semaphore) return false;

        u64 signalled = semaphore ? value :  wren_semaphore_get_value(item.semaphore.get());

        if (signalled >= item.value) {
            item.callback(signalled);
            return true;
        }
        return false;
    });
}

void wren_semaphore_wait_value(wren_semaphore* semaphore, u64 value, std::move_only_function<wren_semaphore_wait_fn> fn)
{
    auto* ctx = semaphore->ctx;

    ctx->waiter->waits.emplace_back(wren_semaphore_waiter::wait_item {
        .semaphore = semaphore,
        .value = value,
        .callback = std::move(fn),
    });

    unix_check(drmIoctl(ctx->drm_fd, DRM_IOCTL_SYNCOBJ_EVENTFD, wrei_ptr_to(drm_syncobj_eventfd {
        .handle = semaphore->syncobj,
        .point = value,
        .fd = ctx->waiter->fd,
    })));
}

void wren_semaphore_wait_value(wren_semaphore* semaphore, u64 value)
{
    auto* ctx = semaphore->ctx;

    u32 first_signalled;
    unix_check(drmSyncobjTimelineWait(ctx->drm_fd,
        &semaphore->syncobj, &value, 1, INT64_MAX, 0, &first_signalled));

    if (std::this_thread::get_id() == ctx->event_loop->main_thread) {
        ctx->waiter->process_signalled(semaphore, value);
    }
}

void wren_semaphore_signal_value(wren_semaphore* semaphore, u64 value)
{
    auto* ctx = semaphore->ctx;

    unix_check(drmSyncobjTimelineSignal(ctx->drm_fd, &semaphore->syncobj, &value, 1));
}

// -----------------------------------------------------------------------------

ref<wren_binary_semaphore> wren_binary_semaphore_create(wren_context* ctx)
{
    auto binary = wrei_create<wren_binary_semaphore>();
    binary->ctx = ctx;

    if (!ctx->free_binary_semaphores.empty()) {
        binary->semaphore = ctx->free_binary_semaphores.back();
        ctx->free_binary_semaphores.pop_back();
    } else {
        wren_check(ctx->vk.CreateSemaphore(ctx->device, wrei_ptr_to(VkSemaphoreCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = wrei_ptr_to(VkExportSemaphoreCreateInfo {
                .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
                .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
            }),
        }), nullptr, &binary->semaphore));
    }

    return binary;
}

ref<wren_binary_semaphore> wren_semaphore_export_binary(wren_semaphore* semaphore, u64 source_point)
{
    auto sync_fd = wren_semaphore_export_syncfile(semaphore, source_point);

    auto* ctx = semaphore->ctx;

    auto binary = wren_binary_semaphore_create(ctx);

    wren_check(ctx->vk.ImportSemaphoreFdKHR(ctx->device, wrei_ptr_to(VkImportSemaphoreFdInfoKHR {
        .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
        .semaphore = binary->semaphore,
        .flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
        .fd = sync_fd,
    })));

    return binary;
}

void wren_semaphore_import_binary(wren_semaphore* semaphore, wren_binary_semaphore* binary, u64 target_point)
{
    auto* ctx = semaphore->ctx;

    int syncobj_fd = -1;
    wren_check(ctx->vk.GetSemaphoreFdKHR(ctx->device, wrei_ptr_to(VkSemaphoreGetFdInfoKHR {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
        .semaphore = binary->semaphore,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
    }), &syncobj_fd));

    wren_semaphore_import_syncfile(semaphore, syncobj_fd, target_point);

    close(syncobj_fd);
}

wren_binary_semaphore::~wren_binary_semaphore()
{
    ctx->free_binary_semaphores.emplace_back(semaphore);
}
