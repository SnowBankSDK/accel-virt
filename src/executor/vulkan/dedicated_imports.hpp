#ifndef AV_DEDICATED_IMPORTS_HPP
#define AV_DEDICATED_IMPORTS_HPP

#include "core/support/keyed_slots.hpp"
#include "core/support/object_pool.hpp"
#include "core/support/status.hpp"
#include "executor/owned_fd.hpp"

#include <stdint.h>

namespace av {

class BlockSource;

class DedicatedImports {
public:
    explicit DedicatedImports(BlockSource &blocks);
    DedicatedImports(const DedicatedImports &) = delete;
    DedicatedImports &operator=(const DedicatedImports &) = delete;

    [[nodiscard]] Status keep_payload(uint64_t memory, int fd, uint64_t bytes);
    int                  payload(uint64_t memory) const;
    uint64_t             payload_bytes(uint64_t memory) const;
    void                 drop_payload(uint64_t memory);
    [[nodiscard]] Status dedicate(uint64_t image, uint64_t memory);
    uint64_t             take_dedicated(uint64_t image);
    uint32_t             payloads() const;
    uint32_t             dedicated() const;

    bool invariants_hold() const;

private:
    struct Payload {
        Payload(uint64_t memory, uint64_t bytes);
        uint64_t memory_;
        uint64_t bytes_;
        OwnedFd  fd_;
    };

    const Payload *find(uint64_t memory) const;

    ObjectPool<Payload>            payloads_;
    KeyedSlots<uint64_t, uint64_t> dedicated_;
};

}

#endif
