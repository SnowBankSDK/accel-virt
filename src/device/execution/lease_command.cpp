#include "device/execution/lease_command.hpp"

#include "core/trace/function_scope.hpp"
#include "device/execution/context_executor.hpp"
#include "proto/submission/lease_record.hpp"

namespace av {

LeaseCommand::LeaseCommand(ContextExecutor &executor, const CommandHeader &header, const uint8_t *body)
    : executor_(executor), header_(header), body_(body)
{
    AV_SCOPE("LeaseCommand::LeaseCommand");
}

Status LeaseCommand::run(uint8_t *reply, size_t capacity, size_t &produced)
{
    AV_SCOPE("LeaseCommand::run");
    produced = 0u;
    const bool extending = header_.id() == CommandHeader::extend_lease_id;
    if (header_.generates_reply() != extending || header_.size() < CommandHeader::encoded_size) return Status::malformed;
    LeaseRecord record;
    if (record.decode(body_, header_.size() - CommandHeader::encoded_size) != Status::ok) return Status::malformed;
    const uint32_t kind = static_cast<uint32_t>(record.kind());
    if (!extending) {
        if (record.least() != 0u) return Status::malformed;
        executor_.return_lease(kind, record.bytes());
        return Status::ok;
    }
    uint64_t granted = 0u;
    if (executor_.extend_lease(kind, record.bytes(), record.least(), granted) != Status::ok) granted = 0u;
    produced = LeaseRecord::answer_size;
    if (reply == nullptr || capacity < produced) return Status::ok;
    for (uint32_t byte = 0u; byte < produced; byte++) reply[byte] = static_cast<uint8_t>(granted >> (8u * byte));
    return Status::ok;
}

}
