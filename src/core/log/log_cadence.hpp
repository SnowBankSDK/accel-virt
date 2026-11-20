#ifndef AV_LOG_CADENCE_HPP
#define AV_LOG_CADENCE_HPP

#include <stdint.h>

namespace av {

class LogCadence {
public:
    static constexpr uint64_t spoken_first = 4u;
    static constexpr uint64_t then_every = 128u;

    LogCadence();
    LogCadence(const LogCadence &) = delete;
    LogCadence &operator=(const LogCadence &) = delete;

    [[nodiscard]] bool speaks();
    uint64_t           seen() const;
    void               reset();

private:
    uint64_t seen_;
};

}

#endif
