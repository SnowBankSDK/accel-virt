#include "executor/opengl/gl_upload_ring.hpp"

#include "core/trace/function_scope.hpp"
#include "executor/opengl/gl_error_queue.hpp"
#include "executor/slice_progress.hpp"

namespace av {

namespace {

constexpr GLbitfield gl_dynamic_storage_bit = 0x0100u;

uint64_t aligned(uint64_t bytes, uint64_t alignment)
{
    AV_SCOPE("aligned");
    return (bytes + alignment - 1u) & ~(alignment - 1u);
}

}

GlUploadRing::GlUploadRing(const glproto::RealGl &gl, const SliceProgress &progress, GlErrorQueue &errors)
    : gl_(gl), progress_(progress), errors_(errors), spans_{}, first_(0u), count_(0u), head_(0u), buffer_(0u),
      refused_(false)
{
    AV_SCOPE("GlUploadRing::GlUploadRing");
}

bool GlUploadRing::write(uint32_t buffer, uint64_t offset, const uint8_t *data, uint64_t bytes)
{
    AV_SCOPE("GlUploadRing::write");
    if (bytes == 0u || bytes > largest || refused_) return false;
    if (buffer_ == 0u && !make()) return false;
    uint64_t at = 0u;
    if (!place(bytes, at)) return false;
    gl_.glNamedBufferSubData(buffer_, static_cast<GLintptr>(at), static_cast<GLsizeiptr>(bytes), data);
    gl_.glCopyNamedBufferSubData(buffer_, buffer, static_cast<GLintptr>(at), static_cast<GLintptr>(offset),
                                 static_cast<GLsizeiptr>(bytes));
    take(at, aligned(bytes, alignment));
    return true;
}

bool GlUploadRing::make()
{
    AV_SCOPE("GlUploadRing::make");
    refused_ = gl_.glCreateBuffers == nullptr || gl_.glNamedBufferStorage == nullptr ||
               gl_.glNamedBufferSubData == nullptr || gl_.glCopyNamedBufferSubData == nullptr ||
               gl_.glDeleteBuffers == nullptr;
    if (refused_) return false;
    errors_.collect();
    GLuint made = 0u;
    gl_.glCreateBuffers(1, &made);
    if (made != 0u) gl_.glNamedBufferStorage(made, static_cast<GLsizeiptr>(ring_bytes), nullptr, gl_dynamic_storage_bit);
    const uint32_t error = errors_.take();
    errors_.collect();
    if (made == 0u || error != 0u) {
        if (made != 0u) gl_.glDeleteBuffers(1, &made);
        refused_ = true;
        return false;
    }
    buffer_ = made;
    head_ = 0u;
    return true;
}

void GlUploadRing::reclaim()
{
    AV_SCOPE("GlUploadRing::reclaim");
    const uint64_t reached = progress_.reached();
    while (count_ != 0u && spans_[first_].mark_ <= reached) {
        first_ = (first_ + 1u) % most_spans;
        count_--;
    }
    if (count_ == 0u) {
        first_ = 0u;
        head_ = 0u;
    }
}

bool GlUploadRing::place(uint64_t bytes, uint64_t &at)
{
    AV_SCOPE("GlUploadRing::place");
    reclaim();
    const uint64_t size = aligned(bytes, alignment);
    const Span    *last = count_ != 0u ? &spans_[(first_ + count_ - 1u) % most_spans] : nullptr;
    const bool     joins = last != nullptr && last->mark_ == progress_.next();
    if (count_ == 0u) {
        at = 0u;
        return true;
    }
    const uint64_t tail = spans_[first_].begin_;
    if (head_ > tail) {
        if (head_ + size <= ring_bytes && (joins || count_ < most_spans)) {
            at = head_;
            return true;
        }
        if (size < tail && count_ < most_spans) {
            at = 0u;
            return true;
        }
        return false;
    }
    if (head_ + size < tail && (joins || count_ < most_spans)) {
        at = head_;
        return true;
    }
    return false;
}

void GlUploadRing::take(uint64_t at, uint64_t bytes)
{
    AV_SCOPE("GlUploadRing::take");
    const uint64_t mark = progress_.next();
    Span *const    last = count_ != 0u ? &spans_[(first_ + count_ - 1u) % most_spans] : nullptr;
    if (last != nullptr && last->mark_ == mark && last->end_ == at) {
        last->end_ = at + bytes;
    } else {
        spans_[(first_ + count_) % most_spans] = Span{at, at + bytes, mark};
        count_++;
    }
    head_ = at + bytes;
}

void GlUploadRing::release()
{
    AV_SCOPE("GlUploadRing::release");
    if (buffer_ != 0u && gl_.glDeleteBuffers != nullptr) {
        const GLuint name = buffer_;
        gl_.glDeleteBuffers(1, &name);
    }
    forget();
}

void GlUploadRing::forget()
{
    AV_SCOPE("GlUploadRing::forget");
    buffer_ = 0u;
    first_ = 0u;
    count_ = 0u;
    head_ = 0u;
    refused_ = false;
}

uint32_t GlUploadRing::spans() const
{
    AV_SCOPE("GlUploadRing::spans");
    return count_;
}

bool GlUploadRing::invariants_hold() const
{
    AV_SCOPE("GlUploadRing::invariants_hold");
    if (count_ > most_spans || first_ >= most_spans || head_ > ring_bytes) return false;
    if (count_ != 0u && buffer_ == 0u) return false;
    uint64_t previous_mark = 0u;
    for (uint32_t index = 0u; index < count_; index++) {
        const Span &span = spans_[(first_ + index) % most_spans];
        if (span.begin_ >= span.end_ || span.end_ > ring_bytes || span.mark_ < previous_mark) return false;
        previous_mark = span.mark_;
    }
    return true;
}

}
