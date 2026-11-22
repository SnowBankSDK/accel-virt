#ifndef AV_HOST_BLOB_ERRAND_HPP
#define AV_HOST_BLOB_ERRAND_HPP

#include "core/support/status.hpp"
#include "device/abi/av_executor.h"
#include "device/threading/errand.hpp"
#include "proto/resources/host_memory_request.hpp"

#include <stddef.h>
#include <stdint.h>

namespace av {

class ContextExecutor;
class ContextPlane;
class HostBlobTable;

class HostBlobErrand final : public Errand {
public:
    enum class Kind : uint8_t { create, lend, alias };

    static constexpr uint32_t most_holders = 4u;

    HostBlobErrand(HostBlobTable &table, ContextPlane &plane, Kind kind, uint32_t id, uint32_t serial);
    ~HostBlobErrand() = default;

    void ask(uint32_t context, HostMemoryRequest request, uint64_t size, uint32_t ticket);
    void lay(size_t offset, uint32_t entries);
    bool alias_from(ContextPlane &holder, uint64_t key);
    void place_through(ContextExecutor &executor);
    void answer(Status status, const AvHostMemory &memory);

    Kind                kind() const;
    uint32_t            id() const;
    uint32_t            serial() const;
    uint32_t            context() const;
    HostMemoryRequest   request() const;
    uint64_t            size() const;
    uint32_t            ticket() const;
    size_t              offset() const;
    uint32_t            entries() const;
    bool                places() const;
    Status              status() const;
    const AvHostMemory &memory() const;

protected:
    void run() override;
    void finish(bool ran) override;

private:
    struct Holder {
        ContextPlane *plane_;
        uint64_t      key_;
    };

    void run_alias();

    HostBlobTable    &table_;
    ContextPlane     &plane_;
    ContextExecutor  *placer_;
    Kind              kind_;
    uint32_t          id_;
    uint32_t          serial_;
    uint32_t          context_;
    HostMemoryRequest request_;
    uint64_t          size_;
    uint32_t          ticket_;
    size_t            offset_;
    uint32_t          entries_;
    Holder            holders_[most_holders];
    uint32_t          holder_count_;
    Status            status_;
    AvHostMemory      memory_;
};

}

#endif
