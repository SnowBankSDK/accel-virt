#ifndef AV_LEASE_RECORD_HPP
#define AV_LEASE_RECORD_HPP

#include "core/support/status.hpp"

#include <stddef.h>
#include <stdint.h>

namespace av {

class LeaseRecord {
public:
    enum class Kind : uint32_t { device = 0u, host = 1u };

    static constexpr uint32_t kinds = 2u;
    static constexpr size_t   encoded_size = 24u;
    static constexpr size_t   answer_size = 8u;
    static constexpr uint64_t initial_bytes[kinds] = {uint64_t{2} << 20u, 0u};
    static constexpr uint64_t direct3d_initial_bytes[kinds] = {uint64_t{16} << 20u, 0u};

    LeaseRecord();
    LeaseRecord(Kind kind, uint64_t bytes, uint64_t least);

    Kind     kind() const;
    uint64_t bytes() const;
    uint64_t least() const;

    [[nodiscard]] Status encode(uint8_t *into, size_t capacity) const;
    [[nodiscard]] Status decode(const uint8_t *from, size_t length);

private:
    bool fields_valid() const;

    uint32_t kind_;
    uint64_t bytes_;
    uint64_t least_;
};

}

#endif
