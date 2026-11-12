#ifndef AV_RELEASE_QUEUE_HPP
#define AV_RELEASE_QUEUE_HPP

#include "proto/submission/submission_descriptor.hpp"

#include <stdint.h>

namespace av {

class ReleaseQueue {
public:
    static constexpr uint32_t most_held = 64u;
    static constexpr uint32_t most_writers = 32u;

    enum class Turn : uint8_t { release_now, held, released_already, refused };

    ReleaseQueue();

    Turn     arrive(const SubmissionDescriptor &batch, uint64_t published, uint32_t after);
    void     released(const SubmissionDescriptor &batch);
    bool     next_held(SubmissionDescriptor &batch);
    void     written_by(uint32_t writer, const void *context);
    bool     forget(const void *context);
    bool     stranded() const;
    bool     active() const;
    void     clear();
    uint32_t held() const;
    uint64_t next() const;
    uint64_t awaitable(uint64_t fence) const;

private:
    void drop_passed();
    void take_out(uint32_t index);
    bool gone(uint32_t writer) const;

    SubmissionDescriptor held_[most_held];
    uint32_t             after_[most_held];
    uint32_t             writers_[most_writers];
    const void          *writer_contexts_[most_writers];
    uint32_t             count_;
    uint64_t             next_;
    uint64_t             published_fence_;
    bool                 active_;
};

}

#endif
