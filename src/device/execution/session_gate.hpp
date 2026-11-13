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
    [[nodiscard]] bool   reached(uint64_t end, uint64_t deadline, void *wake);
    [[nodiscard]] Status attend(void *wake);
    void                 depart(const void *wake);
    [[nodiscard]] Status listen(ContextSignal &signal);
    void                 forget(const ContextSignal &signal);
    void                 raise();

    bool invariants_hold() const;

private:
    void wake_waiters();

    Host          &host_;
    void          *run_;
    void          *listeners_lock_;
    const void    *runner_;
    uint64_t       consumed_;
    uint32_t       waiting_;
    bool           closed_;
    ContextSignal *listeners_[most_listeners];
    uint32_t       listening_;
    void          *waiters_[ContextCapacity::lanes];
    uint32_t       attending_;

    friend struct SessionGateProbe;
};

}

#endif
