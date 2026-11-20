#ifndef AV_D3D_COMPLETION_WATERMARK_HPP
#define AV_D3D_COMPLETION_WATERMARK_HPP

#include <stdint.h>

namespace av::d3d {

class CompletionWatermark {
public:
    CompletionWatermark();
    CompletionWatermark(const CompletionWatermark &) = delete;
    CompletionWatermark &operator=(const CompletionWatermark &) = delete;

    uint64_t stamp_write();
    uint64_t ended() const;
    void     seen(uint64_t event_order);
    bool     known_done(uint64_t write_order) const;

private:
    uint64_t order_;
    uint64_t watermark_;
};

}

#endif
