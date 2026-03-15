#include "../internal.hpp"

struct io::Session
{
};

void io::session::init(io::Context*)
{
    log_error("IO - session backend not implemented");
}
