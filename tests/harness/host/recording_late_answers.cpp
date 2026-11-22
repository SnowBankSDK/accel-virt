#include "harness/host/recording_late_answers.hpp"

#include "device/isolation/renderer_answer.hpp"

#include <cstddef>

namespace av {

namespace {

class Peek {
public:
    Status decode(const std::uint8_t *bytes, std::size_t length, std::uint32_t descriptors)
    {
        first_byte_ = length != 0u ? bytes[0] : std::uint8_t{0};
        descriptors_ = descriptors;
        return Status::ok;
    }
    std::uint8_t  first_byte() const { return first_byte_; }
    std::uint32_t descriptors() const { return descriptors_; }

private:
    std::uint8_t  first_byte_ = 0u;
    std::uint32_t descriptors_ = 0u;
};

}

RecordingLateAnswers::RecordingLateAnswers()
    : claimed_(RendererMessageHeader::Kind::reply), outcome_(Status::ok), claims_(false), taken_{}, count_(0u)
{
}

Status RecordingLateAnswers::take(const RendererAnswer &answer)
{
    if (!claims_ || answer.kind() != claimed_) return Status::not_found;
    Peek peek;
    static_cast<void>(answer.decode(peek));
    if (count_ < max_taken) taken_[count_++] = Taken{answer.sequence(), peek.descriptors(), peek.first_byte()};
    answer.close();
    return outcome_;
}

void RecordingLateAnswers::claim(RendererMessageHeader::Kind kind, Status outcome)
{
    claimed_ = kind;
    outcome_ = outcome;
    claims_ = true;
}

std::uint32_t RecordingLateAnswers::taken() const
{
    return count_;
}

std::uint64_t RecordingLateAnswers::sequence_at(std::uint32_t index) const
{
    return taken_[index].sequence;
}

std::uint32_t RecordingLateAnswers::descriptors_at(std::uint32_t index) const
{
    return taken_[index].descriptors;
}

std::uint8_t RecordingLateAnswers::first_byte_at(std::uint32_t index) const
{
    return taken_[index].first_byte;
}

}
