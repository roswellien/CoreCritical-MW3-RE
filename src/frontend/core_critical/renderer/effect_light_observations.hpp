#pragma once
// WP04A unit4: presentation-only observations of original effect emitters.
// The cache owns value copies of Emitter/Point only; no node, material or
// game-state pointer is retained and the caller supplies every clock value.
// Ranking is not done here: rank_frame_lights consumes candidates() output.
#include "effect_light_catalog.hpp"
#include "effect_light_ranking.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace mw34::effects {

class ObservedLights {
  struct Entry {
    Emitter emitter;
    Point a, b;
    double observed;
    std::uint64_t source;
  };
  static constexpr size_t max_entries = 256;
  static constexpr double dedup2 = 0.75 * 0.75;

private:
  std::vector<Entry> entries_;
  double previous_time_{};
  std::uint64_t next_id_ = 1;

  static double squared(const Point &a, const Point &b) {
    double s = 0;
    for (size_t i = 0; i < 3; ++i) {
      const double q = static_cast<double>(a[i]) - static_cast<double>(b[i]);
      s += q * q;
    }
    return s;
  }

public:
  void clear() {
    entries_.clear();
    previous_time_ = 0;
    next_id_ = 1;
  }

private:
  void expire(double time) {
    if (!std::isfinite(time) || time < 0)
      throw std::runtime_error("ObservedLights: invalid time");
    if (time < previous_time_)
      clear();
    previous_time_ = time;
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&](const Entry &e) {
                                    return time - e.observed >
                                           static_cast<double>(
                                               e.emitter.lifetime);
                                  }),
                   entries_.end());
  }

public:
  size_t size() const { return entries_.size(); }

  std::vector<Candidate> candidates(double time) {
    expire(time);
    std::vector<Candidate> out;
    for (const Entry &e : entries_) {
      const double fade =
          std::clamp(1.0 - (time - e.observed) / e.emitter.lifetime, 0.0, 1.0);
      // Preserve the reference presentation gain without changing an
      // installed catalog or compounding gain on repeated observations.
      const bool ppc =
          e.emitter.kind == 3 ||
          (e.emitter.kind == 2 && e.emitter.color == Point{.15f, .4f, 1.f});
      const double power =
          e.emitter.intensity * (ppc ? 1.25 : 1.0) * fade * fade;
      if (!std::isfinite(power))
        throw std::runtime_error("ObservedLights: non-finite power");
      if (power <= 0.0001)
        continue;
      Candidate c;
      c.weapon = true;
      c.source = e.source;
      c.light.start = {e.a[0], e.a[1], e.a[2], e.emitter.radius};
      c.light.end = {e.b[0], e.b[1], e.b[2], e.emitter.kind == 1 ? 1.f : 0.f};
      c.light.color = {e.emitter.color[0], e.emitter.color[1],
                       e.emitter.color[2], static_cast<float>(power)};
      out.push_back(std::move(c));
    }
    return out;
  }

  void observe(const Emitter &e, const std::vector<Point> &points,
               double time) {
    const float radius = e.radius, intensity = e.intensity,
                lifetime = e.lifetime;
    if (e.kind < 1 || e.kind > 6 || !(lifetime > 0) ||
        !std::isfinite(lifetime) || !(radius > 0) || !std::isfinite(radius) ||
        !(intensity >= 0) || !std::isfinite(intensity))
      throw std::runtime_error("ObservedLights: invalid emitter");
    for (const float c : e.color)
      if (!(c >= 0) || !std::isfinite(c))
        throw std::runtime_error("ObservedLights: invalid color");
    if (points.empty() || points.size() > 1024)
      throw std::runtime_error("ObservedLights: invalid point count");
    for (const Point &p : points)
      for (const float v : p)
        if (!std::isfinite(v) || std::fabs(v) > 1e6)
          throw std::runtime_error("ObservedLights: invalid point");
    expire(time);
    Point a{}, b{};
    if (e.kind == 1 && points.size() > 1) {
      // Endpoints of the rendered beam in double; reject overlong beams
      // outright instead of fabricating an endpoint.
      const Point &first = points.front();
      double best = -1;
      for (const Point &p : points) {
        const double d = squared(first, p);
        if (d > best) {
          best = d;
          b = p;
        }
      }
      best = -1;
      for (const Point &p : points) {
        const double d = squared(b, p);
        if (d > best) {
          best = d;
          a = p;
        }
      }
      if (squared(a, b) > 4096.0 * 4096.0)
        return;
      if (b < a)
        std::swap(a, b); // lexicographic order keeps orientation stable
    } else {
      std::array<double, 3> sum{};
      for (const Point &p : points)
        for (size_t i = 0; i < 3; ++i)
          sum[i] += static_cast<double>(p[i]);
      for (size_t i = 0; i < 3; ++i)
        a[i] = static_cast<float>(sum[i] / points.size());
      b = a;
    }
    for (Entry &old : entries_) {
      // Move a matching source: preserve its identity instead of leaving
      // a trail of stationary lights behind it.
      if (old.emitter.kind == e.kind && old.emitter.color == e.color &&
          old.emitter.radius == radius && old.emitter.intensity == intensity &&
          old.emitter.lifetime == lifetime && squared(old.a, a) < dedup2 &&
          squared(old.b, b) < dedup2) {
        old = Entry{e, a, b, time, old.source};
        return;
      }
    }
    if (next_id_ == std::numeric_limits<std::uint64_t>::max())
      throw std::runtime_error("ObservedLights: source id exhausted");
    if (entries_.size() >= max_entries) {
      // Deterministic saturation: replace the oldest observation, then
      // the lowest source id on a tie.
      auto victim = entries_.begin();
      for (auto it = entries_.begin(); it != entries_.end(); ++it)
        if (it->observed < victim->observed ||
            (it->observed == victim->observed && it->source < victim->source))
          victim = it;
      *victim = Entry{e, a, b, time, next_id_};
    } else {
      entries_.push_back(Entry{e, a, b, time, next_id_});
    }
    ++next_id_;
  }
};

} // namespace mw34::effects
