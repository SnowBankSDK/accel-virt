#ifndef AV_SESSION_GATE_HPP
#define AV_SESSION_GATE_HPP

#include "core/support/status.hpp"
#include "proto/context/context_capacity.hpp"

#include <stdint.h>

namespace av {

class ContextSignal;
class Host;
struct SessionGateProbe;

class SessionGate {
public:
    static constexpr uint32_t most_listeners = ContextCapacity::lanes + 1u;
    static constexpr uint64_t wait_slice_ns = 1000000u;

    explicit SessionGate(Host &host);
    ~SessionGate();
    SessionGate(const SessionGate &) = delete;
    SessionGate &operator=(const SessionGate &) = delete;

    [[nodiscard]] Status initialize();
    bool                 ready() const;
    void                 close();
    bool                 closed() const;
    [[nodiscard]] bool   enter(const void *runner);
    void                 leave();
    void                 consumed(uint64_t head);
    uint64_t             consumed() const;
    [[nodiscard]] bool   reached(uint64_t end, uint64_t deadline);
    [[nodiscard]] Status listen(ContextSignal &signal);
    void                 forget(const ContextSignal &signal);
    void                 raise();

    bool invariants_hold() const;

private:
    Host          &host_;
    void          *run_;
    void          *listeners_lock_;
    void          *progress_;
    const void    *runner_;
    uint64_t       consumed_;
    uint32_t       waiting_;
    bool           closed_;
    ContextSignal *listeners_[most_listeners];
    uint32_t       listening_;

    friend struct SessionGateProbe;
};

}

#endif
