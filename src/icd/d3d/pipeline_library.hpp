#ifndef AV_D3D_PIPELINE_LIBRARY_HPP
#define AV_D3D_PIPELINE_LIBRARY_HPP

#include "icd/d3d/hresult.hpp"

#include <stddef.h>
#include <stdint.h>

namespace av {
class BlockSource;
}

namespace av::d3d {

class PipelineLibrary {
public:
    static constexpr uint32_t magic = 0x4C505641u;
    static constexpr uint32_t version = 2u;
    static constexpr uint32_t first_version = 1u;
    static constexpr size_t   header_bytes = 16u;
    static constexpr size_t   entry_header_bytes = 16u;
    static constexpr size_t   first_header_bytes = 8u;
    static constexpr uint32_t most_entries = 1u << 20;
    static constexpr uint64_t most_blob_bytes = 64ull << 20;

    explicit PipelineLibrary(BlockSource &memory);
    ~PipelineLibrary();
    PipelineLibrary(const PipelineLibrary &) = delete;
    PipelineLibrary &operator=(const PipelineLibrary &) = delete;

    Hresult open(const void *data, size_t bytes);
    Hresult add(uint32_t index, const uint8_t *blob, uint64_t size);
    bool    find(uint32_t index, const uint8_t *&blob, uint64_t &size) const;
    size_t  serialized_size() const;
    void    serialize(void *destination) const;
    uint32_t entries() const;

    bool invariants_hold() const;

private:
    struct Entry {
        uint32_t index_;
        uint64_t size_;
        uint8_t *bytes_;
    };

    Entry  *entry_of(uint32_t index) const;
    bool    grow();
    Hresult parse(const uint8_t *data, size_t bytes);
    void    release_all();

    BlockSource &memory_;
    Entry       *entries_;
    uint32_t     count_;
    uint32_t     capacity_;
};

}

#endif
