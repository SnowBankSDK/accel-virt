#include "proto/submission/lease_record.hpp"

#include "core/trace/function_scope.hpp"
#include "proto/wire/wire_reader.hpp"
#include "proto/wire/wire_writer.hpp"

namespace av {

LeaseRecord::LeaseRecord() : kind_(static_cast<uint32_t>(Kind::device)), bytes_(0u), least_(0u)
{
    AV_SCOPE("LeaseRecord::LeaseRecord");
}

LeaseRecord::LeaseRecord(Kind kind, uint64_t bytes, uint64_t least)
    : kind_(static_cast<uint32_t>(kind)), bytes_(bytes), least_(least)
{
    AV_SCOPE("LeaseRecord::LeaseRecord");
}

LeaseRecord::Kind LeaseRecord::kind() const
{
    AV_SCOPE("LeaseRecord::kind");
    return static_cast<Kind>(kind_);
}

uint64_t LeaseRecord::bytes() const
{
    AV_SCOPE("LeaseRecord::bytes");
    return bytes_;
}

uint64_t LeaseRecord::least() const
{
    AV_SCOPE("LeaseRecord::least");
    return least_;
}

bool LeaseRecord::fields_valid() const
{
    AV_SCOPE("LeaseRecord::fields_valid");
    return kind_ < kinds && least_ <= bytes_;
}

Status LeaseRecord::encode(uint8_t *into, size_t capacity) const
{
    AV_SCOPE("LeaseRecord::encode");
    if (capacity < encoded_size) return Status::too_large;
    if (!fields_valid()) return Status::invalid_parameter;
    WireWriter writer(into, capacity);
    writer.write_u32(kind_);
    writer.write_zeros(4u);
    writer.write_u64(bytes_);
    writer.write_u64(least_);
    return writer.status();
}

Status LeaseRecord::decode(const uint8_t *from, size_t length)
{
    AV_SCOPE("LeaseRecord::decode");
    if (length != encoded_size) return Status::malformed;
    WireReader  reader(from, length);
    LeaseRecord candidate;
    candidate.kind_ = reader.read_u32();
    reader.expect_zero(4u);
    candidate.bytes_ = reader.read_u64();
    candidate.least_ = reader.read_u64();
    if (reader.status() != Status::ok || !candidate.fields_valid()) return Status::malformed;
    *this = candidate;
    return Status::ok;
}

}
