#ifndef AV_WORKER_REAPER_HPP
#define AV_WORKER_REAPER_HPP

#include "core/support/status.hpp"
#include "device/host/host.hpp"
#include "proto/context/context_capacity.hpp"

#include <stdint.h>

namespace av {

class Worker;
struct WorkerReaperProbe;

class WorkerReaper {
public:
    static constexpr uint32_t capacity = ContextCapacity::contexts;

    WorkerReaper(Host &host, HostRoutine reaped, void *owner);
    ~WorkerReaper();
    WorkerReaper(const WorkerReaper &) = delete;
    WorkerReaper &operator=(const WorkerReaper &) = delete;

    [[nodiscard]] Status start();
    [[nodiscard]] Status take(Worker &worker, uint32_t token);
    uint32_t             collect(uint32_t *tokens, uint32_t room);
    uint32_t             drain(uint64_t deadline, uint32_t &newly_overdue);
    void                 resume();
    void                 stop();
    void                 abandon();
    bool                 running() const;
    uint32_t             held();
    uint32_t             fresh();
    void                 run();

    bool invariants_hold() const;

private:
    enum class State : uint8_t { free, waiting, joined };

    struct Entry {
        Worker  *worker_;
        uint32_t token_;
        State    state_;
        bool     overdue_;
    };

    Worker  *exited_at(uint32_t index);
    void     join_exited();
    uint32_t waiting() const;
    void     release();

    Host       &host_;
    HostRoutine reaped_;
    void       *owner_;
    void       *thread_;
    void       *wake_;
    void       *drained_;
    void       *lock_;
    Entry       entries_[capacity];
    uint32_t    held_;
    bool        stopping_;
    bool        scheduled_;
    bool        quiet_;
    bool        abandoned_;

    friend struct WorkerReaperProbe;
};

}

#endif
