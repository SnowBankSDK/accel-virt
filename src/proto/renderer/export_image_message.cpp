#include "proto/renderer/export_image_message.hpp"

#include "core/trace/function_scope.hpp"
#include "proto/wire/wire_reader.hpp"
#include "proto/wire/wire_writer.hpp"

namespace av {

ExportImageMessage::ExportImageMessage() : key_(0u), head_(0u)
{
    AV_SCOPE("ExportImageMessage::ExportImageMessage");
}

ExportImageMessage::ExportImageMessage(uint64_t key, uint32_t head) : key_(key), head_(head)
{
    AV_SCOPE("ExportImageMessage::ExportImageMessage");
}

uint64_t ExportImageMessage::key() const
{
    AV_SCOPE("ExportImageMessage::key");
    return key_;
}

uint32_t ExportImageMessage::head() const
{
    AV_SCOPE("ExportImageMessage::head");
    return head_;
}

Status ExportImageMessage::encode(uint8_t *bytes, size_t capacity) const
{
    AV_SCOPE("ExportImageMessage::encode");
    if (bytes == nullptr || capacity < encoded_size) return Status::too_large;
    if (head_ >= heads) return Status::invalid_parameter;
    WireWriter writer(bytes, capacity);
    writer.write_u64(key_);
    writer.write_u32(head_);
    writer.write_u32(0u);
    return writer.status();
}

Status ExportImageMessage::decode(const uint8_t *bytes, size_t length, uint32_t descriptor_count)
{
    AV_SCOPE("ExportImageMessage::decode");
    if (bytes == nullptr || length != encoded_size || descriptor_count != descriptors) return Status::malformed;
    WireReader     reader(bytes, length);
    const uint64_t key = reader.read_u64();
    const uint32_t head = reader.read_u32();
    const uint32_t reserved = reader.read_u32();
    if (head >= heads || reserved != 0u) return Status::malformed;
    key_ = key;
    head_ = head;
    return Status::ok;
}

}
