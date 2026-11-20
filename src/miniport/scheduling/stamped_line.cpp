#include "miniport/scheduling/stamped_line.hpp"

#include "core/trace/function_scope.hpp"

namespace av {

StampedLine::StampedLine(Platform &platform, uint64_t adapter, const char *event)
    : platform_(platform), bytes_{}, text_(bytes_, sizeof bytes_)
{
    AV_SCOPE("StampedLine::StampedLine");
    text_.append(event);
    text_.append(": adapter ");
    text_.append_hex(adapter, 16u);
    text_.append(" at ");
    text_.append_decimal(platform_.interrupt_time() / ticks_per_millisecond);
    text_.append(" ms");
}

StampedLine &StampedLine::number(const char *name, uint64_t value)
{
    AV_SCOPE("StampedLine::number");
    text_.append(" ");
    text_.append(name);
    text_.append(" ");
    text_.append_decimal(value);
    return *this;
}

StampedLine &StampedLine::address(const char *name, const void *value)
{
    AV_SCOPE("StampedLine::address");
    text_.append(" ");
    text_.append(name);
    text_.append(" ");
    text_.append_hex(reinterpret_cast<uintptr_t>(value), 16u);
    return *this;
}

StampedLine &StampedLine::code(const char *name, uint32_t value)
{
    AV_SCOPE("StampedLine::code");
    text_.append(" ");
    text_.append(name);
    text_.append(" 0x");
    text_.append_hex(value, 8u);
    return *this;
}

void StampedLine::write()
{
    AV_SCOPE("StampedLine::write");
    platform_.log(text_.text());
}

}
