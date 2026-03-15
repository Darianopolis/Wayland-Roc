#include "../internal.hpp"

struct io::Libinput
{
};

void io::libinput::init(io::Context*)
{
    log_error("IO - libinput backend not implemented");
}
