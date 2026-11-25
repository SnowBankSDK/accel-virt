#ifndef AV_D3D_AMORTIZED_PROCESSING_HPP
#define AV_D3D_AMORTIZED_PROCESSING_HPP

#include <stdint.h>

namespace av::icd {
class CallingThread;
}

namespace av::d3d {

class ReentrantCallLock;

class AmortizedProcessing {
public:
    AmortizedProcessing(ReentrantCallLock &calls, icd::CallingThread &caller);
    AmortizedProcessing(const AmortizedProcessing &) = delete;
    AmortizedProcessing &operator=(const AmortizedProcessing &) = delete;

    [[nodiscard]] bool due(uint64_t count, uint64_t step);
    void               begin();
    void               end();

private:
    ReentrantCallLock  &calls_;
    icd::CallingThread &caller_;
    uint64_t            counted_;
    uint32_t            amortizing_;
};

}

#endif
