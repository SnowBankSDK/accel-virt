#ifndef AV_FEATURE_LEVEL_GATE_HPP
#define AV_FEATURE_LEVEL_GATE_HPP

#include <stdint.h>

namespace av::d3d {

struct FeatureLevelRequirements {
    uint32_t host_level;
    uint32_t tiled_tier;
    bool     typed_uav_load_additional_formats;
    bool     rovs;
    uint32_t conservative_rasterization_tier;
    bool     binding_tier_met;
    bool     vp_rt_array_without_gs;
};

class FeatureLevelGate {
public:
    static constexpr uint32_t level_11_0 = 0xb000u;
    static constexpr uint32_t level_11_1 = 0xb100u;
    static constexpr uint32_t level_12_0 = 0xc000u;
    static constexpr uint32_t level_12_1 = 0xc100u;

    explicit FeatureLevelGate(const FeatureLevelRequirements &requirements);

    uint32_t highest() const;

private:
    FeatureLevelRequirements requirements_;
};

}

#endif
