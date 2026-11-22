#ifndef AV_RENDERER_ANSWER_HPP
#define AV_RENDERER_ANSWER_HPP

#include "core/support/status.hpp"
#include "core/trace/function_scope.hpp"
#include "proto/renderer/renderer_message_header.hpp"

#include <stdint.h>

namespace av {

class Host;

class RendererAnswer {
public:
    RendererAnswer(Host &host, const RendererMessageHeader &header, const uint8_t *body, int *descriptors,
                   uint32_t count);
    RendererAnswer(const RendererAnswer &) = delete;
    RendererAnswer &operator=(const RendererAnswer &) = delete;

    RendererMessageHeader::Kind kind() const;
    uint64_t                    sequence() const;
    int                         descriptor() const;
    void                        close() const;
    template <typename Message>
    [[nodiscard]] Status decode(Message &message) const;

private:
    Host                 &host_;
    RendererMessageHeader header_;
    const uint8_t        *body_;
    int                  *descriptors_;
    uint32_t              count_;
};

template <typename Message>
Status RendererAnswer::decode(Message &message) const
{
    AV_SCOPE("RendererAnswer::decode");
    return message.decode(body_, header_.body_length(), count_);
}

}

#endif
