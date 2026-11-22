#ifndef AV_LATE_ANSWERS_HPP
#define AV_LATE_ANSWERS_HPP

#include "core/support/status.hpp"

namespace av {

class RendererAnswer;

class LateAnswers {
public:
    [[nodiscard]] virtual Status take(const RendererAnswer &answer) = 0;

protected:
    LateAnswers() = default;
    ~LateAnswers() = default;
    LateAnswers(const LateAnswers &) = delete;
    LateAnswers &operator=(const LateAnswers &) = delete;
};

}

#endif
