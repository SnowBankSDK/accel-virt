#ifndef AV_EGL_ENTRY_GATE_HPP
#define AV_EGL_ENTRY_GATE_HPP

#include <pthread.h>
#include <stdint.h>

namespace av {

struct EglEntryGateProbe;

class EglEntryGate {
public:
    static constexpr uint32_t threads = 512u;

    EglEntryGate();
    ~EglEntryGate();
    EglEntryGate(const EglEntryGate &) = delete;
    EglEntryGate &operator=(const EglEntryGate &) = delete;

    static EglEntryGate &process();

    void               need();
    bool               needed() const;
    void               enter();
    void               leave();
    [[nodiscard]] bool exclude(uint64_t wait_ns);
    void               admit();
    void              *retire();
    void               joined(void *retired);
    uint32_t           inside() const;
    uint64_t           exclusions() const;
    uint64_t           held_back() const;

private:
    struct alignas(64) Slot {
        uint32_t inside_;
        uint32_t claimed_;
    };

    Slot *slot();
    void  wait_out();

    Slot            slots_[threads];
    Slot            overflow_;
    pthread_mutex_t excluding_;
    pthread_t       excluder_;
    uint32_t        pending_;
    uint32_t        nested_;
    bool            needed_;
    uint64_t        exclusions_;
    uint64_t        held_back_;

    friend struct EglEntryGateProbe;
    friend struct EglEntrySlotHold;
};

class EglEntry {
public:
    EglEntry();
    explicit EglEntry(EglEntryGate &gate);
    ~EglEntry();
    EglEntry(const EglEntry &) = delete;
    EglEntry &operator=(const EglEntry &) = delete;

private:
    EglEntryGate *gate_;
};

}

#endif
