#ifndef AV_UM_ADAPTER_SESSIONS_HPP
#define AV_UM_ADAPTER_SESSIONS_HPP

#include "guest/kmt/adapter_slots.hpp"
#include "guest/kmt/kmt.hpp"
#include "core/log/driver_log.hpp"
#include "core/log/log_line.hpp"
#include "core/support/object_storage.hpp"
#include "core/support/scope_exit.hpp"
#include "guest/transport/guest_connection.hpp"
#include "icd/common/host_channel.hpp"
#include "icd/common/link_generations.hpp"
#include "proto/context/sub_api.hpp"
#include "core/trace/function_scope.hpp"
#include "um/common/srw_lock.hpp"
#include "um/d3d/d3d_session.hpp"
#include "um/d3d/slice_offer.hpp"

#include <stdint.h>

namespace av {

class BlockSource;
class Kmt;

template <typename Caps>
class AdapterSessions {
public:
    static constexpr uint32_t max_adapters = AdapterSlots::capacity;
    static constexpr uint32_t none = AdapterSlots::none;

    AdapterSessions(Kmt &kmt, BlockSource &memory, DriverLog &log, const char *refusal, uint64_t arena_bytes,
                    uint32_t select_command, uint32_t flush_command, const d3d::SliceOffer *offer);
    ~AdapterSessions();
    AdapterSessions(const AdapterSessions &) = delete;
    AdapterSessions &operator=(const AdapterSessions &) = delete;

    uint32_t         slot_of(uint32_t kmt_adapter, bool &learned);
    void             hold(uint32_t slot);
    void             release(const Caps &held);
    Caps            &caps(uint32_t slot);
    d3d::D3dSession *session(uint32_t slot);
    bool             exhausted() const;

private:
    static constexpr uint8_t  probe_ring_order = 16u;
    static constexpr uint32_t probe_reply_bytes = 64u * 1024u;
    static constexpr size_t   probe_staging_bytes = 16u * 1024u;

    [[nodiscard]] bool learn(Caps &caps, uint32_t kmt_adapter);
    void               renew(uint32_t slot);
    void               refuse(const char *cause, const char *label, uint32_t number);

