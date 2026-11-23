#include "device/display/frame_latch.hpp"

#include "core/trace/function_scope.hpp"

namespace av {

FrameLatch::FrameLatch()
    : pending_frame_(0u), pending_key_(0u), pending_exported_(false), latched_frame_(0u), latched_key_(0u),
      unshown_(false), posts_(0u), posts_exported_on_post_(0u), posts_dropped_(0u)
{
    AV_SCOPE("FrameLatch::FrameLatch");
}

void FrameLatch::post(uint32_t frame, uint64_t key)
{
    AV_SCOPE("FrameLatch::post");
    if (frame == 0u) return;
    replace_pending(frame, key);
    pending_exported_ = false;
}

void FrameLatch::post_exported(uint32_t frame, uint64_t key)
{
    AV_SCOPE("FrameLatch::post_exported");
    if (frame == 0u) return;
    replace_pending(frame, key);
    pending_exported_ = true;
    posts_exported_on_post_++;
}

void FrameLatch::replace_pending(uint32_t frame, uint64_t key)
{
    AV_SCOPE("FrameLatch::replace_pending");
    if (pending_frame_ != 0u && pending_frame_ != latched_frame_ && !pending_exported_) posts_dropped_++;
    pending_frame_ = frame;
    pending_key_ = key;
    posts_++;
}

bool FrameLatch::latch_to_show()
{
    AV_SCOPE("FrameLatch::latch_to_show");
    if (pending_frame_ == 0u || pending_frame_ == latched_frame_) return false;
    latched_frame_ = pending_frame_;
    latched_key_ = pending_key_;
    unshown_ = !pending_exported_;
    return unshown_;
}

uint32_t FrameLatch::latched() const
{
    AV_SCOPE("FrameLatch::latched");
    return latched_frame_;
}

bool FrameLatch::take_unshown(uint64_t &key)
{
    AV_SCOPE("FrameLatch::take_unshown");
    if (!unshown_) return false;
    unshown_ = false;
    key = latched_key_;
    return true;
}

void FrameLatch::clear()
{
    AV_SCOPE("FrameLatch::clear");
    pending_frame_ = 0u;
    pending_key_ = 0u;
    pending_exported_ = false;
    latched_frame_ = 0u;
    latched_key_ = 0u;
    unshown_ = false;
}

uint64_t FrameLatch::posts() const
{
    AV_SCOPE("FrameLatch::posts");
    return posts_;
}

uint64_t FrameLatch::posts_exported_on_post() const
{
    AV_SCOPE("FrameLatch::posts_exported_on_post");
    return posts_exported_on_post_;
}

uint64_t FrameLatch::posts_dropped() const
{
    AV_SCOPE("FrameLatch::posts_dropped");
    return posts_dropped_;
}

bool FrameLatch::invariants_hold() const
{
    AV_SCOPE("FrameLatch::invariants_hold");
    const bool pending_named = pending_frame_ != 0u || (pending_key_ == 0u && !pending_exported_);
    return (!unshown_ || latched_frame_ != 0u) && pending_named && posts_exported_on_post_ <= posts_ &&
           posts_dropped_ <= posts_;
}

}
