#ifndef AV_STAMPED_LINE_HPP
#define AV_STAMPED_LINE_HPP

#include "core/platform/platform.hpp"
#include "core/support/text_buffer.hpp"

#include <stdint.h>

namespace av {

class StampedLine {
public:
    static constexpr uint64_t ticks_per_millisecond = 10000u;

    StampedLine(Platform &platform, uint64_t adapter, const char *event);
    StampedLine(const StampedLine &) = delete;
    StampedLine &operator=(const StampedLine &) = delete;

    StampedLine &number(const char *name, uint64_t value);
    StampedLine &address(const char *name, const void *value);
    void         write();

private:
    Platform  &platform_;
    char       bytes_[192];
    TextBuffer text_;
};

}

#endif
