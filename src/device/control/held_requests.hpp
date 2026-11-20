#ifndef AV_HELD_REQUESTS_HPP
#define AV_HELD_REQUESTS_HPP

#include "core/support/status.hpp"

#include <stddef.h>
#include <stdint.h>

namespace av {

class ControlRequest;
class GpuResponse;
class Host;
struct HeldRequestsProbe;

class HeldRequests {
public:
    using Answering = uint32_t (*)(Status status);

    static constexpr uint32_t capacity = 256u;
    static constexpr uint32_t no_ticket = 0u;

    explicit HeldRequests(Host &host);
    ~HeldRequests();
    HeldRequests(const HeldRequests &) = delete;
    HeldRequests &operator=(const HeldRequests &) = delete;

    [[nodiscard]] Status hold(ControlRequest &request, Answering answering, uint32_t &ticket);
    bool                 holds(uint32_t ticket) const;
    void                *kept(uint32_t ticket) const;
    void                 conclude(uint32_t ticket, Status status);
    void                 answer(uint32_t ticket, const GpuResponse &response);
    void                 drop_all();
    uint32_t             held() const;

    bool invariants_hold() const;

private:
    struct Held {
        void     *kept_;
        Answering answering_;
        uint32_t  serial_;
    };

    static constexpr uint32_t index_bits = 8u;
    static constexpr uint32_t index_mask = (1u << index_bits) - 1u;

    const Held *slot_of(uint32_t ticket) const;
    void        release(uint32_t index, bool answered);

    Host    &host_;
    Held     held_[capacity];
    uint32_t live_;
    uint32_t serial_;

    friend struct HeldRequestsProbe;
};

}

#endif
