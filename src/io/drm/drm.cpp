#include "../internal.hpp"

struct io::Drm
{
};

void io::drm::init(io::Context*)
{
    log_error("IO - drm backend not implemented");
}
