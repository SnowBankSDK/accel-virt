#ifndef AV_ADAPTER_SLOTS_HPP
#define AV_ADAPTER_SLOTS_HPP

#include "proto/context/av_device_count.hpp"

#include <stdint.h>

namespace av {

class AdapterSlots {
public:
    static constexpr uint32_t capacity = most_av_devices;
    static constexpr uint32_t none = capacity;

    AdapterSlots();

    uint32_t claim(uint64_t luid, bool &fresh);
    void     hold(uint32_t slot);
    bool     release(uint32_t slot);
    void     vacate(uint32_t slot);
    uint32_t holds(uint32_t slot) const;
    uint64_t luid(uint32_t slot) const;
    uint32_t used() const;

    bool invariants_hold() const;

private:
    uint64_t luids_[capacity];
    uint32_t holds_[capacity];
};

}

#endif
