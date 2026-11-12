#ifndef AV_RELEASE_RECORD_HPP
#define AV_RELEASE_RECORD_HPP

#include "core/support/status.hpp"
#include "proto/submission/submission_descriptor.hpp"

#include <stddef.h>
#include <stdint.h>

namespace av {

class ReleaseRecord {
public:
    static constexpr size_t encoded_size = 16u + SubmissionDescriptor::encoded_size;

    ReleaseRecord();
    ReleaseRecord(uint32_t ring, const SubmissionDescriptor &batch, uint32_t writer = 0u, uint32_t after = 0u);

    uint32_t                    ring() const;
    uint32_t                    writer() const;
    uint32_t                    after() const;
    const SubmissionDescriptor &batch() const;

    [[nodiscard]] Status encode(uint8_t *bytes, size_t capacity) const;
    [[nodiscard]] Status decode(const uint8_t *bytes, size_t length);

private:
    static constexpr uint32_t magic = 0x00525641u;

    uint32_t             ring_;
    uint32_t             writer_;
    uint32_t             after_;
    SubmissionDescriptor batch_;
};

}

#endif
