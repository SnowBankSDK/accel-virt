#ifndef AV_EXECUTION_NODE_HPP
#define AV_EXECUTION_NODE_HPP

#include "core/platform/one_shot_timer.hpp"
#include "core/platform/platform.hpp"
#include "core/platform/spin_lock.hpp"
#include "core/support/status.hpp"

#include <stdint.h>

namespace av {

class StateWriter;

class InterruptNotification;
struct ExecutionNodeProbe;

class ExecutionNode {
public:
    static constexpr uint32_t capacity = 64u;

    enum class Hold : uint8_t { none, parks, resolves };

    explicit ExecutionNode(Platform &platform, TimerRoutine deadline_passed = nullptr, void *context = nullptr);
    ExecutionNode(const ExecutionNode &) = delete;
    ExecutionNode &operator=(const ExecutionNode &) = delete;

    [[nodiscard]] Status submit(uint32_t fence);
    [[nodiscard]] Status submit(uint32_t fence, void *owner, uint64_t awaited, Hold hold = Hold::none);
    void                 finish(const void *owner, uint64_t completed);
    void                 unpark(const void *owner);
    uint32_t             parked();
    static constexpr uint64_t park_deadline = 12000000u;
    uint32_t             release_overdue(uint64_t now, void **owners = nullptr, uint32_t room = 0u,
                                         uint32_t *named = nullptr);
    [[nodiscard]] Status preempt(uint32_t fence);
    [[nodiscard]] Status complete(InterruptNotification &notification, uint32_t *returned = nullptr);
    uint32_t             current_fence();
    uint32_t             pending();
    uint32_t             last_submitted();
    uint64_t             progress_time();
    void                 reset();
    void                 restart();
    void                 quiesce();
    uint32_t             abort_pending();

    void report(StateWriter &writer);

    bool invariants_hold() const;

private:
    [[nodiscard]] Status queue(uint32_t fence, void *owner, uint64_t awaited, Hold hold);
    void                 clear_parked(const void *owner);
    void                 drop_parks();
    void                 watch_parks(uint64_t now);
    bool take(uint32_t &fence);
    bool take_preemption(uint32_t &fence, uint32_t &last_completed, uint32_t &returned);

    Platform &platform_;
    SpinLock  lock_;
    uint32_t  pending_[capacity];
    uint32_t  head_;
    uint32_t  count_;
    uint32_t  last_submitted_;
    uint32_t  last_completed_;
    uint32_t  preemption_fence_;
    bool      preemption_requested_;
    bool      stopped_;
    uint64_t  progress_time_;
    void     *owners_[capacity];
    uint64_t  awaited_[capacity];
    uint64_t  queued_at_[capacity];
    bool      parked_[capacity];
    uint32_t  parked_count_;
    uint64_t  alarm_due_;
    bool      alarmed_;
    bool      alarms_;
    uint64_t  preempted_;
    OneShotTimer park_alarm_;

    friend struct ExecutionNodeProbe;
};

}

#endif
