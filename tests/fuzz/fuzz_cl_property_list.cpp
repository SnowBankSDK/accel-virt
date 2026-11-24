#include "icd/opencl/cl_property_list.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size);

namespace {

using av::clicd::ClPropertyList;

constexpr std::size_t   words = ClPropertyList::max_pairs * 2u + 1u;
constexpr std::size_t   guard_words = ClPropertyList::max_pairs * 2u + ClPropertyList::most_list_values + 1u;
constexpr std::uint64_t canary = 0xDEADBEEFCAFEF00Dull;

void require(bool holds)
{
    if (!holds) __builtin_trap();
}

}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    static std::uint64_t list[words + guard_words];
    std::size_t          given = size / sizeof(std::uint64_t);
    if (given > words) given = words;
    std::memcpy(list, data, given * sizeof(std::uint64_t));
    for (std::size_t at = given; at < words + guard_words; at++) list[at] = canary;

    ClPropertyList read;
    const bool     ended = read.read(list);
    require(read.invariants_hold());
    if (!ended) {
        require(read.pairs() == 0u);
    } else {
        require(read.pairs() <= ClPropertyList::max_pairs);
        std::size_t at = 0u;
        for (std::uint32_t index = 0u; index < read.pairs(); index++) {
            require(read.name_at(index) == list[at]);
            require(read.value_at(index) == list[at + 1u]);
            require(read.name_at(index) != 0u);
            at += 2u;
            if (read.name_at(index) == ClPropertyList::device_handle_list)
                while (list[at - 1u] != 0u) at++;
            std::uint64_t value = canary;
            require(read.names(read.name_at(index), value));
            std::uint32_t first = 0u;
            while (read.name_at(first) != read.name_at(index)) first++;
            require(value == read.value_at(first));
            require(read.repeated() || first == index);
        }
        require(at < words + guard_words);
        require(list[at] == 0u);
    }
    for (std::size_t at = (given > words ? words : given); at < words + guard_words; at++)
        require(list[at] == canary);
    std::uint64_t value = canary;
    require(!read.names(0u, value) && value == canary);
    return 0;
}
