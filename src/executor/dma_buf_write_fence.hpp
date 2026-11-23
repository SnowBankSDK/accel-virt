#ifndef AV_DMA_BUF_WRITE_FENCE_HPP
#define AV_DMA_BUF_WRITE_FENCE_HPP

#include <stdint.h>

namespace av {

class DriverLog;

class DmaBufWriteFence {
public:
    explicit DmaBufWriteFence(DriverLog &lines);

    [[nodiscard]] bool attach(int dma_buf, int sync_file);
    uint64_t           refused() const;

private:
    DriverLog &lines_;
    uint64_t   refused_;
};

}

#endif
