#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace mw34::submission {
// Scene-owned payloads keep empty emission arrays out of ordinary draw records.
// Indices survive vector growth; no pointer into a mutable asset is retained.
class DrawEmissions {
  public:
    using Float4 = std::array<float,4>;
    static constexpr std::uint32_t none = std::numeric_limits<std::uint32_t>::max();
  private:
    struct Payload { Float4 params{};std::array<Float4,32> regions{}; };
    std::vector<Payload> values;
  public:
    std::uint32_t capture(const Float4& params,std::span<const Float4> regions) {
        if (regions.size()>32 || values.size()>=none)
            throw std::runtime_error("Draw emission payload outside range");
        const auto index=std::uint32_t(values.size());
        auto& value=values.emplace_back();
        value.params=params;
        std::copy(regions.begin(),regions.end(),value.regions.begin());
        return index;
    }
    template<class Uniform> void apply(std::uint32_t index,Uniform& uniform) const {
        if (index==none) { uniform.emission_params={};uniform.emission_regions={};return; }
        const auto& value=values.at(index);
        uniform.emission_params=value.params;uniform.emission_regions=value.regions;
    }
    std::size_t size() const { return values.size(); }
};
} // namespace mw34::submission