    Kmt                           &kmt_;
    BlockSource                   &memory_;
    DriverLog                     &lines_;
    const char                    *refusal_;
    uint64_t                       arena_bytes_;
    uint32_t                       select_command_;
    uint32_t                       flush_command_;
    const d3d::SliceOffer         *offer_;
    ObjectStorage<Caps>            caps_[max_adapters];
    ObjectStorage<icd::LinkGenerations<d3d::D3dSession>> sessions_[max_adapters];
    AdapterSlots                   slots_;
    bool                           exhausted_;
    const char                    *learn_step_;
    Status                         learn_status_;
};

template <typename Caps>
AdapterSessions<Caps>::AdapterSessions(Kmt &kmt, BlockSource &memory, DriverLog &log, const char *refusal,
                                       uint64_t arena_bytes, uint32_t select_command,
                                       uint32_t flush_command, const d3d::SliceOffer *offer)
    : kmt_(kmt), memory_(memory), lines_(log), refusal_(refusal), arena_bytes_(arena_bytes),
      select_command_(select_command), flush_command_(flush_command), offer_(offer), caps_(),
      sessions_(), slots_(), exhausted_(false), learn_step_(""), learn_status_(Status::ok)
{
    AV_SCOPE("AdapterSessions::AdapterSessions");
    for (auto &slot : caps_) static_cast<void>(slot.construct());
    for (auto &slot : sessions_) static_cast<void>(slot.construct(memory));
}

template <typename Caps>
AdapterSessions<Caps>::~AdapterSessions()
{
    AV_SCOPE("AdapterSessions::~AdapterSessions");
    for (auto &slot : sessions_) slot.destroy();
    for (auto &slot : caps_) slot.destroy();
}

template <typename Caps>
uint32_t AdapterSessions<Caps>::slot_of(uint32_t kmt_adapter, bool &learned)
{
    AV_SCOPE("AdapterSessions::slot_of");
    learned = false;
    exhausted_ = false;
    uint64_t     luid = 0u;
    const Status named = kmt_.adapter_luid(kmt_adapter, luid);
    if (named != Status::ok || luid == 0u) {
        refuse("the adapter's LUID is unknown", "status", static_cast<uint32_t>(named));
        return none;
    }
    bool           fresh = false;
    const uint32_t slot = slots_.claim(luid, fresh);
    if (slot == none) {
        refuse("every adapter slot holds devices of another adapter", "slots", slots_.used());
        return none;
    }
    if (!fresh) return slot;
    renew(slot);
    if (!learn(caps_[slot].object(), kmt_adapter)) {
        slots_.vacate(slot);
        refuse(learn_step_, "status", static_cast<uint32_t>(learn_status_));
        return none;
    }
    learned = true;
    return slot;
}

template <typename Caps>
void AdapterSessions<Caps>::hold(uint32_t slot)
{
    AV_SCOPE("AdapterSessions::hold");
    slots_.hold(slot);
}

template <typename Caps>
void AdapterSessions<Caps>::release(const Caps &held)
{
    AV_SCOPE("AdapterSessions::release");
    for (uint32_t slot = 0u; slot < max_adapters; slot++) {
        if (&caps_[slot].object() != &held) continue;
        if (slots_.release(slot)) renew(slot);
        return;
    }
}

template <typename Caps>
void AdapterSessions<Caps>::renew(uint32_t slot)
{
    AV_SCOPE("AdapterSessions::renew");
    sessions_[slot].destroy();
    static_cast<void>(sessions_[slot].construct(memory_));
    caps_[slot].destroy();
    static_cast<void>(caps_[slot].construct());
}

template <typename Caps>
void AdapterSessions<Caps>::refuse(const char *cause, const char *label, uint32_t number)
{
    AV_SCOPE("AdapterSessions::refuse");
    LogLine(lines_).text(refusal_).text(": ").text(cause).text(", ").text(label).text(" ").number(number).write();
}

template <typename Caps>
bool AdapterSessions<Caps>::learn(Caps &caps, uint32_t kmt_adapter)
{
    AV_SCOPE("AdapterSessions::learn");
    GuestConnection connection(kmt_);
    learn_step_ = "the adapter would not open";
    learn_status_ = connection.open(kmt_adapter);
    if (learn_status_ != Status::ok) return false;
    ScopeExit        closed([&] { connection.close(); });
    SrwLock          unshared;
    icd::HostChannel probe(memory_, lines_,
                           icd::ChannelShape(SubApi::d3d, probe_ring_order, probe_reply_bytes, probe_staging_bytes,
                                             0u, 0u),
                           unshared);
    const Status opened = probe.open(connection);
    learn_step_ = "the probe context was refused";
    learn_status_ = opened;
    if (opened != Status::ok) {
        exhausted_ = opened == Status::no_memory;
        return false;
    }
    const bool learned = caps.learn(probe.transport(), kmt_, kmt_adapter);
    learn_step_ = "the host did not answer the capability queries";
    exhausted_ = !learned && probe.exhausted();
    probe.close(false);
    return learned;
}

template <typename Caps>
bool AdapterSessions<Caps>::exhausted() const
{
    AV_SCOPE("AdapterSessions::exhausted");
    return exhausted_;
}

template <typename Caps>
Caps &AdapterSessions<Caps>::caps(uint32_t slot)
{
    AV_SCOPE("AdapterSessions::caps");
    return caps_[slot].object();
}

template <typename Caps>
d3d::D3dSession *AdapterSessions<Caps>::session(uint32_t slot)
{
    AV_SCOPE("AdapterSessions::session");
    bool                   left_lost = false;
    d3d::D3dSession *const joined = sessions_[slot].object().join(left_lost, kmt_, memory_, lines_, arena_bytes_,
                                                                  select_command_, flush_command_, offer_);
    if (left_lost) lines_.write("the host faulted the adapter's session: new devices open one of their own");
    return joined;
}

}

#endif
