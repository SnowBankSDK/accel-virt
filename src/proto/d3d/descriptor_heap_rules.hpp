#ifndef AV_DESCRIPTOR_HEAP_RULES_HPP
#define AV_DESCRIPTOR_HEAP_RULES_HPP

#include <stdint.h>

namespace av {

class DescriptorHeapRules {
public:
    static constexpr uint32_t types = 4u;
    static constexpr uint32_t most_descriptors = 1000000u;

    static bool admits_type(uint32_t type);
    static bool admits_count(uint32_t count);
};

}

#endif
