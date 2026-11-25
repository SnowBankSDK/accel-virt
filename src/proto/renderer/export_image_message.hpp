#ifndef AV_EXPORT_IMAGE_MESSAGE_HPP
#define AV_EXPORT_IMAGE_MESSAGE_HPP

#include "core/support/status.hpp"

#include <stddef.h>
#include <stdint.h>

namespace av {

class ExportImageMessage {
public:
    static constexpr size_t   encoded_size = 16u;
    static constexpr uint32_t descriptors = 0u;
    static constexpr uint32_t heads = 16u;

    ExportImageMessage();
    ExportImageMessage(uint64_t key, uint32_t head);

    uint64_t key() const;
    uint32_t head() const;

    [[nodiscard]] Status encode(uint8_t *bytes, size_t capacity) const;
    [[nodiscard]] Status decode(const uint8_t *bytes, size_t length, uint32_t descriptor_count);

private:
    uint64_t key_;
    uint32_t head_;
};

}

#endif
