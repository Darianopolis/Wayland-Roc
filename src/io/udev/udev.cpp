#include "../internal.hpp"

struct io::Udev
{
};

void io::udev::init(io::Context*)
{
    log_error("IO - udev backend not implemented");
}
