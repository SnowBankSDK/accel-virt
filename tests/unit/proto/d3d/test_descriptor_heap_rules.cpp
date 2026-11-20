#include "harness/framework/test_registry.hpp"
#include "proto/d3d/descriptor_heap_rules.hpp"

using av::DescriptorHeapRules;

TEST(descriptor_heap_rules, only_the_four_heap_types_exist)
{ CHECK(DescriptorHeapRules::admits_type(0u));
  CHECK(DescriptorHeapRules::admits_type(3u));
  CHECK(!DescriptorHeapRules::admits_type(4u));
  CHECK(!DescriptorHeapRules::admits_type(77u)); }

TEST(descriptor_heap_rules, a_heap_holds_no_more_than_the_most_descriptors)
{ CHECK(DescriptorHeapRules::admits_count(0u));
  CHECK(DescriptorHeapRules::admits_count(DescriptorHeapRules::most_descriptors));
  CHECK(!DescriptorHeapRules::admits_count(DescriptorHeapRules::most_descriptors + 1u)); }
