#include "harness/transport/lease_host_transport.hpp"

#include "executor/session_lease.hpp"
#include "proto/submission/command_header.hpp"
#include "proto/submission/lease_record.hpp"

namespace av {

LeaseHostTransport::LeaseHostTransport(SessionLease &lease)
    : RoomedTransport(slot_bytes, slot_bytes), lease_(lease), request_{}, reply_{}, extensions_(0u), returns_(0u),
      returned_(0u), others_(0u), refusing_(false), arena_(nullptr, 0u), blobs_()
{
}

std::uint32_t LeaseHostTransport::extensions() const { return extensions_; }
std::uint32_t LeaseHostTransport::returns() const { return returns_; }
std::uint64_t LeaseHostTransport::returned() const { return returned_; }
std::uint32_t LeaseHostTransport::others() const { return others_; }
void          LeaseHostTransport::refuse_all(bool refusing) { refusing_ = refusing; }

WireWriter LeaseHostTransport::request_writer() { return WireWriter(request_, slot_bytes); }

TransportResult LeaseHostTransport::send_async(std::uint32_t command_id, WireWriter &writer)
{
    LeaseRecord record;
    if (command_id != CommandHeader::return_lease_id || record.decode(request_, writer.offset()) != Status::ok) {
        others_++;
        return TransportResult::ok;
    }
    returns_++;
    returned_ += record.bytes();
    lease_.surrender(static_cast<std::uint32_t>(record.kind()), record.bytes());
    return TransportResult::ok;
}

TransportResult LeaseHostTransport::send_sync(std::uint32_t command_id, WireWriter &writer, WireReader &reply)
{
    LeaseRecord record;
    if (command_id != CommandHeader::extend_lease_id || record.decode(request_, writer.offset()) != Status::ok) {
        others_++;
        return TransportResult::failed;
    }
    extensions_++;
    const std::uint64_t granted =
        refusing_ ? 0u : lease_.extend(static_cast<std::uint32_t>(record.kind()), record.bytes(), record.least());
    for (std::uint32_t byte = 0u; byte < LeaseRecord::answer_size; byte++)
        reply_[byte] = static_cast<std::uint8_t>(granted >> (8u * byte));
    reply = WireReader(reply_, LeaseRecord::answer_size);
    return TransportResult::ok;
}

void             LeaseHostTransport::end_call() {}
WireArena       &LeaseHostTransport::decode_arena() { return arena_; }
ClientBlobArena &LeaseHostTransport::blobs() { return blobs_; }

}
