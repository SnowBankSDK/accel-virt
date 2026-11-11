#include "harness/framework/test_registry.hpp"
#include "icd/d3d/feature_level_gate.hpp"

#include <cstdint>

using av::d3d::FeatureLevelGate;
using av::d3d::FeatureLevelRequirements;

namespace {

FeatureLevelRequirements all_of_12_1()
{
    return FeatureLevelRequirements{FeatureLevelGate::level_12_1, 2u, true, true, 1u, true, true};
}

std::uint32_t level_of(const FeatureLevelRequirements &have) { return FeatureLevelGate(have).highest(); }

}

TEST(feature_level_gate, a_host_that_meets_every_requirement_reaches_12_1)
{ CHECK_EQ(level_of(all_of_12_1()), FeatureLevelGate::level_12_1); }

TEST(feature_level_gate, each_requirement_of_12_1_missing_leaves_12_0)
{ FeatureLevelRequirements have = all_of_12_1();
  have.rovs = false;
  CHECK_EQ(level_of(have), FeatureLevelGate::level_12_0);
  have = all_of_12_1();
  have.conservative_rasterization_tier = 0u;
  CHECK_EQ(level_of(have), FeatureLevelGate::level_12_0);
  have = all_of_12_1();
  have.vp_rt_array_without_gs = false;
  CHECK_EQ(level_of(have), FeatureLevelGate::level_12_0);
  have = all_of_12_1();
  have.host_level = FeatureLevelGate::level_12_0;
  CHECK_EQ(level_of(have), FeatureLevelGate::level_12_0); }

TEST(feature_level_gate, each_requirement_of_12_0_missing_leaves_11_1)
{ FeatureLevelRequirements have = all_of_12_1();
  have.tiled_tier = 1u;
  CHECK_EQ(level_of(have), FeatureLevelGate::level_11_1);
  have = all_of_12_1();
  have.typed_uav_load_additional_formats = false;
  CHECK_EQ(level_of(have), FeatureLevelGate::level_11_1);
  have = all_of_12_1();
  have.binding_tier_met = false;
  CHECK_EQ(level_of(have), FeatureLevelGate::level_11_1); }

TEST(feature_level_gate, an_11_x_host_never_yields_a_12_x_level)
{ FeatureLevelRequirements have = all_of_12_1();
  have.host_level = FeatureLevelGate::level_11_1;
  CHECK_EQ(level_of(have), FeatureLevelGate::level_11_1);
  have.host_level = FeatureLevelGate::level_11_0;
  CHECK_EQ(level_of(have), FeatureLevelGate::level_11_0);
  have.host_level = 0xa100u;
  CHECK_EQ(level_of(have), 0u); }
