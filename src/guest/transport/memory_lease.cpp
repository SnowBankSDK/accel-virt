#include "guest/transport/memory_lease.hpp"

#include "core/trace/function_scope.hpp"
#include "proto/client/client_transport.hpp"
#include "proto/client/sync_call_scope.hpp"
#include "proto/submission/command_header.hpp"

namespace av {

MemoryLease::MemoryLease() : pools_{}, extensions_(0u)
{
    AV_SCOPE("MemoryLease::MemoryLease");
    reset();
}

void MemoryLease::reset()
{
    AV_SCOPE("MemoryLease::reset");
    for (uint32_t kind = 0u; kind < LeaseRecord::kinds; kind++)
        pools_[kind] = Pool{LeaseRecord::initial_bytes[kind], first_chunk};
}

bool MemoryLease::take(ClientTransport &transport, LeaseRecord::Kind kind, uint64_t bytes)
{
    AV_SCOPE("MemoryLease::take");
    if (bytes == 0u) return true;
    Pool &taken = pool(kind);
    if (draw(taken, bytes)) return true;
    const uint64_t chunk = __atomic_load_n(&taken.chunk_, __ATOMIC_RELAXED);
    const uint64_t granted = extend(transport, kind, bytes > chunk ? bytes : chunk, bytes);
    __atomic_fetch_add(&taken.left_, granted, __ATOMIC_ACQ_REL);
    const uint64_t doubled = chunk < largest_chunk / 2u ? chunk * 2u : largest_chunk;
    const uint64_t asked = bytes < largest_chunk ? bytes : largest_chunk;
    const uint64_t grown = asked > doubled ? asked : doubled;
    if (granted >= bytes) __atomic_store_n(&taken.chunk_, grown, __ATOMIC_RELAXED);
    return granted >= bytes && draw(taken, bytes);
}

bool MemoryLease::draw(Pool &taken, uint64_t bytes)
{
    AV_SCOPE("MemoryLease::draw");
    uint64_t left = __atomic_load_n(&taken.left_, __ATOMIC_ACQUIRE);
    do {
        if (left < bytes) return false;
    } while (!__atomic_compare_exchange_n(&taken.left_, &left, left - bytes, true, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
    return true;
}

void MemoryLease::give(ClientTransport &transport, LeaseRecord::Kind kind, uint64_t bytes)
{
    AV_SCOPE("MemoryLease::give");
    if (bytes == 0u) return;
    Pool          &given = pool(kind);
    const uint64_t left = __atomic_add_fetch(&given.left_, bytes, __ATOMIC_ACQ_REL);
    if (left > __atomic_load_n(&given.chunk_, __ATOMIC_RELAXED) * kept_chunks) settle(transport, kind, given);
}

void MemoryLease::settle(ClientTransport &transport, LeaseRecord::Kind kind, Pool &settled)
{
    AV_SCOPE("MemoryLease::settle");
    const uint64_t kept = __atomic_load_n(&settled.chunk_, __ATOMIC_RELAXED);
    uint64_t       left = __atomic_load_n(&settled.left_, __ATOMIC_ACQUIRE);
    do {
        if (left <= kept * kept_chunks) return;
    } while (!__atomic_compare_exchange_n(&settled.left_, &left, kept, true, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
    uint8_t record[LeaseRecord::encoded_size] = {};
    if (LeaseRecord(kind, left - kept, 0u).encode(record, sizeof record) != Status::ok) return;
    WireWriter request = transport.request_writer();
    request.copy_bytes(record, sizeof record);
    static_cast<void>(transport.send_async(CommandHeader::return_lease_id, request));
}

uint64_t MemoryLease::extend(ClientTransport &transport, LeaseRecord::Kind kind, uint64_t bytes, uint64_t least)
{
    AV_SCOPE("MemoryLease::extend");
    uint8_t record[LeaseRecord::encoded_size] = {};
    if (LeaseRecord(kind, bytes, least).encode(record, sizeof record) != Status::ok) return 0u;
    __atomic_fetch_add(&extensions_, 1u, __ATOMIC_RELAXED);
    WireWriter          request = transport.request_writer();
    WireReader          answer(nullptr, 0u);
    const SyncCallScope call(transport);
    request.copy_bytes(record, sizeof record);
    if (transport.send_sync(CommandHeader::extend_lease_id, request, answer) != TransportResult::ok) return 0u;
    if (answer.remaining() != LeaseRecord::answer_size) return 0u;
    const uint64_t granted = answer.read_u64();
    return granted <= bytes ? granted : bytes;
}

uint64_t MemoryLease::left(LeaseRecord::Kind kind) const
{
    AV_SCOPE("MemoryLease::left");
    const uint32_t index = static_cast<uint32_t>(kind);
    return index < LeaseRecord::kinds ? __atomic_load_n(&pools_[index].left_, __ATOMIC_ACQUIRE) : 0u;
}

uint64_t MemoryLease::extensions() const
{
    AV_SCOPE("MemoryLease::extensions");
    return __atomic_load_n(&extensions_, __ATOMIC_RELAXED);
}

MemoryLease::Pool &MemoryLease::pool(LeaseRecord::Kind kind)
{
    AV_SCOPE("MemoryLease::pool");
    const uint32_t index = static_cast<uint32_t>(kind);
    return pools_[index < LeaseRecord::kinds ? index : 0u];
}

bool MemoryLease::invariants_hold() const
{
    AV_SCOPE("MemoryLease::invariants_hold");
    for (const Pool &each : pools_)
        if (each.chunk_ < first_chunk || each.chunk_ > largest_chunk) return false;
    return true;
}

}
