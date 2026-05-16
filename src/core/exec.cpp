#include "exec.hpp"

#include "chrono.hpp"
#include "util.hpp"
#include "log.hpp"

// -----------------------------------------------------------------------------

static
auto to_epoll_events(Flags<FdEventBit> events) -> u32
{
    u32 out = 0;
    if (events.contains(FdEventBit::readable)) out |= EPOLLIN;
    if (events.contains(FdEventBit::writable)) out |= EPOLLOUT;
    return out;
}

static
auto from_epoll_events(u32 events) -> Flags<FdEventBit>
{
    Flags<FdEventBit> out = {};
    if (events & EPOLLIN)  out |= FdEventBit::readable;
    if (events & EPOLLOUT) out |= FdEventBit::writable;
    return out;
}

auto exec_create() -> Ref<ExecContext>
{
    auto exec = ref_create<ExecContext>();

    exec->os_thread = std::this_thread::get_id();

    exec->epoll_fd = Fd(unix_check<epoll_create1>(EPOLL_CLOEXEC).value);

    return exec;
}

thread_local ExecContext* exec_thread_context;

void exec_set_thread_context(ExecContext* exec)
{
    exec_thread_context = exec;
}

auto exec_get_thread_context() -> ExecContext*
{
    return exec_thread_context;
}

static
void check_all_stopped(ExecContext* exec)
{
    debug_assert(exec->idle.listeners.empty());

    for (auto[i, listener] : exec->listeners | std::views::enumerate) {
        debug_assert(!listener, "Listener for ({}) still registered", i);
    }
}

ExecContext::~ExecContext()
{
    debug_assert(stopped);

    check_all_stopped(this);
}

void exec_stop(ExecContext* exec)
{
    exec->stopped = true;

    check_all_stopped(exec);
}

void exec_run(ExecContext* exec)
{
    debug_assert(!exec_get_thread_context());
    exec_set_thread_context(exec);

    exec->os_thread = std::this_thread::get_id();

    static constexpr usz max_epoll_events = 64;
    std::array<epoll_event, max_epoll_events> events;

    while (!exec->stopped) {

        // Check for new fd events

        i32 timeout = 0;
        if (exec->idle.listeners.empty()) {
            exec->stats.poll_waits++;
            timeout = -1;
        }
        auto[count, error] = unix_check<epoll_wait, EAGAIN, EINTR>(exec->epoll_fd.get(), events.data(), events.size(), timeout);
        if (error) {
            if (error == EAGAIN || error == EINTR) {
                if (exec->idle.listeners.empty()) continue;
            } else {
                // At this point, we can't assume that we'll receive any future FD events.
                // Since this includes all user input, the only safe thing to do is
                // immediately terminate to avoid locking out the user's system.
                debug_kill();
            }
        }

        // Flush fd events

        if (count > 0) {
            for (i32 i = 0; i < count; ++i) {
                auto fd = events[i].data.fd;
                auto l = exec->listeners[fd];
                if (!l) continue;

                exec->stats.events_handled++;

                auto event_bits = from_epoll_events(events[i].events);
                if (l->flags.contains(FdListenFlag::oneshot)) {
                    Ref listener = l;
                    fd_unlisten(exec, fd);
                    listener->handle(fd, event_bits);
                } else {
                    l->handle(fd, event_bits);
                }
            }
        }

        exec->idle();
    }
}

// -----------------------------------------------------------------------------

void fd_listen(
    ExecContext* exec,
    fd_t fd,
    FdListener* listener)
{
    auto events = listener->events;

    debug_assert(fd_is_valid(fd));

    debug_assert(events);
    debug_assert(!exec->listeners[fd]);

    exec->listeners[fd] = listener;

    unix_check<epoll_ctl>(exec->epoll_fd.get(), EPOLL_CTL_ADD, fd, ptr_to(epoll_event {
        .events = to_epoll_events(events),
        .data {
            .fd = fd,
        }
    }));
}

void fd_unlisten(ExecContext* exec, fd_t fd)
{
    debug_assert(fd_is_valid(fd));

    if (!exec->listeners[fd]) {
        log_warn("fd does not have registered listener");
    }

    auto res = unix_check<epoll_ctl>(exec->epoll_fd.get(), EPOLL_CTL_DEL, fd, nullptr);
    debug_assert(res.ok());

    exec->listeners[fd] = nullptr;
}
