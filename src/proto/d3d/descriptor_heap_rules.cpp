#include "proto/d3d/descriptor_heap_rules.hpp"

#include "core/trace/function_scope.hpp"

namespace av {

bool DescriptorHeapRules::admits_type(uint32_t type)
{
    AV_SCOPE("DescriptorHeapRules::admits_type");
    return type < types;
}

bool DescriptorHeapRules::admits_count(uint32_t count)
{
    AV_SCOPE("DescriptorHeapRules::admits_count");
    return count <= most_descriptors;
}

}
