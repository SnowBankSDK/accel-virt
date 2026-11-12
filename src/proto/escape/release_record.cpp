#include "proto/escape/release_record.hpp"

#include "core/trace/function_scope.hpp"
#include "proto/wire/wire_reader.hpp"
#include "proto/wire/wire_writer.hpp"

namespace av {

ReleaseRecord::ReleaseRecord() : ring_(0u), writer_(0u), after_(0u), batch_()
{
    AV_SCOPE("ReleaseRecord::ReleaseRecord");
}

ReleaseRecord::ReleaseRecord(uint32_t ring, const SubmissionDescriptor &batch, uint32_t writer, uint32_t after)
    : ring_(ring), writer_(writer), after_(after), batch_(batch)
{
    AV_SCOPE("ReleaseRecord::ReleaseRecord");
}

uint32_t ReleaseRecord::ring() const
{
    AV_SCOPE("ReleaseRecord::ring");
    return ring_;
}

uint32_t ReleaseRecord::writer() const
{
    AV_SCOPE("ReleaseRecord::writer");
    return writer_;
}

uint32_t ReleaseRecord::after() const
{
    AV_SCOPE("ReleaseRecord::after");
    return after_;
}

const SubmissionDescriptor &ReleaseRecord::batch() const
{
    AV_SCOPE("ReleaseRecord::batch");
    return batch_;
}

Status ReleaseRecord::encode(uint8_t *bytes, size_t capacity) const
{
    AV_SCOPE("ReleaseRecord::encode");
    if (capacity < encoded_size) return Status::too_large;
    WireWriter writer(bytes, capacity);
    writer.write_u32(magic);
    writer.write_u32(ring_);
    writer.write_u32(writer_);
    writer.write_u32(after_);
    if (writer.status() != Status::ok) return writer.status();
    return batch_.encode(bytes + 16u, capacity - 16u);
}

Status ReleaseRecord::decode(const uint8_t *bytes, size_t length)
{
    AV_SCOPE("ReleaseRecord::decode");
    if (length < encoded_size) return Status::malformed;
    WireReader     reader(bytes, encoded_size);
    const uint32_t found_magic = reader.read_u32();
    const uint32_t ring = reader.read_u32();
    const uint32_t written_by = reader.read_u32();
    const uint32_t after = reader.read_u32();
    if (reader.status() != Status::ok || found_magic != magic || ring == 0u) return Status::malformed;
    SubmissionDescriptor batch;
    if (batch.decode(bytes + 16u, SubmissionDescriptor::encoded_size) != Status::ok) return Status::malformed;
    ring_ = ring;
    writer_ = written_by;
    after_ = after;
    batch_ = batch;
    return Status::ok;
}

}
