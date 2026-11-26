#include "executor/vulkan/dma_buf_origin.hpp"

#include "core/trace/function_scope.hpp"

#include <fcntl.h>
#include <linux/dma-buf.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace av {

DmaBufOrigin::DmaBufOrigin() : name_{}, known_(false)
{
    AV_SCOPE("DmaBufOrigin::DmaBufOrigin");
}

void DmaBufOrigin::learn(const uint8_t (&uuid)[uuid_bytes])
{
    AV_SCOPE("DmaBufOrigin::learn");
    constexpr char digits[] = "0123456789abcdef";
    uint32_t       at = 0u;
    name_[at++] = 'a';
    name_[at++] = 'v';
    name_[at++] = '-';
    for (uint32_t index = 0u; index < stamped_bytes; index++) {
        name_[at++] = digits[uuid[index] >> 4u];
        name_[at++] = digits[uuid[index] & 0xfu];
    }
    name_[at] = '\0';
    known_ = true;
}

void DmaBufOrigin::forget()
{
    AV_SCOPE("DmaBufOrigin::forget");
    name_[0] = '\0';
    known_ = false;
}

bool DmaBufOrigin::stamp(int fd) const
{
    AV_SCOPE("DmaBufOrigin::stamp");
    return known_ && fd >= 0 && ioctl(fd, DMA_BUF_SET_NAME, name_) == 0;
}

bool DmaBufOrigin::ours(int fd) const
{
    AV_SCOPE("DmaBufOrigin::ours");
    if (!known_ || fd < 0) return false;
    char path[64] = {};
    static_cast<void>(snprintf(path, sizeof path, "/proc/self/fdinfo/%d", fd));
    const int info = open(path, O_RDONLY | O_CLOEXEC);
    if (info < 0) return false;
    char          text[fdinfo_capacity] = {};
    const ssize_t got = read(info, text, sizeof text - 1u);
    static_cast<void>(close(info));
    return got > 0 && names_us(text);
}

bool DmaBufOrigin::names_us(const char *fdinfo) const
{
    AV_SCOPE("DmaBufOrigin::names_us");
    if (!known_ || fdinfo == nullptr) return false;
    constexpr char key[] = "name:";
    for (const char *line = fdinfo; *line != '\0';) {
        if (strncmp(line, key, sizeof key - 1u) == 0) {
            const char *value = line + sizeof key - 1u;
            while (*value == ' ' || *value == '\t') value++;
            const size_t length = strlen(name_);
            return strncmp(value, name_, length) == 0 && (value[length] == '\n' || value[length] == '\0');
        }
        const char *next = strchr(line, '\n');
        if (next == nullptr) break;
        line = next + 1;
    }
    return false;
}

}
