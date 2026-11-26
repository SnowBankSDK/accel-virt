#ifndef AV_DMA_BUF_ORIGIN_HPP
#define AV_DMA_BUF_ORIGIN_HPP

#include <stdint.h>

namespace av {

class DmaBufOrigin {
public:
    static constexpr uint32_t uuid_bytes = 16u;

    DmaBufOrigin();
    DmaBufOrigin(const DmaBufOrigin &) = delete;
    DmaBufOrigin &operator=(const DmaBufOrigin &) = delete;

    void               learn(const uint8_t (&uuid)[uuid_bytes]);
    void               forget();
    [[nodiscard]] bool stamp(int fd) const;
    bool               ours(int fd) const;
    bool               names_us(const char *fdinfo) const;

private:
    static constexpr uint32_t stamped_bytes = 12u;
    static constexpr uint32_t name_capacity = 32u;
    static constexpr uint32_t fdinfo_capacity = 4096u;

    char name_[name_capacity];
    bool known_;
};

}

#endif
