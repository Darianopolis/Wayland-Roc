#pragma once

#include "debug.hpp"
#include "object.hpp"
#include "enum.hpp"
#include "fd.hpp"
#include "signal.hpp"

// -----------------------------------------------------------------------------

struct FdListener;

struct ExecContext
{
    bool stopped = false;

    std::array<Ref<FdListener>, fd_limit> listeners  = {};

    std::thread::id os_thread;

    Signal<void()> idle;

    Fd epoll_fd;

    struct {
        u64 events_handled;
        u64 poll_waits;
    } stats;

    ~ExecContext();
};

auto exec_create() -> Ref<ExecContext>;

void exec_set_thread_context(ExecContext*);
auto exec_get_thread_context() -> ExecContext*;

void exec_run( ExecContext*);
void exec_stop(ExecContext*);

// -----------------------------------------------------------------------------

enum class FdEventBit : u32
{
    readable = 1 << 0,
    writable = 1 << 1,
};

enum class FdListenFlag : u32
{
    oneshot = 1 << 0,
};

struct FdListener
{
    Flags<FdEventBit> events;
    Flags<FdListenFlag> flags;

    virtual void handle(fd_t fd, Flags<FdEventBit> events) = 0;
};

// -----------------------------------------------------------------------------

void fd_listen(  ExecContext*, fd_t fd, FdListener*);
void fd_unlisten(ExecContext*, fd_t fd);

template<typename Fn>
void fd_listen(
    ExecContext* exec,
    fd_t fd,
    Flags<FdEventBit> events,
    Fn&& callback,
    Flags<FdListenFlag> flags = {})
{
    struct Listener : FdListener
    {
        Fn lambda;
        Listener(Fn&& lambda): lambda(std::move(lambda)) {}
        virtual void handle(fd_t fd, Flags<FdEventBit> events) { lambda(fd, events); }
    };

    auto listener = ref_create<Listener>(std::move(callback));
    listener->events = events;
    listener->flags = flags;
    fd_listen(exec, fd, listener.get());
}

