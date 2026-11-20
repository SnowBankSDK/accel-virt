#ifndef AV_ADAPTER_LUIDS_HPP
#define AV_ADAPTER_LUIDS_HPP

#include "core/support/status.hpp"
#include "proto/context/av_device_count.hpp"

#include <stdint.h>

namespace av {

class Kmt;

class AdapterLuids {
public:
    static constexpr uint32_t capacity = 2u * most_av_devices;

    AdapterLuids();

    void                 remember(uint32_t adapter, uint64_t luid);
    void                 forget(uint32_t adapter);
    [[nodiscard]] Status luid_of(Kmt &kmt, uint32_t adapter, uint64_t &luid);
    uint32_t             count() const;

    bool invariants_hold() const;

private:
    uint32_t adapters_[capacity];
    uint64_t luids_[capacity];
    uint32_t count_;
};

}

#endif
