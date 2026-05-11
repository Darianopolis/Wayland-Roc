#include "shell.hpp"

#include <core/math.hpp>

#include <wm/wm.hpp>
#include <ui/ui.hpp>

auto main(int argc, char* argv[]) -> int
{
    log_init(PROGRAM_NAME ".log");
    fd_registry_init();
    registry_init();
    defer {
        registry_deinit();
        fd_registry_deinit();
        log_deinit();
    };

    log_info("{} ({:n:})", PROJECT_NAME, std::span<const char* const>(argv, argc));

    Shell shell = {};

    // Config

    shell.app_share = std::filesystem::path(getenv("HOME")) / ".local/share" / PROGRAM_NAME;
    shell.wallpaper = getenv("WALLPAPER") ?: "";
    if (getenv("WAYLAND_DISPLAY")) {
        log_debug("Running nested!");
        shell.main_mod = WmModifier::alt;
    } else {
        log_debug("Running in direct session");
        shell.main_mod = WmModifier::super;
    }

    // Systems

    auto exec = exec_create();
    auto gpu = gpu_create(exec.get(), {});
    auto io = io_create(exec.get(), gpu.get());
    auto wm = wm_create({
        .exec = exec.get(),
        .gpu = gpu.get(),
        .main_mod = shell.main_mod,
    });
    auto way = way_create(exec.get(), gpu.get(), wm.get());

    shell.exec = exec.get();
    shell.gpu = gpu.get();
    shell.way = way.get();
    shell.io = io.get();
    shell.wm = wm.get();

    // Applets

    auto _ = shell_init_background(&shell);
    auto _ = shell_init_launcher(&shell);
    auto _ = shell_init_log_viewer(&shell);
    auto _ = shell_init_menu(&shell);

    shell_init_xwayland(&shell, argc, argv);

    // Copy test

    static
    const char* text_mime_types[] = {
        "text/plain;charset=utf-8",
        "text/plain",
        "text/html",
    };
    struct Source : WmDataSource
    {
        virtual void on_cancel() final override {}
        virtual void on_send(const char *mime_type, fd_t target) final override
        {
            char message[] = "Hello, world!";
            write(target, message, sizeof(message) - 1);
        }
    };
    auto source = ref_create<Source>();
    for (auto* mime : text_mime_types) {
        wm_data_source_offer(source.get(), mime);
    }
    for (auto* seat : wm_get_seats(wm.get())) {
        wm_set_selection(seat, source.get());
    }

    // IO

    shell_run_io(&shell);
}
