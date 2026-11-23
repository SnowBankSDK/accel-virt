#ifndef AV_UDMABUF_HPP
#define AV_UDMABUF_HPP

#include <cstdint>

namespace av {

class Udmabuf {
public:
    explicit Udmabuf(std::uint64_t bytes);
    ~Udmabuf();
    Udmabuf(const Udmabuf &) = delete;
    Udmabuf &operator=(const Udmabuf &) = delete;

    bool           made() const;
    int            fd() const;
    std::uint32_t *pixels() const;
    int            read_fence() const;
    void           let_go();

private:
    int            memfd_;
    int            fd_;
    std::uint64_t  bytes_;
    std::uint32_t *mapped_;
};

}

#endif
