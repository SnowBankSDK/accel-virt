#include "icd/d3d/feature_level_gate.hpp"

#include "core/trace/function_scope.hpp"

namespace av::d3d {

namespace {
constexpr uint32_t tiled_tier_12_0 = 2u;
}

FeatureLevelGate::FeatureLevelGate(const FeatureLevelRequirements &requirements) : requirements_(requirements)
{
    AV_SCOPE("FeatureLevelGate::FeatureLevelGate");
}

uint32_t FeatureLevelGate::highest() const
{
    AV_SCOPE("FeatureLevelGate::highest");
    const FeatureLevelRequirements &have = requirements_;
    const bool twelve_0 = have.host_level >= level_12_0 && have.binding_tier_met && have.tiled_tier >= tiled_tier_12_0 &&
                          have.typed_uav_load_additional_formats;
    if (twelve_0 && have.host_level >= level_12_1 && have.conservative_rasterization_tier >= 1u && have.rovs &&
        have.vp_rt_array_without_gs)
        return level_12_1;
    if (twelve_0) return level_12_0;
    if (have.host_level >= level_11_1) return level_11_1;
    if (have.host_level >= level_11_0) return level_11_0;
    return 0u;
}

}
