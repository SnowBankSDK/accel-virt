#ifndef AV_RECORDING_LATE_ANSWERS_HPP
#define AV_RECORDING_LATE_ANSWERS_HPP

#include "core/support/status.hpp"
#include "device/isolation/late_answers.hpp"
#include "proto/renderer/renderer_message_header.hpp"

#include <cstdint>

namespace av {

class RecordingLateAnswers final : public LateAnswers {
public:
    static constexpr std::uint32_t max_taken = 16u;

    RecordingLateAnswers();

    Status take(const RendererAnswer &answer) override;

    void                        claim(RendererMessageHeader::Kind kind, Status outcome);
    std::uint32_t               taken() const;
    std::uint64_t               sequence_at(std::uint32_t index) const;
    std::uint32_t               descriptors_at(std::uint32_t index) const;
    std::uint8_t                first_byte_at(std::uint32_t index) const;

private:
    struct Taken {
        std::uint64_t sequence;
        std::uint32_t descriptors;
        std::uint8_t  first_byte;
    };

    RendererMessageHeader::Kind claimed_;
    Status                      outcome_;
    bool                        claims_;
    Taken                       taken_[max_taken];
    std::uint32_t               count_;
};

}

#endif
