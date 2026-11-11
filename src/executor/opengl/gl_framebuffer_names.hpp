#ifndef AV_GL_FRAMEBUFFER_NAMES_HPP
#define AV_GL_FRAMEBUFFER_NAMES_HPP

#include "core/support/keyed_slots.hpp"
#include "proto/generated/opengl/gl_real_dispatch.h"

#include <stdint.h>

namespace av {

class BlockSource;
class GlErrorQueue;

class GlFramebufferNames {
public:
    static constexpr uint32_t gl_invalid_operation = 0x0502u;
    static constexpr uint32_t gl_out_of_memory = 0x0505u;
    static constexpr int32_t  deleted_at_once = 64;

    GlFramebufferNames(BlockSource &blocks, glproto::RealGl &gl, GlErrorQueue &errors);
    GlFramebufferNames(const GlFramebufferNames &) = delete;
    GlFramebufferNames &operator=(const GlFramebufferNames &) = delete;

    void install();

    [[nodiscard]] bool named(GLuint &name) const;
    [[nodiscard]] bool bound(GLuint &name);
    GLuint             guest_of(GLuint host) const;
    void               made(GLsizei count, GLuint *names);
    void               deleted(GLsizei count, const GLuint *names);
    void               release();
    void               forget_all();

    uint32_t count() const;

    bool invariants_hold() const;

private:
    [[nodiscard]] bool admit(GLuint guest, GLuint host);
    GLuint             fresh_guest();
    void               give_back(GLuint host);

    glproto::RealGl               &gl_;
    GlErrorQueue                  &errors_;
    KeyedSlots<uint32_t, uint32_t> hosts_;
    KeyedSlots<uint32_t, uint32_t> guests_;
    GLuint                         next_;
};

}

#endif
