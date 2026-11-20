#include "harness/executor/lease_rig.hpp"

#include "executor/byte_ledger.hpp"

namespace av {

LeaseRig::LeaseRig(ByteLedger *vram, ByteLedger *host, std::uint64_t room, const std::uint64_t *initial)
    : vram_(vram), lease_(), transport_(lease_), guest_(), filler_()
{
    filler_.bind(vram);
    if (vram != nullptr && vram->cap() != UINT64_MAX) {
        const std::uint64_t free = vram->free_bytes();
        if (free > room) static_cast<void>(filler_.take_session(free - room));
    }
    lease_.bind(vram, host);
    lease_.open(initial);
    guest_.reset(initial);
}

LeaseRig::~LeaseRig()
{
    static_cast<void>(lease_.close());
    static_cast<void>(filler_.close());
}

SessionLease &LeaseRig::host() { return lease_; }
MemoryLease  &LeaseRig::guest() { return guest_; }

bool LeaseRig::admit(std::uint64_t bytes) { return guest_.take(transport_, LeaseRecord::Kind::device, bytes); }

void LeaseRig::credit(std::uint64_t bytes) { guest_.give(transport_, LeaseRecord::Kind::device, bytes); }

std::uint64_t LeaseRig::in_use() const
{
    if (vram_ == nullptr) return 0u;
    const std::uint64_t used = vram_->used();
    const std::uint64_t held = lease_.idle(LeaseRecord::Kind::device) + filler_.held();
    return used > held ? used - held : 0u;
}

std::uint64_t LeaseRig::room() const { return vram_ != nullptr ? vram_->free_bytes() : 0u; }

}
