#ifndef AV_CONTEXT_REFUSALS_HPP
#define AV_CONTEXT_REFUSALS_HPP

#include "core/log/log_cadence.hpp"

#include <stdint.h>

namespace av {

class DriverLog;

class ContextRefusals {
public:
    enum class Reason : uint32_t {
        domain,
        options,
        share_api,
        sub_api_mask,
        no_backend,
        share_gone,
        bind,
        d3d11_device,
        count
    };

    explicit ContextRefusals(DriverLog &log);
    ContextRefusals(const ContextRefusals &) = delete;
    ContextRefusals &operator=(const ContextRefusals &) = delete;

    void     refuse(Reason reason, const char *api, uint64_t detail);
    uint64_t refused(Reason reason) const;

private:
    const char *describe(Reason reason) const;

    DriverLog &log_;
    LogCadence seen_[static_cast<uint32_t>(Reason::count)];
};

}

#endif
