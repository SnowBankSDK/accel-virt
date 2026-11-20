#ifndef AV_ICD_LANE_PROGRESS_HPP
#define AV_ICD_LANE_PROGRESS_HPP

#include <stdint.h>

namespace av::icd {

class LaneProgress {
public:
    LaneProgress(const LaneProgress &) = delete;
    LaneProgress &operator=(const LaneProgress &) = delete;

    virtual uint64_t covering() const = 0;
    virtual bool     ran(uint64_t fence) const = 0;
    virtual void     publish(uint64_t fence) = 0;

protected:
    LaneProgress() = default;
    ~LaneProgress() = default;
};

}

#endif
