#ifndef AV_ICD_JOINT_TIMELINE_HPP
#define AV_ICD_JOINT_TIMELINE_HPP

#include "icd/common/gpu_timeline.hpp"
#include "proto/context/context_capacity.hpp"

#include <stdint.h>

namespace av {
class Kmt;
class ThreadLock;
}

namespace av::icd {

class LaneProgress;

class JointTimeline final : public GpuTimeline {
public:
    static constexpr uint32_t most_lanes = ContextCapacity::lanes;
    static constexpr uint32_t marks = 256u;
    static constexpr uint64_t patience_seconds = 30u;

    JointTimeline(GpuTimeline &session, ThreadLock &lock, Kmt &kmt);
    JointTimeline(const JointTimeline &) = delete;
    JointTimeline &operator=(const JointTimeline &) = delete;

    void join(LaneProgress &lane);
    void leave(const LaneProgress &lane);

    uint64_t           recording() const override;
    uint64_t           covering() const override;
    uint64_t           completed() const override;
    [[nodiscard]] bool wait(uint64_t mark) override;

private:
    struct Mark {
        uint64_t session_;
        uint64_t lanes_[most_lanes];
        uint32_t generations_[most_lanes];
    };

    uint64_t mark(uint64_t session) const;
    bool     reached(const Mark &mark) const;
    void     advance() const;

    GpuTimeline        &session_;
    ThreadLock         &lock_;
    Kmt                &kmt_;
    LaneProgress       *lanes_[most_lanes];
    uint32_t            generations_[most_lanes];
    uint32_t            joined_;
    mutable Mark        marks_[marks];
    mutable uint64_t    stamped_;
    mutable uint64_t    reached_;
};

}

#endif
