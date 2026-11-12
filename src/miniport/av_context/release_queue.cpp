#include "miniport/av_context/release_queue.hpp"

#include "core/trace/function_scope.hpp"

namespace av {

ReleaseQueue::ReleaseQueue()
    : held_{}, after_{}, writers_{}, writer_contexts_{}, count_(0u), next_(0u), active_(false)
{
    AV_SCOPE("ReleaseQueue::ReleaseQueue");
}

ReleaseQueue::Turn ReleaseQueue::arrive(const SubmissionDescriptor &batch, uint64_t published, uint32_t after)
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
    after_[count_] = after;
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
        if (held_[index].begin() < next_) take_out(index);
        else index++;
    }
}

bool ReleaseQueue::next_held(SubmissionDescriptor &batch)
{
    AV_SCOPE("ReleaseQueue::next_held");
    for (uint32_t index = 0u; index < count_; index++) {
        if (held_[index].begin() != next_) continue;
        batch = held_[index];
        take_out(index);
        return true;
    }
    return false;
}

void ReleaseQueue::take_out(uint32_t index)
{
    AV_SCOPE("ReleaseQueue::take_out");
    count_--;
    held_[index] = held_[count_];
    after_[index] = after_[count_];
}

void ReleaseQueue::written_by(uint32_t writer, const void *context)
{
    AV_SCOPE("ReleaseQueue::written_by");
    if (writer == 0u || context == nullptr) return;
    uint32_t free = most_writers;
    for (uint32_t index = 0u; index < most_writers; index++) {
        if (writers_[index] == writer) return;
        if (free == most_writers && writers_[index] == 0u) free = index;
    }
    for (uint32_t index = 0u; index < most_writers && free == most_writers; index++)
        if (writer_contexts_[index] == nullptr) free = index;
    if (free == most_writers) return;
    writers_[free] = writer;
    writer_contexts_[free] = context;
}

bool ReleaseQueue::forget(const void *context)
{
    AV_SCOPE("ReleaseQueue::forget");
    bool forgot = false;
    for (uint32_t index = 0u; index < most_writers; index++) {
        if (context == nullptr || writer_contexts_[index] != context) continue;
        writer_contexts_[index] = nullptr;
        forgot = true;
    }
    return forgot;
}

bool ReleaseQueue::gone(uint32_t writer) const
{
    AV_SCOPE("ReleaseQueue::gone");
    for (uint32_t index = 0u; index < most_writers; index++)
        if (writer != 0u && writers_[index] == writer) return writer_contexts_[index] == nullptr;
    return false;
}

bool ReleaseQueue::stranded() const
{
    AV_SCOPE("ReleaseQueue::stranded");
    uint32_t first = most_held;
    for (uint32_t index = 0u; index < count_; index++)
        if (first == most_held || held_[index].begin() < held_[first].begin()) first = index;
    return first != most_held && gone(after_[first]);
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
    for (uint32_t index = 0u; index < most_writers; index++) {
        writers_[index] = 0u;
        writer_contexts_[index] = nullptr;
    }
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
