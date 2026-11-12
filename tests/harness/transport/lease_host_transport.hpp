#ifndef AV_LEASE_HOST_TRANSPORT_HPP
#define AV_LEASE_HOST_TRANSPORT_HPP

#include "harness/transport/roomed_transport.hpp"
#include "proto/client/client_blob_arena.hpp"
#include "proto/wire/wire_arena.hpp"

#include <cstddef>
#include <cstdint>

namespace av {

class SessionLease;

class LeaseHostTransport final : public RoomedTransport {
public:
    static constexpr std::size_t slot_bytes = 4096u;

    explicit LeaseHostTransport(SessionLease &lease);

    std::uint32_t extensions() const;
    std::uint32_t returns() const;
    std::uint64_t returned() const;
    std::uint32_t others() const;
    void          refuse_all(bool refusing);

    WireWriter request_writer() override;
    [[nodiscard]] TransportResult send_async(std::uint32_t command_id, WireWriter &writer) override;
    [[nodiscard]] TransportResult send_sync(std::uint32_t command_id, WireWriter &writer,
                                            WireReader &reply) override;
    void             end_call() override;
    WireArena       &decode_arena() override;
    ClientBlobArena &blobs() override;

private:
    SessionLease   &lease_;
    std::uint8_t    request_[slot_bytes];
    std::uint8_t    reply_[slot_bytes];
    std::uint32_t   extensions_;
    std::uint32_t   returns_;
    std::uint64_t   returned_;
    std::uint32_t   others_;
    bool            refusing_;
    WireArena       arena_;
    ClientBlobArena blobs_;
};

}

#endif
