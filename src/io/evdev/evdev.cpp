#include "../internal.hpp"

struct io::Evdev
{
};

void io::evdev::init(io::Context*)
{
    log_error("IO - evdev backend not implemented");
}
