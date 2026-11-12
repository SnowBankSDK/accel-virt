#include "executor/host_ram_ledger.hpp"

#include "core/trace/function_scope.hpp"
#include "executor/host_memory_gauge.hpp"

namespace av {

HostRamLedger::HostRamLedger()
    : budget_(0u), gauge_(nullptr), floor_(0u), stamp_(never), available_(unread), seen_(never), credit_(0u),
      unseen_(0u), relieved_(never), tight_refusals_(0u)
{
    AV_SCOPE("HostRamLedger::HostRamLedger");
}

void HostRamLedger::budget(uint64_t bytes)
{
    AV_SCOPE("HostRamLedger::budget");
    budget_ = bytes;
    set_cap(bytes == 0u ? UINT64_MAX : bytes);
}

uint64_t HostRamLedger::budget() const
{
    AV_SCOPE("HostRamLedger::budget");
    return budget_;
}

void HostRamLedger::watch(HostMemoryGauge *gauge)
{
    AV_SCOPE("HostRamLedger::watch");
    __atomic_store_n(&gauge_, gauge, __ATOMIC_RELEASE);
}

void HostRamLedger::floor(uint64_t bytes)
{
    AV_SCOPE("HostRamLedger::floor");
    __atomic_store_n(&stamp_, never, __ATOMIC_RELAXED);
    __atomic_store_n(&floor_, bytes, __ATOMIC_RELEASE);
}

uint64_t HostRamLedger::floor() const
{
    AV_SCOPE("HostRamLedger::floor");
    return __atomic_load_n(&floor_, __ATOMIC_ACQUIRE);
}

uint64_t HostRamLedger::available() const
{
    AV_SCOPE("HostRamLedger::available");
    const uint64_t left = __atomic_load_n(&available_, __ATOMIC_RELAXED);
    return left == unread ? never : left;
}

uint64_t HostRamLedger::credit() const
{
    AV_SCOPE("HostRamLedger::credit");
    return __atomic_load_n(&credit_, __ATOMIC_RELAXED);
}

uint64_t HostRamLedger::tight_refusals() const
{
    AV_SCOPE("HostRamLedger::tight_refusals");
    return __atomic_load_n(&tight_refusals_, __ATOMIC_RELAXED);
}

uint64_t HostRamLedger::admissible() const
{
    AV_SCOPE("HostRamLedger::admissible");
    const uint64_t room = free_bytes();
    const uint64_t lowest = __atomic_load_n(&floor_, __ATOMIC_ACQUIRE);
    const uint64_t left = __atomic_load_n(&available_, __ATOMIC_RELAXED);
    if (__atomic_load_n(&gauge_, __ATOMIC_ACQUIRE) == nullptr || lowest == 0u || left >= unread) return room;
    const uint64_t above = left > lowest ? left - lowest : 0u;
    return above < room ? above : room;
}

bool HostRamLedger::take(uint64_t bytes)
{
    AV_SCOPE("HostRamLedger::take");
    if (!admit(bytes)) {
        __atomic_fetch_add(&tight_refusals_, 1u, __ATOMIC_RELAXED);
        refuse();
        relieve();
        return false;
    }
    if (ByteLedger::take(bytes)) return true;
    restore(bytes);
    return false;
}

bool HostRamLedger::take_session(uint64_t bytes)
{
    AV_SCOPE("HostRamLedger::take_session");
    return ByteLedger::take(bytes);
}

void HostRamLedger::give(uint64_t bytes)
{
    AV_SCOPE("HostRamLedger::give");
    const uint64_t held = used();
    const uint64_t returned = bytes < held ? bytes : held;
    ByteLedger::give(bytes);
    if (returned == 0u || __atomic_load_n(&available_, __ATOMIC_RELAXED) >= unread) return;
    __atomic_fetch_add(&credit_, returned, __ATOMIC_RELAXED);
    restore(returned);
}

bool HostRamLedger::admit(uint64_t bytes)
{
    AV_SCOPE("HostRamLedger::admit");
    HostMemoryGauge *const gauge = __atomic_load_n(&gauge_, __ATOMIC_ACQUIRE);
    const uint64_t         lowest = __atomic_load_n(&floor_, __ATOMIC_ACQUIRE);
    if (gauge == nullptr || lowest == 0u) return true;
    const uint64_t now = gauge->now_ms();
    uint64_t       left = __atomic_load_n(&available_, __ATOMIC_ACQUIRE);
    if (left == unread) {
        read_first(now);
        left = __atomic_load_n(&available_, __ATOMIC_ACQUIRE);
    } else {
        uint64_t stamp = __atomic_load_n(&stamp_, __ATOMIC_ACQUIRE);
        if ((stamp == never || now - stamp >= refresh_ms) &&
            __atomic_compare_exchange_n(&stamp_, &stamp, now, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
            refresh();
            left = __atomic_load_n(&available_, __ATOMIC_ACQUIRE);
        }
    }
    for (;;) {
        if (left == never) return true;
        if (left < bytes || left - bytes < lowest) return false;
        if (__atomic_compare_exchange_n(&available_, &left, left - bytes, true, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            __atomic_fetch_add(&unseen_, bytes - spend(bytes), __ATOMIC_RELAXED);
            return true;
        }
    }
}

void HostRamLedger::read_first(uint64_t now)
{
    AV_SCOPE("HostRamLedger::read_first");
    HostMemoryGauge *const gauge = __atomic_load_n(&gauge_, __ATOMIC_ACQUIRE);
    uint64_t               stamp = never;
    __atomic_compare_exchange_n(&stamp_, &stamp, now, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
    uint64_t found = 0u;
    uint64_t first = never;
    if (gauge->available(found)) {
        __atomic_store_n(&seen_, found, __ATOMIC_RELAXED);
        const uint64_t owed = __atomic_load_n(&credit_, __ATOMIC_RELAXED);
        first = owed > largest - found ? largest : found + owed;
    }
    uint64_t expected = unread;
    __atomic_compare_exchange_n(&available_, &expected, first, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

void HostRamLedger::refresh()
{
    AV_SCOPE("HostRamLedger::refresh");
    HostMemoryGauge *const gauge = __atomic_load_n(&gauge_, __ATOMIC_ACQUIRE);
    const uint64_t         before = __atomic_load_n(&available_, __ATOMIC_ACQUIRE);
    uint64_t               found = 0u;
    if (!gauge->available(found)) return;
    const uint64_t last = __atomic_exchange_n(&seen_, found, __ATOMIC_RELAXED);
    if (last != never && found > last) static_cast<void>(spend(found - last));
    const uint64_t pending = __atomic_load_n(&unseen_, __ATOMIC_RELAXED);
    const uint64_t dropped = last != never && found < last ? last - found : 0u;
    const uint64_t shown = dropped < pending ? dropped : pending;
    if (shown != 0u) __atomic_fetch_sub(&unseen_, shown, __ATOMIC_RELAXED);
    const uint64_t owed = __atomic_load_n(&credit_, __ATOMIC_RELAXED);
    const uint64_t held = owed > largest - found ? largest : found + owed;
    const uint64_t read = pending - shown > held ? 0u : held - (pending - shown);
    uint64_t       left = before;
    for (;;) {
        const bool counted = left != never && before != never;
        uint64_t   next = read;
        if (counted && left < before) next = before - left > next ? 0u : next - (before - left);
        if (counted && left > before) next = left - before > largest - next ? largest : next + (left - before);
        if (__atomic_compare_exchange_n(&available_, &left, next, true, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
    }
}

void HostRamLedger::relieve()
{
    AV_SCOPE("HostRamLedger::relieve");
    HostMemoryGauge *const gauge = __atomic_load_n(&gauge_, __ATOMIC_ACQUIRE);
    if (gauge == nullptr) return;
    const uint64_t now = gauge->now_ms();
    uint64_t       last = __atomic_load_n(&relieved_, __ATOMIC_RELAXED);
    if (last != never && now - last < relieve_ms) return;
    if (!__atomic_compare_exchange_n(&relieved_, &last, now, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) return;
    gauge->relieve();
}

void HostRamLedger::restore(uint64_t bytes)
{
    AV_SCOPE("HostRamLedger::restore");
    uint64_t left = __atomic_load_n(&available_, __ATOMIC_RELAXED);
    while (left < unread) {
        const uint64_t back = bytes > largest - left ? largest : left + bytes;
        if (__atomic_compare_exchange_n(&available_, &left, back, true, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) return;
    }
}

uint64_t HostRamLedger::spend(uint64_t bytes)
{
    AV_SCOPE("HostRamLedger::spend");
    uint64_t owed = __atomic_load_n(&credit_, __ATOMIC_RELAXED);
    while (owed != 0u &&
           !__atomic_compare_exchange_n(&credit_, &owed, bytes < owed ? owed - bytes : 0u, true, __ATOMIC_ACQ_REL,
                                        __ATOMIC_RELAXED)) {
    }
    return bytes < owed ? bytes : owed;
}

}
