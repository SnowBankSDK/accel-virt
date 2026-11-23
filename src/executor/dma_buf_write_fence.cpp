#include "executor/dma_buf_write_fence.hpp"

#include "core/log/driver_log.hpp"
#include "core/trace/function_scope.hpp"

#include <linux/dma-buf.h>
#include <sys/ioctl.h>

namespace av {

DmaBufWriteFence::DmaBufWriteFence(DriverLog &lines) : lines_(lines), refused_(0u)
{
    AV_SCOPE("DmaBufWriteFence::DmaBufWriteFence");
}

bool DmaBufWriteFence::attach(int dma_buf, int sync_file)
{
    AV_SCOPE("DmaBufWriteFence::attach");
    dma_buf_import_sync_file fence = {};
    fence.flags = DMA_BUF_SYNC_WRITE;
    fence.fd = sync_file;
    if (dma_buf >= 0 && sync_file >= 0 && ioctl(dma_buf, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &fence) == 0) return true;
    if (++refused_ == 1u) lines_.write("the export image takes no write fence: each export waits for its copy");
    return false;
}

uint64_t DmaBufWriteFence::refused() const
{
    AV_SCOPE("DmaBufWriteFence::refused");
    return refused_;
}

}
