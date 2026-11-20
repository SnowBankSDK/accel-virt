#include "device/memory/host_blob_errand.hpp"

#include "core/trace/function_scope.hpp"
#include "device/context/context_plane.hpp"
#include "device/execution/context_executor.hpp"
#include "device/memory/host_blob_table.hpp"

namespace av {

HostBlobErrand::HostBlobErrand(HostBlobTable &table, ContextPlane &plane, Kind kind, uint32_t id, uint32_t serial)
    : table_(table), plane_(plane), placer_(nullptr), kind_(kind), id_(id), serial_(serial), context_(0u),
      request_(), size_(0u), ticket_(0u), offset_(0u), entries_(0u), holders_{}, holder_count_(0u),
      status_(Status::invalid_state), memory_{-1, 0u, 0u}
{
    AV_SCOPE("HostBlobErrand::HostBlobErrand");
}

void HostBlobErrand::ask(uint32_t context, HostMemoryRequest request, uint64_t size, uint32_t ticket)
{
    AV_SCOPE("HostBlobErrand::ask");
    context_ = context;
    request_ = request;
    size_ = size;
    ticket_ = ticket;
}

void HostBlobErrand::lay(size_t offset, uint32_t entries)
{
    AV_SCOPE("HostBlobErrand::lay");
    offset_ = offset;
    entries_ = entries;
}

bool HostBlobErrand::alias_from(ContextPlane &holder, uint64_t key)
{
    AV_SCOPE("HostBlobErrand::alias_from");
    if (holder_count_ == most_holders) return false;
    holders_[holder_count_++] = Holder{&holder, key};
    return true;
}

void HostBlobErrand::place_through(ContextExecutor &executor)
{
    AV_SCOPE("HostBlobErrand::place_through");
    placer_ = &executor;
}

void HostBlobErrand::run()
{
    AV_SCOPE("HostBlobErrand::run");
    switch (kind_) {
    case Kind::create:
        status_ = placer_ != nullptr ? placer_->place(size_, request_.encoded(), memory_)
                                     : plane_.allocate_memory(context_, id_, size_, request_.encoded(), memory_);
        break;
    case Kind::lend: status_ = plane_.allocate_memory(context_, id_, size_, request_.encoded(), memory_); break;
    case Kind::alias: run_alias(); break;
    }
}

void HostBlobErrand::run_alias()
{
    AV_SCOPE("HostBlobErrand::run_alias");
    status_ = Status::no_memory;
    for (uint32_t index = 0u; index < holder_count_ && status_ != Status::ok; index++) {
        memory_ = AvHostMemory{-1, 0u, 0u};
        status_ = plane_.alias_memory(id_, *holders_[index].plane_, holders_[index].key_, memory_);
    }
}

void HostBlobErrand::finish(bool ran)
{
    AV_SCOPE("HostBlobErrand::finish");
    table_.errand_finished(*this, ran);
}

HostBlobErrand::Kind HostBlobErrand::kind() const
{
    AV_SCOPE("HostBlobErrand::kind");
    return kind_;
}

uint32_t HostBlobErrand::id() const
{
    AV_SCOPE("HostBlobErrand::id");
    return id_;
}

uint32_t HostBlobErrand::serial() const
{
    AV_SCOPE("HostBlobErrand::serial");
    return serial_;
}

uint32_t HostBlobErrand::context() const
{
    AV_SCOPE("HostBlobErrand::context");
    return context_;
}

HostMemoryRequest HostBlobErrand::request() const
{
    AV_SCOPE("HostBlobErrand::request");
    return request_;
}

uint64_t HostBlobErrand::size() const
{
    AV_SCOPE("HostBlobErrand::size");
    return size_;
}

uint32_t HostBlobErrand::ticket() const
{
    AV_SCOPE("HostBlobErrand::ticket");
    return ticket_;
}

size_t HostBlobErrand::offset() const
{
    AV_SCOPE("HostBlobErrand::offset");
    return offset_;
}

uint32_t HostBlobErrand::entries() const
{
    AV_SCOPE("HostBlobErrand::entries");
    return entries_;
}

bool HostBlobErrand::places() const
{
    AV_SCOPE("HostBlobErrand::places");
    return kind_ == Kind::create && request_.places();
}

Status HostBlobErrand::status() const
{
    AV_SCOPE("HostBlobErrand::status");
    return status_;
}

const AvHostMemory &HostBlobErrand::memory() const
{
    AV_SCOPE("HostBlobErrand::memory");
    return memory_;
}

}
