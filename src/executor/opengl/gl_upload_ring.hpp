#ifndef AV_GL_UPLOAD_RING_HPP
#define AV_GL_UPLOAD_RING_HPP

#include "proto/generated/opengl/gl_real_dispatch.h"

#include <stdint.h>

namespace av {

class GlErrorQueue;
class SliceProgress;

class GlUploadRing {
public:
    static constexpr uint64_t ring_bytes = 4u << 20u;
    static constexpr uint64_t largest = ring_bytes / 4u;
    static constexpr uint64_t alignment = 256u;
    static constexpr uint32_t most_spans = 64u;

    GlUploadRing(const glproto::RealGl &gl, const SliceProgress &progress, GlErrorQueue &errors);
    GlUploadRing(const GlUploadRing &) = delete;
    GlUploadRing &operator=(const GlUploadRing &) = delete;

    [[nodiscard]] bool write(uint32_t buffer, uint64_t offset, const uint8_t *data, uint64_t bytes);
    void               release();
    void               forget();
    uint32_t           spans() const;

    bool invariants_hold() const;

private:
    struct Span {
        uint64_t begin_;
        uint64_t end_;
        uint64_t mark_;
    };

    bool make();
    void reclaim();
    bool place(uint64_t bytes, uint64_t &at);
    void take(uint64_t at, uint64_t bytes);

    const glproto::RealGl &gl_;
    const SliceProgress   &progress_;
    GlErrorQueue          &errors_;
    Span                   spans_[most_spans];
    uint32_t               first_;
    uint32_t               count_;
    uint64_t               head_;
    uint32_t               buffer_;
    bool                   refused_;
};

}

#endif
