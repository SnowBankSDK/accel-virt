#ifndef AV_LEASE_COMMAND_HPP
#define AV_LEASE_COMMAND_HPP

#include "core/support/status.hpp"
#include "proto/submission/command_header.hpp"

#include <stddef.h>
#include <stdint.h>

namespace av {

class ContextExecutor;

class LeaseCommand {
public:
    LeaseCommand(ContextExecutor &executor, const CommandHeader &header, const uint8_t *body);

    [[nodiscard]] Status run(uint8_t *reply, size_t capacity, size_t &produced);

private:
    ContextExecutor &executor_;
    CommandHeader    header_;
    const uint8_t   *body_;
};

}

#endif
