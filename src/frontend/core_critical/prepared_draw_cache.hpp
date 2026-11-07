#pragma once

#include "prepared_draw.hpp"
#include <cfenv>
#include <map>
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) ||              \
    defined(__i386__)
#include <xmmintrin.h>
#endif

namespace cc_mw3_re {
// PreparedModels owns immutable batches for the adapter's lifetime. Addresses
// identify candidate entries only; all mutable inputs are compared
// byte-exactly. Deformed batches bypass the cache because their vertex bytes
// can change.
class PreparedDrawCache {
public:
  static constexpr bool supports_control_state() {
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) ||              \
    defined(__i386__)
    return true;
#else
    return false;
#endif
  }
  struct Statistics {
    std::uint64_t hits{}, misses{}, bypasses{};
    std::size_t bytes{}, entries{};
  };
  explicit PreparedDrawCache(std::size_t maximum_bytes = 128 * 1024 * 1024,
                             std::size_t maximum_entries = 512)
      : maximum_bytes_(maximum_bytes), maximum_entries_(maximum_entries) {}

  std::shared_ptr<const PreparedSubmission>
  get(const PreparedBatchDraw &source,
      const ::platform::ModelDrawContext &context,
      const std::array<int, 4> &viewport, unsigned width, unsigned height,
      std::shared_ptr<const mw34::vkscene::ModelGeometry> geometry,
      bool lighting, PreparedDrawCpuStatistics *profile = nullptr,
      bool enabled = true) {
    const auto build = [&] {
      return std::make_shared<PreparedSubmission>(
          prepare_draw(source, context, viewport, width, height, geometry,
                       lighting, profile));
    };
    if (!enabled || !supports_control_state() || !source.deformed.empty() ||
        !maximum_entries_) {
      ++statistics_.bypasses;
      return build();
    }
    Signature signature{};
    std::size_t offset = 0;
    const auto append = [&](const auto &value) {
      std::memcpy(signature.data() + offset, &value, sizeof value);
      offset += sizeof value;
    };
    append(source.camera);
    append(source.world);
    append(context.near_z);
    append(context.clip_near);
    append(context.far_z);
    append(context.fx_near);
    append(context.fy_near);
    append(context.cx);
    append(context.cy);
    append(viewport);
    append(width);
    append(height);
    append(lighting);
    append(zmod::fog());
    append(zmod::fog_calc().camera_to_world);
    const int rounding = std::fegetround();
    append(rounding);
    std::uint32_t controls = 0;
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) ||              \
    defined(__i386__)
    // FTZ/DAZ and exception masks affect arithmetic too. Status flags do
    // not affect results and remain restored by the adapter's FP scope.
    controls = _mm_getcsr() & 0xffc0U;
#endif
    append(controls);
    const auto identity =
        std::make_pair(reinterpret_cast<std::uintptr_t>(source.batch),
                       reinterpret_cast<std::uintptr_t>(context.node));
    auto found = entries_.find(identity);
    if (found != entries_.end() && found->second.signature == signature &&
        found->second.geometry == geometry) {
      ++statistics_.hits;
      found->second.used = ++clock_;
      return found->second.value;
    }
    ++statistics_.misses;
    auto value = build();
    auto bytes =
        value->vertices.capacity() + sizeof(Entry) + sizeof(PreparedSubmission);
    if (value->draw.model)
      bytes += sizeof(*value->draw.model);
    if (value->draw.sun_tl)
      bytes += sizeof(*value->draw.sun_tl);
    if (value->draw.lit_normals)
      bytes +=
          sizeof(*value->draw.lit_normals) +
          value->draw.lit_normals->capacity() * sizeof(std::array<float, 3>);
    if (found != entries_.end()) {
      statistics_.bytes -= found->second.bytes;
      entries_.erase(found);
    }
    if (bytes <= maximum_bytes_) {
      while (!entries_.empty() &&
             (entries_.size() >= maximum_entries_ ||
              statistics_.bytes > maximum_bytes_ - bytes)) {
        auto oldest = std::min_element(entries_.begin(), entries_.end(),
                                       [](const auto &a, const auto &b) {
                                         return a.second.used < b.second.used;
                                       });
        statistics_.bytes -= oldest->second.bytes;
        entries_.erase(oldest);
      }
      entries_.emplace(identity,
                       Entry{signature, geometry, value, bytes, ++clock_});
      statistics_.bytes += bytes;
    }
    statistics_.entries = entries_.size();
    return value;
  }
  const Statistics &statistics() const { return statistics_; }

private:
  // Fog has no padding: both integers and every float occupy four bytes.
  static_assert(sizeof(zmod::Fog) == 56);
  using Signature =
      std::array<unsigned char,
                 2 * sizeof(::platform::prepared::Matrix) + 7 * sizeof(float) +
                     sizeof(std::array<int, 4>) + 2 * sizeof(unsigned) +
                     sizeof(bool) + sizeof(zmod::Fog) +
                     sizeof(zmod::FogCalc::camera_to_world) + sizeof(int) +
                     sizeof(std::uint32_t)>;
  struct Entry {
    Signature signature;
    std::shared_ptr<const mw34::vkscene::ModelGeometry> geometry;
    std::shared_ptr<const PreparedSubmission> value;
    std::size_t bytes;
    std::uint64_t used;
  };
  std::map<std::pair<std::uintptr_t, std::uintptr_t>, Entry> entries_;
  std::size_t maximum_bytes_, maximum_entries_;
  std::uint64_t clock_{};
  Statistics statistics_;
};
} // namespace cc_mw3_re
