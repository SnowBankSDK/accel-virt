#include "miniport/av_context/release_queue.hpp"

#include "core/trace/function_scope.hpp"

namespace av {

ReleaseQueue::ReleaseQueue() : held_{}, count_(0u), next_(0u), held_since_(0u), noted_next_(0u), active_(false)
{
    AV_SCOPE("ReleaseQueue::ReleaseQueue");
}

ReleaseQueue::Turn ReleaseQueue::arrive(const SubmissionDescriptor &batch, uint64_t published)
{
    AV_SCOPE("ReleaseQueue::arrive");
    if (!active_) {
        active_ = true;
        next_ = published;
    } else if (batch.begin() > next_ && published > next_) {
        next_ = published < batch.begin() ? published : batch.begin();
        drop_passed();
    }
    if (batch.end() <= next_) return Turn::released_already;
    if (batch.begin() < next_) return Turn::refused;
    if (batch.begin() == next_) return Turn::release_now;
    for (uint32_t index = 0u; index < count_; index++)
        if (held_[index].begin() == batch.begin()) return Turn::held;
    if (count_ == most_held) return Turn::refused;
    held_[count_++] = batch;
    return Turn::held;
}

void ReleaseQueue::released(const SubmissionDescriptor &batch)
{
    AV_SCOPE("ReleaseQueue::released");
    next_ = batch.end();
    drop_passed();
}

void ReleaseQueue::drop_passed()
{
    AV_SCOPE("ReleaseQueue::drop_passed");
    for (uint32_t index = 0u; index < count_;) {
        if (held_[index].begin() < next_) held_[index] = held_[--count_];
        else index++;
    }
}

bool ReleaseQueue::next_held(SubmissionDescriptor &batch)
{
    AV_SCOPE("ReleaseQueue::next_held");
    for (uint32_t index = 0u; index < count_; index++) {
        if (held_[index].begin() != next_) continue;
        batch = held_[index];
        held_[index] = held_[--count_];
        return true;
    }
    return false;
}

void ReleaseQueue::note(uint64_t now)
{
    AV_SCOPE("ReleaseQueue::note");
    if (count_ == 0u) {
        held_since_ = 0u;
        return;
    }
    if (held_since_ != 0u && noted_next_ == next_) return;
    held_since_ = now != 0u ? now : 1u;
    noted_next_ = next_;
}

bool ReleaseQueue::starved(uint64_t now, uint64_t after) const
{
    AV_SCOPE("ReleaseQueue::starved");
    return count_ != 0u && held_since_ != 0u && now >= held_since_ && now - held_since_ >= after;
}

bool ReleaseQueue::active() const
{
    AV_SCOPE("ReleaseQueue::active");
    return active_;
}

void ReleaseQueue::clear()
{
    AV_SCOPE("ReleaseQueue::clear");
    count_ = 0u;
    next_ = 0u;
    held_since_ = 0u;
    noted_next_ = 0u;
    active_ = false;
}

uint64_t ReleaseQueue::next() const
{
    AV_SCOPE("ReleaseQueue::next");
    return next_;
}

uint32_t ReleaseQueue::held() const
{
    AV_SCOPE("ReleaseQueue::held");
    return count_;
}

}
