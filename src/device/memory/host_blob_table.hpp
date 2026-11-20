#ifndef AV_HOST_BLOB_TABLE_HPP
#define AV_HOST_BLOB_TABLE_HPP

#include "core/support/growable_array.hpp"
#include "core/support/keyed_slots.hpp"
#include "core/support/object_pool.hpp"
#include "core/support/status.hpp"
#include "device/memory/host_blob.hpp"
#include "device/memory/host_blob_errand.hpp"
#include "device/memory/host_blob_owners.hpp"
#include "device/memory/hostmem_space.hpp"
#include "device/memory/guest_ram_lendings.hpp"
#include "proto/resources/host_blob_id.hpp"
#include "proto/resources/host_memory_request.hpp"

#include <stddef.h>
#include <stdint.h>

namespace av {

class ContextPlane;
class ControlRequest;
class HeldRequests;
class Host;
class ServiceThread;
class StateWriter;
class WireRecorder;
struct HostBlobTableProbe;

class HostBlobTable final : public HostBlobOwners {
public:
    explicit HostBlobTable(Host &host);
    ~HostBlobTable();
    HostBlobTable(const HostBlobTable &) = delete;
    HostBlobTable &operator=(const HostBlobTable &) = delete;

    [[nodiscard]] Status initialize(ContextPlane &contexts, ServiceThread &service, HeldRequests &held,
                                    WireRecorder &recorder);
    [[nodiscard]] Status create(uint32_t id, uint32_t context, HostMemoryRequest request, uint64_t size,
                                uint32_t ticket);
    [[nodiscard]] Status create_over_guest(uint32_t id, uint32_t context, HostMemoryRequest request, uint64_t size);
    bool                 waits_for_backing(uint32_t id) const;
    [[nodiscard]] Status begin_adopting(uint32_t id, ControlRequest &request, size_t offset, uint32_t entries,
                                        uint32_t ticket);
    [[nodiscard]] Status adopt(uint32_t id, ControlRequest &request, size_t offset, uint32_t entries);
    void                 errand_finished(HostBlobErrand &errand, bool ran);
    void                 share_lendings(GuestRamLendings &lendings);
    [[nodiscard]] Status map(uint32_t id, uint64_t offset, uint32_t &caching);
    [[nodiscard]] Status unmap(uint32_t id);
    [[nodiscard]] Status release(uint32_t id);
    void                 release_all();
    bool                 contains(uint32_t id) const;
    bool                 owns(uint32_t context, uint32_t id) override;
    uint32_t             live() const;
    uint32_t             pending() const;

    void report(StateWriter &writer) const;
    bool invariants_hold() const;

private:
    struct Placed {
        uint64_t offset_;
        uint64_t size_;
    };

    HostBlob       *blob_of(uint32_t id);
    const HostBlob *blob_of(uint32_t id) const;
    uint32_t        placed_from(uint64_t offset) const;
    bool            overlaps_placed(uint64_t offset, uint64_t size) const;
    void            unplace(uint64_t offset);
    void            drop(uint32_t id);
    void            destroy(uint32_t id);
    void            forget(uint32_t id);
    HostBlob       *awaited(const HostBlobErrand &errand);
    void            settle_create(HostBlobErrand &errand, bool ran);
    void            settle_lend(HostBlobErrand &errand, bool ran);
    void            settle_alias(HostBlobErrand &errand, bool ran);
    Status          lay_kept(const HostBlobErrand &errand, GuestRamLending &lending);
    Status          verify_kept(const HostBlobErrand &errand, HostBlob &blob, GuestRamLending &lending);
    void            conclude(const HostBlobErrand &errand, HostBlob *blob, Status status);
    uint32_t        next_serial();

    Host                            &host_;
    ContextPlane                    *contexts_;
    ServiceThread                   *service_;
    HeldRequests                    *held_;
    WireRecorder                    *recorder_;
    HostmemSpace                     space_;
    GuestRamLendings                 own_lendings_;
    GuestRamLendings                *lendings_;
    void                            *lock_;
    ObjectPool<HostBlob>             blobs_;
    ObjectPool<HostBlobErrand>       errands_;
    uint32_t                         serial_;
    KeyedSlots<uint32_t, HostBlob *> ids_;
    GrowableArray<Placed>            placed_;

    friend struct HostBlobTableProbe;
};

}

#endif
