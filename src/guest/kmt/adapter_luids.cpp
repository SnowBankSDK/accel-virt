#include "guest/kmt/adapter_luids.hpp"

#include "core/trace/function_scope.hpp"
#include "guest/kmt/kmt.hpp"
#include "proto/escape/umd_adapter_info.hpp"

namespace av {

AdapterLuids::AdapterLuids() : adapters_{}, luids_{}, count_(0u)
{
    AV_SCOPE("AdapterLuids::AdapterLuids");
}

void AdapterLuids::remember(uint32_t adapter, uint64_t luid)
{
    AV_SCOPE("AdapterLuids::remember");
    if (adapter == 0u || luid == 0u) return;
    for (uint32_t index = 0u; index < count_; index++) {
        if (adapters_[index] != adapter) continue;
        luids_[index] = luid;
        return;
    }
    if (count_ == capacity) return;
    adapters_[count_] = adapter;
    luids_[count_] = luid;
    count_++;
}

void AdapterLuids::forget(uint32_t adapter)
{
    AV_SCOPE("AdapterLuids::forget");
    for (uint32_t index = 0u; index < count_; index++) {
        if (adapters_[index] != adapter) continue;
        count_--;
        adapters_[index] = adapters_[count_];
        luids_[index] = luids_[count_];
        adapters_[count_] = 0u;
        luids_[count_] = 0u;
        return;
    }
}

Status AdapterLuids::luid_of(Kmt &kmt, uint32_t adapter, uint64_t &luid)
{
    AV_SCOPE("AdapterLuids::luid_of");
    luid = 0u;
    for (uint32_t index = 0u; index < count_; index++) {
        if (adapters_[index] != adapter) continue;
        luid = luids_[index];
        return Status::ok;
    }
    UmdAdapterInfo info;
    const Status   asked = kmt.query_adapter_features(adapter, info);
    if (asked != Status::ok) return asked;
    if (info.luid() == 0u) return Status::not_found;
    luid = info.luid();
    remember(adapter, luid);
    return Status::ok;
}

uint32_t AdapterLuids::count() const
{
    AV_SCOPE("AdapterLuids::count");
    return count_;
}

bool AdapterLuids::invariants_hold() const
{
    AV_SCOPE("AdapterLuids::invariants_hold");
    if (count_ > capacity) return false;
    for (uint32_t index = 0u; index < capacity; index++) {
        if ((index < count_) != (adapters_[index] != 0u)) return false;
        if ((index < count_) != (luids_[index] != 0u)) return false;
    }
    return true;
}

}
