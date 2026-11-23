#include "harness/memory/udmabuf.hpp"

#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/udmabuf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace av {

Udmabuf::Udmabuf(std::uint64_t bytes) : memfd_(-1), fd_(-1), bytes_(bytes), mapped_(nullptr)
{
    const int device = open("/dev/udmabuf", O_RDWR | O_CLOEXEC);
    if (device < 0) return;
    memfd_ = memfd_create("av-udmabuf", MFD_ALLOW_SEALING | MFD_CLOEXEC);
    if (memfd_ >= 0 && ftruncate(memfd_, static_cast<off_t>(bytes)) == 0 &&
        fcntl(memfd_, F_ADD_SEALS, F_SEAL_SHRINK) == 0) {
        udmabuf_create create = {};
        create.memfd = static_cast<std::uint32_t>(memfd_);
        create.size = bytes;
        fd_ = ioctl(device, UDMABUF_CREATE, &create);
    }
    close(device);
    if (fd_ >= 0) {
        void *const map = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        mapped_ = map == MAP_FAILED ? nullptr : static_cast<std::uint32_t *>(map);
    }
}

Udmabuf::~Udmabuf()
{
    let_go();
}

bool Udmabuf::made() const
{
    return mapped_ != nullptr;
}

int Udmabuf::fd() const
{
    return fd_;
}

std::uint32_t *Udmabuf::pixels() const
{
    return mapped_;
}

int Udmabuf::read_fence() const
{
    dma_buf_export_sync_file exported = {};
    exported.flags = DMA_BUF_SYNC_READ;
    exported.fd = -1;
    return fd_ >= 0 && ioctl(fd_, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &exported) == 0 ? exported.fd : -1;
}

void Udmabuf::let_go()
{
    if (mapped_ != nullptr) munmap(mapped_, bytes_);
    if (fd_ >= 0) close(fd_);
    if (memfd_ >= 0) close(memfd_);
    mapped_ = nullptr;
    fd_ = -1;
    memfd_ = -1;
}

}
