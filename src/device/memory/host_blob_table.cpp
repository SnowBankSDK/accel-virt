#include "device/memory/host_blob_table.hpp"
#include "core/support/sorted_search.hpp"

#include "core/support/scope_exit.hpp"
#include "core/trace/function_scope.hpp"
#include "core/trace/state_writer.hpp"
#include "device/context/context_plane.hpp"
#include "device/control/control_request.hpp"
#include "device/control/held_requests.hpp"
#include "device/host/host.hpp"
#include "device/memory/host_memory_free.hpp"
#include "device/recording/wire_recorder.hpp"
#include "proto/wire/wire_reader.hpp"

namespace av {

HostBlobTable::HostBlobTable(Host &host)
    : host_(host), contexts_(nullptr), service_(nullptr), held_(nullptr), recorder_(nullptr), space_(host),
      own_lendings_(host), lendings_(&own_lendings_), lock_(nullptr), blobs_(host), errands_(host), serial_(0u),
      ids_(host, Pool::paged), placed_(host, Pool::paged)
{
    AV_SCOPE("HostBlobTable::HostBlobTable");
}

HostBlobTable::~HostBlobTable()
{
    AV_SCOPE("HostBlobTable::~HostBlobTable");
    for (uint32_t slot = ids_.next_live(0u); slot != ids_.no_slot; slot = ids_.next_live(slot + 1u))
        drop(ids_.key_at(slot));
    if (lock_ != nullptr) host_.destroy_mutex(lock_);
}

Status HostBlobTable::initialize(ContextPlane &contexts, ServiceThread &service, HeldRequests &held,
                                 WireRecorder &recorder)
{
    AV_SCOPE("HostBlobTable::initialize");
    host_.require_main_loop("HostBlobTable::initialize");
    if (lock_ != nullptr) return Status::invalid_state;
    if (host_.create_mutex(lock_) != Status::ok) return Status::no_memory;
    contexts_ = &contexts;
    service_ = &service;
    held_ = &held;
    recorder_ = &recorder;
    return Status::ok;
}

HostBlob *HostBlobTable::blob_of(uint32_t id)
{
    AV_SCOPE("HostBlobTable::blob_of");
    HostBlob *const *const found = ids_.find(id);
    return found != nullptr ? *found : nullptr;
}

const HostBlob *HostBlobTable::blob_of(uint32_t id) const
{
    AV_SCOPE("HostBlobTable::blob_of");
    HostBlob *const *const found = ids_.find(id);
    return found != nullptr ? *found : nullptr;
}

uint32_t HostBlobTable::placed_from(uint64_t offset) const
{
    AV_SCOPE("HostBlobTable::placed_from");
    return SortedSearch<Placed, &Placed::offset_>(placed_).at_or_after(offset);
}

bool HostBlobTable::overlaps_placed(uint64_t offset, uint64_t size) const
{
    AV_SCOPE("HostBlobTable::overlaps_placed");
    const uint32_t next = placed_from(offset);
    if (next < placed_.size() && placed_.at(next).offset_ - offset < size) return true;
    return next != 0u && placed_.at(next - 1u).size_ > offset - placed_.at(next - 1u).offset_;
}

void HostBlobTable::unplace(uint64_t offset)
{
    AV_SCOPE("HostBlobTable::unplace");
    const uint32_t at = placed_from(offset);
    if (at < placed_.size() && placed_.at(at).offset_ == offset) placed_.remove_at(at);
}

void HostBlobTable::drop(uint32_t id)
{
    AV_SCOPE("HostBlobTable::drop");
    host_.lock_mutex(lock_);
    HostBlob *blob = nullptr;
    if (ids_.take(id, blob)) {
        if (blob->mapped()) unplace(blob->offset());
        if (blob->lending() != nullptr) lendings_->leave(host_, *blob->lending(), *contexts_, id);
        static_cast<void>(blobs_.destroy(blob));
    }
    host_.unlock_mutex(lock_);
}

void HostBlobTable::destroy(uint32_t id)
{
    AV_SCOPE("HostBlobTable::destroy");
    drop(id);
    forget(id);
}

void HostBlobTable::forget(uint32_t id)
{
    AV_SCOPE("HostBlobTable::forget");
    static_cast<void>(HostMemoryFree::hand_off(host_, *contexts_, *service_, id));
}

uint32_t HostBlobTable::next_serial()
{
    AV_SCOPE("HostBlobTable::next_serial");
    serial_ = serial_ == UINT32_MAX ? 1u : serial_ + 1u;
    return serial_;
}

Status HostBlobTable::create(uint32_t id, uint32_t context, HostMemoryRequest request, uint64_t size,
                             uint32_t ticket)
{
    AV_SCOPE("HostBlobTable::create");
    host_.require_main_loop("HostBlobTable::create");
    if (lock_ == nullptr) return Status::invalid_state;
    if (!HostBlobId(id).valid()) return Status::not_found;
    if (blob_of(id) != nullptr) return Status::conflict;
    if (!request.valid() || size == 0u || size % HostmemSpace::page_size != 0u) return Status::invalid_parameter;
    host_.lock_mutex(lock_);
    const Status room = ids_.reserve(ids_.count() + 1u);
    host_.unlock_mutex(lock_);
    if (room != Status::ok) return Status::no_memory;
    HostBlobErrand *errand = nullptr;
    if (errands_.create(errand, *this, *contexts_, HostBlobErrand::Kind::create, id, next_serial()) != Status::ok)
        return Status::no_memory;
    errand->ask(context, request, size, ticket);
    HostBlob *blob = nullptr;
    if (blobs_.create(blob, host_, context, AvHostMemory{-1, 0u, size}) != Status::ok) {
        static_cast<void>(errands_.destroy(errand));
        return Status::no_memory;
    }
    blob->await(errand->serial());
    uint32_t slot = 0u;
    host_.lock_mutex(lock_);
    static_cast<void>(ids_.insert(id, blob, slot));
    host_.unlock_mutex(lock_);
    if (contexts_->hand_memory(*errand, *service_) == Status::ok) return Status::ok;
    drop(id);
    static_cast<void>(errands_.destroy(errand));
    return Status::no_memory;
}

HostBlob *HostBlobTable::awaited(const HostBlobErrand &errand)
{
    AV_SCOPE("HostBlobTable::awaited");
    HostBlob *const blob = blob_of(errand.id());
    return blob != nullptr && blob->pending() && blob->serial() == errand.serial() ? blob : nullptr;
}

void HostBlobTable::conclude(const HostBlobErrand &errand, HostBlob *blob, Status status)
{
    AV_SCOPE("HostBlobTable::conclude");
    if (blob != nullptr && blob->doomed()) destroy(errand.id());
    held_->conclude(errand.ticket(), status);
}

void HostBlobTable::errand_finished(HostBlobErrand &errand, bool ran)
{
    AV_SCOPE("HostBlobTable::errand_finished");
    host_.require_main_loop("HostBlobTable::errand_finished");
    switch (errand.kind()) {
    case HostBlobErrand::Kind::create: settle_create(errand, ran); break;
    case HostBlobErrand::Kind::lend: settle_lend(errand, ran); break;
    case HostBlobErrand::Kind::alias: settle_alias(errand, ran); break;
    }
    static_cast<void>(errands_.destroy(&errand));
}

void HostBlobTable::settle_create(HostBlobErrand &errand, bool ran)
{
    AV_SCOPE("HostBlobTable::settle_create");
    HostBlob *const     blob = awaited(errand);
    const AvHostMemory &memory = errand.memory();
    const bool          allocated = ran && errand.status() == Status::ok;
    const bool          cached = memory.caching == AV_MEMORY_WRITE_BACK || memory.caching == AV_MEMORY_WRITE_COMBINED;
    if (blob == nullptr || !allocated || memory.fd < 0 || memory.size < errand.size() || !cached) {
        if (allocated && memory.fd >= 0) host_.close_descriptor(memory.fd);
        if (allocated && !errand.places()) forget(errand.id());
        if (blob != nullptr) drop(errand.id());
        held_->conclude(errand.ticket(), Status::no_memory);
        return;
    }
    host_.lock_mutex(lock_);
    blob->fill(AvHostMemory{memory.fd, memory.caching, errand.size()});
    host_.unlock_mutex(lock_);
    recorder_->record_blob(errand.context(), BlobAction::created, 0u, errand.id(), 0u, errand.size());
    conclude(errand, blob, Status::ok);
}

Status HostBlobTable::create_over_guest(uint32_t id, uint32_t context, HostMemoryRequest request, uint64_t size)
{
    AV_SCOPE("HostBlobTable::create_over_guest");
    host_.require_main_loop("HostBlobTable::create_over_guest");
    if (lock_ == nullptr) return Status::invalid_state;
    if (!HostBlobId(id).valid()) return Status::not_found;
    if (blob_of(id) != nullptr) return Status::conflict;
    if (!request.valid() || size == 0u || size % HostmemSpace::page_size != 0u) return Status::invalid_parameter;
    host_.lock_mutex(lock_);
    ScopeExit unlock([this] { host_.unlock_mutex(lock_); });
    if (ids_.reserve(ids_.count() + 1u) != Status::ok) return Status::no_memory;
    HostBlob *blob = nullptr;
    if (blobs_.create(blob, host_, context, size, request) != Status::ok) return Status::no_memory;
    uint32_t slot = 0u;
    static_cast<void>(ids_.insert(id, blob, slot));
    return Status::ok;
}

void HostBlobTable::share_lendings(GuestRamLendings &lendings)
{
    AV_SCOPE("HostBlobTable::share_lendings");
    host_.require_main_loop("HostBlobTable::share_lendings");
    lendings_ = &lendings;
}

bool HostBlobTable::waits_for_backing(uint32_t id) const
{
    AV_SCOPE("HostBlobTable::waits_for_backing");
    const HostBlob *const blob = blob_of(id);
    return blob != nullptr && blob->waiting() && !blob->pending();
}

Status HostBlobTable::adopt(uint32_t id, ControlRequest &request, size_t offset, uint32_t entries)
{
    AV_SCOPE("HostBlobTable::adopt");
    host_.require_main_loop("HostBlobTable::adopt");
    HostBlob *const blob = blob_of(id);
    if (blob == nullptr) return Status::not_found;
    if (blob->pending() || blob->waiting()) return Status::invalid_state;
    GuestRamLending *const lending = blob->lending();
    if (lending == nullptr) return Status::invalid_state;
    if (blob->lender()) return lending->lay(host_, request, offset, entries);
    uint32_t next = blob->entries_laid();
    const Status verified = lending->verify(request, offset, entries, next);
    if (verified == Status::ok) blob->laid(entries);
    return verified;
}

Status HostBlobTable::begin_adopting(uint32_t id, ControlRequest &request, size_t offset, uint32_t entries,
                                     uint32_t ticket)
{
    AV_SCOPE("HostBlobTable::begin_adopting");
    host_.require_main_loop("HostBlobTable::begin_adopting");
    HostBlob *const blob = blob_of(id);
    if (blob == nullptr) return Status::not_found;
    if (!blob->waiting() || blob->pending()) return Status::invalid_state;
    uint8_t entry[GuestPageOverlay::entry_size] = {};
    if (request.read(offset, entry, sizeof entry) != sizeof entry) return Status::invalid_parameter;
    WireReader             reader(entry, sizeof entry);
    const uint64_t         first_address = reader.read_u64();
    GuestRamLending *const joined = lendings_->join(first_address, blob->size(), *contexts_, id);
    HostBlobErrand        *errand = nullptr;
    const HostBlobErrand::Kind kind = joined != nullptr ? HostBlobErrand::Kind::alias : HostBlobErrand::Kind::lend;
    if (errands_.create(errand, *this, *contexts_, kind, id, next_serial()) != Status::ok) {
        if (joined != nullptr) lendings_->leave(host_, *joined, *contexts_, id);
        return Status::no_memory;
    }
    errand->ask(blob->context(), blob->request(), blob->size(), ticket);
    errand->lay(offset, entries);
    if (joined != nullptr) {
        ContextPlane *holder = nullptr;
        uint64_t      holder_key = 0u;
        for (uint32_t tried = 0u; tried < joined->holders(); tried++) {
            static_cast<void>(joined->holder_at(tried, holder, holder_key));
            if (holder != contexts_ && !errand->alias_from(*holder, holder_key)) break;
        }
        blob->join(*joined);
    }
    blob->await(errand->serial());
    if (contexts_->hand_memory(*errand, *service_) == Status::ok) return Status::ok;
    if (joined != nullptr) lendings_->leave(host_, *joined, *contexts_, id);
    blob->settle();
    static_cast<void>(errands_.destroy(errand));
    return Status::no_memory;
}

Status HostBlobTable::lay_kept(const HostBlobErrand &errand, GuestRamLending &lending)
{
    AV_SCOPE("HostBlobTable::lay_kept");
    void *const kept = held_->kept(errand.ticket());
    if (kept == nullptr) return Status::invalid_state;
    ControlRequest request(host_, kept);
    return lending.lay(host_, request, errand.offset(), errand.entries());
}

Status HostBlobTable::verify_kept(const HostBlobErrand &errand, HostBlob &blob, GuestRamLending &lending)
{
    AV_SCOPE("HostBlobTable::verify_kept");
    void *const kept = held_->kept(errand.ticket());
    if (kept == nullptr) return Status::invalid_state;
    ControlRequest request(host_, kept);
    uint32_t       next = blob.entries_laid();
    const Status   verified = lending.verify(request, errand.offset(), errand.entries(), next);
    if (verified == Status::ok) blob.laid(errand.entries());
    return verified;
}

void HostBlobTable::settle_lend(HostBlobErrand &errand, bool ran)
{
    AV_SCOPE("HostBlobTable::settle_lend");
    HostBlob *const     blob = awaited(errand);
    const AvHostMemory &memory = errand.memory();
    const bool          allocated = ran && errand.status() == Status::ok;
    const bool          cached = memory.caching == AV_MEMORY_WRITE_BACK;
    if (blob == nullptr || !allocated || memory.fd < 0 || memory.size < errand.size() || !cached) {
        if (allocated && memory.fd >= 0) host_.close_descriptor(memory.fd);
        if (allocated) forget(errand.id());
        if (blob != nullptr) blob->settle();
        conclude(errand, blob, allocated && !cached ? Status::not_supported : Status::no_memory);
        return;
    }
    GuestRamLending *const lending = lendings_->lend(host_, errand.size(), memory.fd, *contexts_, errand.id());
    if (lending == nullptr) {
        forget(errand.id());
        blob->settle();
        conclude(errand, blob, Status::no_memory);
        return;
    }
    blob->lend(*lending, true, AvHostMemory{memory.fd, memory.caching, errand.size()});
    conclude(errand, blob, lay_kept(errand, *lending));
}

void HostBlobTable::settle_alias(HostBlobErrand &errand, bool ran)
{
    AV_SCOPE("HostBlobTable::settle_alias");
    HostBlob *const     blob = awaited(errand);
    const AvHostMemory &memory = errand.memory();
    const bool          aliased = ran && errand.status() == Status::ok;
    if (blob == nullptr || blob->lending() == nullptr) {
        if (aliased) forget(errand.id());
        held_->conclude(errand.ticket(), Status::no_memory);
        return;
    }
    GuestRamLending &joined = *blob->lending();
    if (!aliased || memory.size < errand.size() || memory.caching != AV_MEMORY_WRITE_BACK) {
        if (aliased) forget(errand.id());
        lendings_->leave(host_, joined, *contexts_, errand.id());
        blob->settle();
        conclude(errand, blob, aliased ? Status::not_supported : Status::no_memory);
        return;
    }
    blob->lend(joined, false, memory);
    conclude(errand, blob, verify_kept(errand, *blob, joined));
}

Status HostBlobTable::map(uint32_t id, uint64_t offset, uint32_t &caching)
{
    AV_SCOPE("HostBlobTable::map");
    host_.require_main_loop("HostBlobTable::map");
    HostBlob *const blob = blob_of(id);
    if (blob == nullptr) return Status::not_found;
    const uint64_t size = blob->size();
    if (blob->mapped() || !space_.admits(offset, size) || overlaps_placed(offset, size))
        return Status::invalid_parameter;
    if (placed_.reserve(placed_.size() + 1u) != Status::ok) return Status::no_memory;
    const Status mapped = blob->map(offset);
    if (mapped != Status::ok) return mapped;
    static_cast<void>(placed_.insert_at(placed_from(offset), Placed{offset, size}));
    caching = blob->caching();
    return Status::ok;
}

Status HostBlobTable::unmap(uint32_t id)
{
    AV_SCOPE("HostBlobTable::unmap");
    host_.require_main_loop("HostBlobTable::unmap");
    HostBlob *const blob = blob_of(id);
    if (blob == nullptr) return Status::not_found;
    const uint64_t offset = blob->offset();
    if (blob->unmap() != Status::ok) return Status::invalid_parameter;
    unplace(offset);
    return Status::ok;
}

Status HostBlobTable::release(uint32_t id)
{
    AV_SCOPE("HostBlobTable::release");
    host_.require_main_loop("HostBlobTable::release");
    HostBlob *const blob = blob_of(id);
    if (blob == nullptr) return Status::not_found;
    if (blob->pending()) {
        blob->doom();
        return Status::ok;
    }
    destroy(id);
    return Status::ok;
}

void HostBlobTable::release_all()
{
    AV_SCOPE("HostBlobTable::release_all");
    host_.require_main_loop("HostBlobTable::release_all");
    for (uint32_t slot = ids_.next_live(0u); slot != ids_.no_slot; slot = ids_.next_live(slot + 1u)) {
        const uint32_t        id = ids_.key_at(slot);
        const HostBlob *const blob = blob_of(id);
        if (blob != nullptr && blob->pending()) drop(id);
        else destroy(id);
    }
}

bool HostBlobTable::contains(uint32_t id) const
{
    AV_SCOPE("HostBlobTable::contains");
    return blob_of(id) != nullptr;
}

bool HostBlobTable::owns(uint32_t context, uint32_t id)
{
    AV_SCOPE("HostBlobTable::owns");
    if (lock_ == nullptr) return false;
    host_.lock_mutex(lock_);
    ScopeExit             unlock([this] { host_.unlock_mutex(lock_); });
    const HostBlob *const blob = blob_of(id);
    return blob != nullptr && blob->context() == context && !blob->pending();
}

uint32_t HostBlobTable::live() const
{
    AV_SCOPE("HostBlobTable::live");
    return ids_.count();
}

uint32_t HostBlobTable::pending() const
{
    AV_SCOPE("HostBlobTable::pending");
    return errands_.live();
}

void HostBlobTable::report(StateWriter &writer) const
{
    AV_SCOPE("HostBlobTable::report");
    writer.open("host_blobs", 0u);
    writer.number("live", live());
    ids_.for_each([&writer](uint32_t id, const HostBlob *blob) {
        writer.open("host_blob", id);
        writer.number("context", blob->context());
        writer.number("size", blob->size());
        writer.number("caching", blob->caching());
        writer.flag("mapped", blob->mapped());
        writer.number("offset", blob->offset());
        writer.flag("lent", blob->lending() != nullptr);
        writer.flag("lender", blob->lender());
        writer.flag("waiting", blob->waiting());
        writer.flag("pending", blob->pending());
        writer.close();
    });
    writer.close();
}

bool HostBlobTable::invariants_hold() const
{
    AV_SCOPE("HostBlobTable::invariants_hold");
    if (lock_ == nullptr && live() != 0u) return false;
    if (!ids_.invariants_hold() || !blobs_.invariants_hold() || blobs_.live() != ids_.count()) return false;
    for (uint32_t index = 1u; index < placed_.size(); index++) {
        const Placed &before = placed_.at(index - 1u);
        if (placed_.at(index).offset_ < before.offset_ + before.size_) return false;
    }
    uint32_t mapped = 0u;
    bool     found = true;
    ids_.for_each([this, &mapped, &found](uint32_t, const HostBlob *blob) {
        if (!blob->mapped()) return;
        mapped++;
        const uint32_t at = placed_from(blob->offset());
        found = found && at < placed_.size() && placed_.at(at).offset_ == blob->offset() &&
                placed_.at(at).size_ == blob->size();
    });
    return found && mapped == placed_.size();
}

}
