#include "executor/egl_entry_gate.hpp"

#include "core/trace/function_scope.hpp"

#include <sched.h>
#include <time.h>

namespace av {

struct EglEntrySlotHold {
    EglEntryGate       *gate_ = nullptr;
    EglEntryGate::Slot *slot_ = nullptr;

    ~EglEntrySlotHold()
    {
        if (slot_ != nullptr && slot_ != &gate_->overflow_) __atomic_store_n(&slot_->claimed_, 0u, __ATOMIC_RELEASE);
    }
};

namespace {

thread_local EglEntrySlotHold thread_slot;

uint64_t monotonic_ns()
{
    timespec now = {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<uint64_t>(now.tv_sec) * 1000000000ull + static_cast<uint64_t>(now.tv_nsec);
}

}

EglEntryGate::EglEntryGate()
    : slots_{}, overflow_{}, excluding_(), excluder_(), pending_(0u), nested_(0u), needed_(false), exclusions_(0u), held_back_(0u)
{
    AV_SCOPE("EglEntryGate::EglEntryGate");
    pthread_mutex_init(&excluding_, nullptr);
}

EglEntryGate::~EglEntryGate()
{
    AV_SCOPE("EglEntryGate::~EglEntryGate");
    pthread_mutex_destroy(&excluding_);
}

EglEntryGate &EglEntryGate::process()
{
    AV_SCOPE("EglEntryGate::process");
    static EglEntryGate gate;
    return gate;
}

void EglEntryGate::need()
{
    AV_SCOPE("EglEntryGate::need");
    __atomic_store_n(&needed_, true, __ATOMIC_RELEASE);
}

bool EglEntryGate::needed() const
{
    AV_SCOPE("EglEntryGate::needed");
    return __atomic_load_n(&needed_, __ATOMIC_RELAXED);
}

EglEntryGate::Slot *EglEntryGate::slot()
{
    AV_SCOPE("EglEntryGate::slot");
    if (thread_slot.gate_ == this) return thread_slot.slot_;
    if (thread_slot.slot_ != nullptr) return &overflow_;
    for (Slot &candidate : slots_) {
        uint32_t free = 0u;
        if (!__atomic_compare_exchange_n(&candidate.claimed_, &free, 1u, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
            continue;
        thread_slot.gate_ = this;
        thread_slot.slot_ = &candidate;
        return &candidate;
    }
    return &overflow_;
}

void EglEntryGate::enter()
{
    if (!__atomic_load_n(&needed_, __ATOMIC_RELAXED)) return;
    Slot *const mine = slot();
    for (;;) {
        const uint32_t depth = __atomic_add_fetch(&mine->inside_, 1u, __ATOMIC_SEQ_CST);
        if (__atomic_load_n(&pending_, __ATOMIC_SEQ_CST) == 0u) return;
        if (depth > 1u && mine != &overflow_) return;
        if (pthread_equal(__atomic_load_n(&excluder_, __ATOMIC_RELAXED), pthread_self()) != 0) return;
        __atomic_sub_fetch(&mine->inside_, 1u, __ATOMIC_SEQ_CST);
        wait_out();
    }
}

void EglEntryGate::leave()
{
    if (!__atomic_load_n(&needed_, __ATOMIC_RELAXED)) return;
    if (thread_slot.gate_ != this) {
        __atomic_sub_fetch(&overflow_.inside_, 1u, __ATOMIC_RELEASE);
        return;
    }
    Slot *const mine = thread_slot.slot_;
    __atomic_store_n(&mine->inside_, __atomic_load_n(&mine->inside_, __ATOMIC_RELAXED) - 1u, __ATOMIC_RELEASE);
}

void EglEntryGate::wait_out()
{
    AV_SCOPE("EglEntryGate::wait_out");
    __atomic_add_fetch(&held_back_, 1u, __ATOMIC_RELAXED);
    pthread_mutex_lock(&excluding_);
    pthread_mutex_unlock(&excluding_);
}

bool EglEntryGate::exclude(uint64_t wait_ns)
{
    AV_SCOPE("EglEntryGate::exclude");
    if (!needed()) return true;
    if (__atomic_load_n(&pending_, __ATOMIC_SEQ_CST) != 0u &&
        pthread_equal(__atomic_load_n(&excluder_, __ATOMIC_RELAXED), pthread_self()) != 0) {
        nested_++;
        return true;
    }
    const uint64_t until = monotonic_ns() + wait_ns;
    pthread_mutex_lock(&excluding_);
    __atomic_store_n(&excluder_, pthread_self(), __ATOMIC_RELAXED);
    __atomic_store_n(&pending_, 1u, __ATOMIC_SEQ_CST);
    const Slot *const own = thread_slot.gate_ == this ? thread_slot.slot_ : nullptr;
    for (;;) {
        uint32_t busy = own == nullptr ? __atomic_load_n(&overflow_.inside_, __ATOMIC_SEQ_CST) : 0u;
        for (const Slot &other : slots_)
            if (&other != own) busy += __atomic_load_n(&other.inside_, __ATOMIC_SEQ_CST);
        if (busy == 0u) break;
        if (monotonic_ns() >= until) {
            __atomic_store_n(&pending_, 0u, __ATOMIC_SEQ_CST);
            __atomic_store_n(&excluder_, pthread_t{}, __ATOMIC_RELAXED);
            pthread_mutex_unlock(&excluding_);
            return false;
        }
        sched_yield();
    }
    __atomic_add_fetch(&exclusions_, 1u, __ATOMIC_RELAXED);
    return true;
}

void EglEntryGate::admit()
{
    AV_SCOPE("EglEntryGate::admit");
    if (!needed()) return;
    if (nested_ != 0u) {
        nested_--;
        return;
    }
    __atomic_store_n(&pending_, 0u, __ATOMIC_SEQ_CST);
    __atomic_store_n(&excluder_, pthread_t{}, __ATOMIC_RELAXED);
    pthread_mutex_unlock(&excluding_);
}

uint32_t EglEntryGate::inside() const
{
    AV_SCOPE("EglEntryGate::inside");
    uint32_t busy = __atomic_load_n(&overflow_.inside_, __ATOMIC_SEQ_CST);
    for (const Slot &other : slots_) busy += __atomic_load_n(&other.inside_, __ATOMIC_SEQ_CST);
    return busy;
}

uint64_t EglEntryGate::exclusions() const
{
    AV_SCOPE("EglEntryGate::exclusions");
    return __atomic_load_n(&exclusions_, __ATOMIC_RELAXED);
}

uint64_t EglEntryGate::held_back() const
{
    AV_SCOPE("EglEntryGate::held_back");
    return __atomic_load_n(&held_back_, __ATOMIC_RELAXED);
}

EglEntry::EglEntry() : gate_(&EglEntryGate::process())
{
    gate_->enter();
}

EglEntry::EglEntry(EglEntryGate &gate) : gate_(&gate)
{
    gate_->enter();
}

EglEntry::~EglEntry()
{
    gate_->leave();
}

}
