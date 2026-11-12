#ifndef AV_PRESENT_ENGINE_HPP
#define AV_PRESENT_ENGINE_HPP

#include "core/support/object_storage.hpp"
#include "core/support/status.hpp"
#include "device/present/present_page_window.hpp"
#include "device/present/present_frames.hpp"
#include "device/present/queued_present.hpp"

#include <stdint.h>

namespace av {

class ControlRequest;
class FenceInterrupts;
class HeldRequests;
class Host;
struct PresentEngineProbe;

class PresentEngine {
public:
    static constexpr uint32_t depth = 16u;
    static constexpr uint64_t stop_budget_nanoseconds = 2000000000u;

    PresentEngine(Host &host, FenceInterrupts &interrupts, HeldRequests &held);
    ~PresentEngine();
    PresentEngine(const PresentEngine &) = delete;
    PresentEngine &operator=(const PresentEngine &) = delete;

    [[nodiscard]] Status initialize();
    [[nodiscard]] Status attach(uint64_t address, const PresentFrames &snapshots);
    bool                 releasing();
    void                 attach_when_released(uint32_t ticket, uint64_t address, const PresentFrames &snapshots);
    void                 detach();
    void                 released();
    uint32_t             queue(ControlRequest &request);
    void                 run();

    bool invariants_hold() const;

private:
    QueuedPresent &slot(uint32_t index);
    void           release_moved();
    uint32_t       take(ControlRequest &request, uint64_t ticket);
    bool           refuse();
    QueuedPresent *next(uint32_t &generation);
    void           complete(QueuedPresent &present, Status moved, uint32_t generation);
    void           release_slots();
    void           stop_thread();

    Host                        &host_;
    FenceInterrupts             &interrupts_;
    HeldRequests                &requests_;
    void                        *lock_;
    void                        *wake_;
    void                        *exited_;
    void                        *thread_;
    bool                         stopping_;
    bool                         parked_;
    bool                         busy_;
    bool                         owed_;
    bool                         abandoned_;
    uint32_t                     generation_;
    uint32_t                     waiting_ticket_;
    uint64_t                     waiting_address_;
    PresentFrames                waiting_snapshots_;
    PresentPageWindow            page_;
    PresentFrames             snapshots_;
    ObjectStorage<QueuedPresent> slots_[depth];
    uint32_t                     first_;
    uint32_t                     moved_;
    uint32_t                     held_;
    uint64_t                     counted_;
    uint64_t                     completed_;
    uint64_t                     refused_;

    friend struct PresentEngineProbe;
};

}

#endif
