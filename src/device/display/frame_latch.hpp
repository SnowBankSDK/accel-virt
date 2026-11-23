#ifndef AV_FRAME_LATCH_HPP
#define AV_FRAME_LATCH_HPP

#include <stdint.h>

namespace av {

class FrameLatch {
public:
    FrameLatch();

    void     post(uint32_t frame, uint64_t key);
    void     post_exported(uint32_t frame, uint64_t key);
    bool     latch_to_show();
    uint32_t latched() const;
    bool     take_unshown(uint64_t &key);
    void     clear();
    uint64_t posts() const;
    uint64_t posts_exported_on_post() const;
    uint64_t posts_dropped() const;

    bool invariants_hold() const;

private:
    void replace_pending(uint32_t frame, uint64_t key);

    uint32_t pending_frame_;
    uint64_t pending_key_;
    bool     pending_exported_;
    uint32_t latched_frame_;
    uint64_t latched_key_;
    bool     unshown_;
    uint64_t posts_;
    uint64_t posts_exported_on_post_;
    uint64_t posts_dropped_;

    friend struct FrameLatchProbe;
};

}

#endif
