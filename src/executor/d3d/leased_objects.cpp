#include "executor/d3d/leased_objects.hpp"

#include "core/trace/function_scope.hpp"
#include "executor/session_lease.hpp"

namespace av {

LeasedObjects::LeasedObjects(BlockSource &blocks) : charges_(blocks, Pool::paged), lease_(nullptr), drawn_(0u), owed_(0u)
{
    AV_SCOPE("LeasedObjects::LeasedObjects");
}

void LeasedObjects::draw_from(SessionLease *lease)
{
    AV_SCOPE("LeasedObjects::draw_from");
    settle();
    charges_.clear();
    lease_ = lease;
}

void LeasedObjects::charge(uint32_t id, uint64_t bytes)
{
    AV_SCOPE("LeasedObjects::charge");
    if (lease_ == nullptr || bytes == 0u) return;
    uint32_t slot = 0u;
    if (charges_.insert(id, bytes, slot) != Status::ok) return;
    drawn_ += lease_->of(LeaseRecord::Kind::device).draw(UINT64_MAX);
    owed_ += bytes;
}

void LeasedObjects::recharge(uint32_t id, uint64_t bytes)
{
    AV_SCOPE("LeasedObjects::recharge");
    if (lease_ == nullptr) return;
    uint64_t *const held = charges_.find(id);
    const uint64_t  before = held != nullptr ? *held : 0u;
    if (bytes > before) {
        drawn_ += lease_->of(LeaseRecord::Kind::device).draw(UINT64_MAX);
        owed_ += bytes - before;
    } else if (before > bytes) {
        lease_->of(LeaseRecord::Kind::device).restore(before - bytes);
    }
    if (held != nullptr) {
        *held = bytes;
        return;
    }
    uint32_t slot = 0u;
    if (bytes != 0u) static_cast<void>(charges_.insert(id, bytes, slot));
}

void LeasedObjects::settle()
{
    AV_SCOPE("LeasedObjects::settle");
    if (lease_ != nullptr && drawn_ > owed_) lease_->of(LeaseRecord::Kind::device).restore(drawn_ - owed_);
    drawn_ = 0u;
    owed_ = 0u;
}

void LeasedObjects::release(uint32_t id)
{
    AV_SCOPE("LeasedObjects::release");
    uint64_t bytes = 0u;
    if (lease_ != nullptr && charges_.take(id, bytes)) lease_->of(LeaseRecord::Kind::device).restore(bytes);
}

void LeasedObjects::clear()
{
    AV_SCOPE("LeasedObjects::clear");
    charges_.clear();
}

uint64_t LeasedObjects::charged(uint32_t id) const
{
    AV_SCOPE("LeasedObjects::charged");
    const uint64_t *const bytes = charges_.find(id);
    return bytes != nullptr ? *bytes : 0u;
}

uint32_t LeasedObjects::count() const
{
    AV_SCOPE("LeasedObjects::count");
    return charges_.count();
}

}
