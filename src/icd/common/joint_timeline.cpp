#include "icd/common/joint_timeline.hpp"

#include "core/platform/thread_lock.hpp"
#include "core/platform/thread_lock_hold.hpp"
#include "core/trace/function_scope.hpp"
#include "guest/kmt/kmt.hpp"
#include "icd/common/lane_progress.hpp"

namespace av::icd {

JointTimeline::JointTimeline(GpuTimeline &session, ThreadLock &lock, Kmt &kmt)
    : session_(session), lock_(lock), kmt_(kmt), lanes_{}, generations_{}, joined_(0u), marks_{}, stamped_(0u), reached_(0u)
{
    AV_SCOPE("JointTimeline::JointTimeline");
}

void JointTimeline::join(LaneProgress &lane)
{
    AV_SCOPE("JointTimeline::join");
    const ThreadLockHold held(lock_);
    for (uint32_t index = 0u; index < most_lanes; index++) {
        if (lanes_[index] != nullptr) continue;
        lanes_[index] = &lane;
        generations_[index]++;
        joined_++;
        return;
    }
}

void JointTimeline::leave(const LaneProgress &lane)
{
    AV_SCOPE("JointTimeline::leave");
    const ThreadLockHold held(lock_);
    for (uint32_t index = 0u; index < most_lanes; index++) {
        if (lanes_[index] != &lane) continue;
        lanes_[index] = nullptr;
        generations_[index]++;
        joined_--;
        return;
    }
}

uint64_t JointTimeline::mark(uint64_t session) const
{
    AV_SCOPE("JointTimeline::mark");
    const ThreadLockHold held(lock_);
    Mark made = {};
    made.session_ = session;
    for (uint32_t index = 0u; index < most_lanes; index++) {
        made.generations_[index] = generations_[index];
        made.lanes_[index] = lanes_[index] != nullptr ? lanes_[index]->covering() : 0u;
    }
    advance();
    if (reached(made)) return reached_;
    Mark &last = marks_[stamped_ % marks];
    bool  same = stamped_ != reached_ && last.session_ == made.session_;
    for (uint32_t index = 0u; same && index < most_lanes; index++)
        same = last.lanes_[index] == made.lanes_[index] && last.generations_[index] == made.generations_[index];
    if (same) return stamped_;
    if (stamped_ - reached_ == marks) {
        if (made.session_ > last.session_) last.session_ = made.session_;
        for (uint32_t index = 0u; index < most_lanes; index++) {
            if (last.generations_[index] != made.generations_[index] || made.lanes_[index] > last.lanes_[index])
                last.lanes_[index] = made.lanes_[index];
            last.generations_[index] = made.generations_[index];
        }
        return stamped_;
    }
    stamped_++;
    marks_[stamped_ % marks] = made;
    return stamped_;
}

uint64_t JointTimeline::recording() const
{
    AV_SCOPE("JointTimeline::recording");
    return mark(session_.recording());
}

uint64_t JointTimeline::covering() const
{
    AV_SCOPE("JointTimeline::covering");
    return mark(session_.covering());
}

bool JointTimeline::reached(const Mark &mark) const
{
    AV_SCOPE("JointTimeline::reached");
    if (session_.completed() < mark.session_) return false;
    for (uint32_t index = 0u; index < most_lanes; index++)
        if (mark.lanes_[index] != 0u && generations_[index] == mark.generations_[index] && lanes_[index] != nullptr &&
            !lanes_[index]->ran(mark.lanes_[index]))
            return false;
    return true;
}

void JointTimeline::advance() const
{
    AV_SCOPE("JointTimeline::advance");
    while (reached_ < stamped_ && reached(marks_[(reached_ + 1u) % marks])) reached_++;
}

uint64_t JointTimeline::completed() const
{
    AV_SCOPE("JointTimeline::completed");
    const ThreadLockHold held(lock_);
    advance();
    return reached_;
}

bool JointTimeline::wait(uint64_t mark)
{
    AV_SCOPE("JointTimeline::wait");
    uint64_t session = 0u;
    {
        const ThreadLockHold held(lock_);
        advance();
        if (mark <= reached_) return true;
        if (mark > stamped_) return false;
        session = marks_[mark % marks].session_;
    }
    if (!session_.wait(session)) return false;
    uint64_t       frequency = 0u;
    const uint64_t began = kmt_.performance_counter(frequency);
    for (;;) {
        {
            const ThreadLockHold held(lock_);
            advance();
            if (mark <= reached_) return true;
        }
        if (kmt_.performance_counter(frequency) - began > frequency * patience_seconds) return false;
        kmt_.sleep_briefly();
    }
}

}
