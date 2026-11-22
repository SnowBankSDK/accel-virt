#include "icd/d3d/pipeline_library.hpp"

#include "core/platform/block_source.hpp"
#include "core/trace/function_scope.hpp"

namespace av::d3d {

namespace {

constexpr uint32_t first_capacity = 16u;
constexpr size_t   alignment = 8u;
constexpr uint32_t byte_bits = 8u;

uint64_t padded(uint64_t size)
{
    AV_SCOPE("padded");
    return (size + alignment - 1u) / alignment * alignment;
}

uint64_t read_le(const uint8_t *from, size_t bytes)
{
    AV_SCOPE("read_le");
    uint64_t value = 0u;
    for (size_t at = 0u; at < bytes; at++) value |= static_cast<uint64_t>(from[at]) << (at * byte_bits);
    return value;
}

void write_le(uint8_t *into, uint64_t value, size_t bytes)
{
    AV_SCOPE("write_le");
    for (size_t at = 0u; at < bytes; at++) into[at] = static_cast<uint8_t>(value >> (at * byte_bits));
}

}

PipelineLibrary::PipelineLibrary(BlockSource &memory) : memory_(memory), entries_(nullptr), count_(0u), capacity_(0u)
{
    AV_SCOPE("PipelineLibrary::PipelineLibrary");
}

PipelineLibrary::~PipelineLibrary()
{
    AV_SCOPE("PipelineLibrary::~PipelineLibrary");
    release_all();
}

void PipelineLibrary::release_all()
{
    AV_SCOPE("PipelineLibrary::release_all");
    for (uint32_t at = 0u; at < count_; at++) memory_.release(entries_[at].bytes_);
    if (entries_ != nullptr) memory_.release(entries_);
    entries_ = nullptr;
    count_ = 0u;
    capacity_ = 0u;
}

PipelineLibrary::Entry *PipelineLibrary::entry_of(uint32_t index) const
{
    AV_SCOPE("PipelineLibrary::entry_of");
    for (uint32_t at = 0u; at < count_; at++)
        if (entries_[at].index_ == index) return &entries_[at];
    return nullptr;
}

bool PipelineLibrary::grow()
{
    AV_SCOPE("PipelineLibrary::grow");
    if (count_ < capacity_) return true;
    if (capacity_ >= most_entries) return false;
    const uint32_t wanted = capacity_ == 0u ? first_capacity : capacity_ * 2u;
    Entry *const   bigger = static_cast<Entry *>(memory_.allocate(sizeof(Entry) * wanted, Pool::paged));
    if (bigger == nullptr) return false;
    for (uint32_t at = 0u; at < count_; at++) bigger[at] = entries_[at];
    if (entries_ != nullptr) memory_.release(entries_);
    entries_ = bigger;
    capacity_ = wanted;
    return true;
}

Hresult PipelineLibrary::add(uint32_t index, const uint8_t *blob, uint64_t size)
{
    AV_SCOPE("PipelineLibrary::add");
    if (blob == nullptr || size == 0u || size > most_blob_bytes) return hr_invalid_arg;
    uint8_t *const kept = static_cast<uint8_t *>(memory_.allocate(static_cast<size_t>(size), Pool::paged));
    if (kept == nullptr) return hr_out_of_memory;
    __builtin_memcpy(kept, blob, static_cast<size_t>(size));
    if (Entry *const existing = entry_of(index); existing != nullptr) {
        memory_.release(existing->bytes_);
        existing->bytes_ = kept;
        existing->size_ = size;
        return hr_ok;
    }
    if (!grow()) {
        memory_.release(kept);
        return hr_out_of_memory;
    }
    entries_[count_++] = Entry{index, size, kept};
    return hr_ok;
}

bool PipelineLibrary::find(uint32_t index, const uint8_t *&blob, uint64_t &size) const
{
    AV_SCOPE("PipelineLibrary::find");
    const Entry *const found = entry_of(index);
    if (found == nullptr) return false;
    blob = found->bytes_;
    size = found->size_;
    return true;
}

Hresult PipelineLibrary::open(const void *data, size_t bytes)
{
    AV_SCOPE("PipelineLibrary::open");
    release_all();
    if (bytes == 0u) return hr_ok;
    if (data == nullptr) return hr_d3d12_driver_version_mismatch;
    const Hresult parsed = parse(static_cast<const uint8_t *>(data), bytes);
    if (parsed != hr_ok) release_all();
    return parsed;
}

Hresult PipelineLibrary::parse(const uint8_t *data, size_t bytes)
{
    AV_SCOPE("PipelineLibrary::parse");
    if (bytes < first_header_bytes || read_le(data, 4u) != magic) return hr_d3d12_driver_version_mismatch;
    const uint64_t stated = read_le(data + 4u, 4u);
    if (stated == first_version) return bytes == first_header_bytes ? hr_ok : hr_d3d12_driver_version_mismatch;
    if (stated != version || bytes < header_bytes) return hr_d3d12_driver_version_mismatch;
    const uint64_t count = read_le(data + 8u, 4u);
    if (count > most_entries) return hr_d3d12_driver_version_mismatch;
    size_t at = header_bytes;
    for (uint64_t entry = 0u; entry < count; entry++) {
        if (bytes - at < entry_header_bytes) return hr_d3d12_driver_version_mismatch;
        const uint32_t index = static_cast<uint32_t>(read_le(data + at, 4u));
        const uint64_t size = read_le(data + at + 8u, 8u);
        at += entry_header_bytes;
        if (size == 0u || size > most_blob_bytes || padded(size) > bytes - at || entry_of(index) != nullptr)
            return hr_d3d12_driver_version_mismatch;
        const Hresult kept = add(index, data + at, size);
        if (kept != hr_ok) return kept;
        at += static_cast<size_t>(padded(size));
    }
    return at == bytes ? hr_ok : hr_d3d12_driver_version_mismatch;
}

size_t PipelineLibrary::serialized_size() const
{
    AV_SCOPE("PipelineLibrary::serialized_size");
    uint64_t total = header_bytes;
    for (uint32_t at = 0u; at < count_; at++) total += entry_header_bytes + padded(entries_[at].size_);
    return static_cast<size_t>(total);
}

void PipelineLibrary::serialize(void *destination) const
{
    AV_SCOPE("PipelineLibrary::serialize");
    uint8_t *const into = static_cast<uint8_t *>(destination);
    if (into == nullptr) return;
    __builtin_memset(into, 0, serialized_size());
    write_le(into, magic, 4u);
    write_le(into + 4u, version, 4u);
    write_le(into + 8u, count_, 4u);
    size_t at = header_bytes;
    for (uint32_t entry = 0u; entry < count_; entry++) {
        write_le(into + at, entries_[entry].index_, 4u);
        write_le(into + at + 8u, entries_[entry].size_, 8u);
        at += entry_header_bytes;
        __builtin_memcpy(into + at, entries_[entry].bytes_, static_cast<size_t>(entries_[entry].size_));
        at += static_cast<size_t>(padded(entries_[entry].size_));
    }
}

uint32_t PipelineLibrary::entries() const
{
    AV_SCOPE("PipelineLibrary::entries");
    return count_;
}

bool PipelineLibrary::invariants_hold() const
{
    AV_SCOPE("PipelineLibrary::invariants_hold");
    if (count_ > capacity_ || (capacity_ != 0u) != (entries_ != nullptr)) return false;
    for (uint32_t at = 0u; at < count_; at++) {
        if (entries_[at].bytes_ == nullptr || entries_[at].size_ == 0u) return false;
        for (uint32_t later = at + 1u; later < count_; later++)
            if (entries_[later].index_ == entries_[at].index_) return false;
    }
    return true;
}

}
