#include "executor/opengl/gl_framebuffer_names.hpp"

#include "core/trace/function_scope.hpp"
#include "executor/opengl/gl_error_queue.hpp"

namespace av {

namespace {

bool name_framebuffer(void *user, GLuint *name)
{
    AV_SCOPE("name_framebuffer");
    return static_cast<const GlFramebufferNames *>(user)->named(*name);
}

bool bind_framebuffer(void *user, GLuint *name)
{
    AV_SCOPE("bind_framebuffer");
    return static_cast<GlFramebufferNames *>(user)->bound(*name);
}

GLuint guest_framebuffer(void *user, GLuint host)
{
    AV_SCOPE("guest_framebuffer");
    return static_cast<const GlFramebufferNames *>(user)->guest_of(host);
}

void framebuffers_made(void *user, GLsizei count, GLuint *names)
{
    AV_SCOPE("framebuffers_made");
    static_cast<GlFramebufferNames *>(user)->made(count, names);
}

void framebuffers_deleted(void *user, GLsizei count, const GLuint *names)
{
    AV_SCOPE("framebuffers_deleted");
    static_cast<GlFramebufferNames *>(user)->deleted(count, names);
}

}

GlFramebufferNames::GlFramebufferNames(BlockSource &blocks, glproto::RealGl &gl, GlErrorQueue &errors)
    : gl_(gl), errors_(errors), hosts_(blocks, Pool::paged), guests_(blocks, Pool::paged), next_(1u)
{
    AV_SCOPE("GlFramebufferNames::GlFramebufferNames");
}

void GlFramebufferNames::install()
{
    AV_SCOPE("GlFramebufferNames::install");
    gl_.av_framebuffer_named = name_framebuffer;
    gl_.av_framebuffer_bound = bind_framebuffer;
    gl_.av_framebuffer_guest = guest_framebuffer;
    gl_.av_framebuffers_made = framebuffers_made;
    gl_.av_framebuffers_deleted = framebuffers_deleted;
    gl_.av_framebuffer_user = this;
}

bool GlFramebufferNames::named(GLuint &name) const
{
    AV_SCOPE("GlFramebufferNames::named");
    if (name == 0u) return true;
    const uint32_t *const host = hosts_.find(name);
    if (host == nullptr) return false;
    name = *host;
    return true;
}

bool GlFramebufferNames::bound(GLuint &name)
{
    AV_SCOPE("GlFramebufferNames::bound");
    if (named(name)) return true;
    if (!gl_.av_fixed_function || gl_.glGenFramebuffers == nullptr) return false;
    GLuint host = 0u;
    gl_.glGenFramebuffers(1, &host);
    if (host == 0u) return false;
    if (!admit(name, host)) {
        give_back(host);
        return false;
    }
    name = host;
    return true;
}

GLuint GlFramebufferNames::guest_of(GLuint host) const
{
    AV_SCOPE("GlFramebufferNames::guest_of");
    const uint32_t *const guest = host != 0u ? guests_.find(host) : nullptr;
    return guest != nullptr ? *guest : 0u;
}

void GlFramebufferNames::made(GLsizei count, GLuint *names)
{
    AV_SCOPE("GlFramebufferNames::made");
    for (GLsizei at = 0; at < count; at++) {
        const GLuint host = names[at];
        if (host == 0u) continue;
        const GLuint guest = fresh_guest();
        if (guest != 0u && admit(guest, host)) {
            names[at] = guest;
            continue;
        }
        give_back(host);
        names[at] = 0u;
        errors_.record(gl_out_of_memory);
    }
}

void GlFramebufferNames::deleted(GLsizei count, const GLuint *names)
{
    AV_SCOPE("GlFramebufferNames::deleted");
    GLuint  hosts[deleted_at_once];
    GLsizei held = 0;
    for (GLsizei at = 0; at < count; at++) {
        uint32_t host = 0u;
        if (names[at] == 0u || !hosts_.take(names[at], host)) continue;
        static_cast<void>(guests_.erase(host));
        hosts[held++] = host;
        if (held < deleted_at_once) continue;
        if (gl_.glDeleteFramebuffers != nullptr) gl_.glDeleteFramebuffers(held, hosts);
        held = 0;
    }
    if (held != 0 && gl_.glDeleteFramebuffers != nullptr) gl_.glDeleteFramebuffers(held, hosts);
}

void GlFramebufferNames::release()
{
    AV_SCOPE("GlFramebufferNames::release");
    GLuint  hosts[deleted_at_once];
    GLsizei held = 0;
    guests_.for_each([&](uint32_t host, uint32_t &) {
        hosts[held++] = host;
        if (held < deleted_at_once) return;
        if (gl_.glDeleteFramebuffers != nullptr) gl_.glDeleteFramebuffers(held, hosts);
        held = 0;
    });
    if (held != 0 && gl_.glDeleteFramebuffers != nullptr) gl_.glDeleteFramebuffers(held, hosts);
    forget_all();
}

void GlFramebufferNames::forget_all()
{
    AV_SCOPE("GlFramebufferNames::forget_all");
    guests_.clear();
    hosts_.clear();
    next_ = 1u;
}

uint32_t GlFramebufferNames::count() const
{
    AV_SCOPE("GlFramebufferNames::count");
    return hosts_.count();
}

bool GlFramebufferNames::invariants_hold() const
{
    AV_SCOPE("GlFramebufferNames::invariants_hold");
    if (hosts_.count() != guests_.count() || next_ == 0u) return false;
    bool paired = true;
    hosts_.for_each([&](uint32_t guest, const uint32_t &host) {
        const uint32_t *const back = guests_.find(host);
        paired = paired && guest != 0u && host != 0u && back != nullptr && *back == guest;
    });
    return paired && hosts_.invariants_hold() && guests_.invariants_hold();
}

bool GlFramebufferNames::admit(GLuint guest, GLuint host)
{
    AV_SCOPE("GlFramebufferNames::admit");
    uint32_t slot = 0u;
    if (hosts_.insert(guest, host, slot) != Status::ok) return false;
    if (guests_.insert(host, guest, slot) == Status::ok) return true;
    static_cast<void>(hosts_.erase(guest));
    return false;
}

GLuint GlFramebufferNames::fresh_guest()
{
    AV_SCOPE("GlFramebufferNames::fresh_guest");
    for (uint32_t tried = 0u; tried <= hosts_.count(); tried++) {
        const GLuint guest = next_;
        next_ = next_ + 1u != 0u ? next_ + 1u : 1u;
        if (hosts_.find(guest) == nullptr) return guest;
    }
    return 0u;
}

void GlFramebufferNames::give_back(GLuint host)
{
    AV_SCOPE("GlFramebufferNames::give_back");
    if (gl_.glDeleteFramebuffers != nullptr) gl_.glDeleteFramebuffers(1, &host);
}

}
