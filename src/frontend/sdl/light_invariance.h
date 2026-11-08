
#pragma once
#include "crt/msvc_rand.h"
#include "gamez/zclass/cls_data.h"
#include "mech3/app_mech.h"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace sdlfe {

class LightSimulationTrace {
public:
  LightSimulationTrace(int frames, bool native_mission) {
    const char *env = std::getenv("MW34_LIGHT_INVARIANCE_LOG");
    if (!env)
      return;
    if (!*env)
      throw std::invalid_argument("Empty MW34_LIGHT_INVARIANCE_LOG");
    if (!native_mission || frames < 1 || frames > 4096)
      throw std::invalid_argument("MW34_LIGHT_INVARIANCE_LOG requires "
                                  "native_mission and frames 1..4096");
    disabled_ = false;
    path_ = env;
    expected_ = static_cast<unsigned>(frames);
    samples_.reserve(expected_);
  }

  void sample(int frame) {
    if (disabled_)
      return;
    if (frame != static_cast<int>(samples_.size()) + 1 ||
        frame > static_cast<int>(expected_))
      throw std::logic_error("LightSimulationTrace::sample frame mismatch");
    void *player = mech3::mission_player_mech();
    if (!player)
      throw std::logic_error("Missing native player for light proof");
    auto *node = static_cast<zcls::Node *>(mech3::mech_node_of(player));
    if (!node || node->cls() != 5 || !node->data)
      throw std::logic_error(
          "LightSimulationTrace::sample invalid player node");
    std::array<uint32_t, 26> s{};
    s[0] = static_cast<uint32_t>(frame);
    s[1] = crt::msvc_rand_state();
    const auto *obj = static_cast<const zcls::Object3d *>(node->data);
    std::memcpy(s.data() + 2, static_cast<const uint8_t *>(obj->raw) + 0x30,
                96);
    samples_.push_back(s);
  }

  void finish() {
    if (disabled_)
      return;
    if (samples_.size() != expected_)
      throw std::logic_error("LightSimulationTrace::finish incomplete proof");
    std::ofstream out(std::filesystem::u8path(path_),
                      std::ios::binary | std::ios::trunc);
    if (!out)
      throw std::runtime_error("LightSimulationTrace::finish cannot open file");
    const char hdr[8] = {'M', 'W', 'L', 'S', 'I', 'M', '1', '\0'};
    out.write(hdr, 8);
    uint32_t cnt = static_cast<uint32_t>(samples_.size());
    for (int i = 0; i < 4; ++i)
      out.put(static_cast<char>((cnt >> (i * 8)) & 0xFF));
    for (const auto &s : samples_)
      for (uint32_t w : s)
        for (int i = 0; i < 4; ++i)
          out.put(static_cast<char>((w >> (i * 8)) & 0xFF));
    out.flush();
    if (!out)
      throw std::runtime_error("LightSimulationTrace::finish write failure");
  }

private:
  bool disabled_ = true;
  std::string path_;
  unsigned expected_ = 0;
  std::vector<std::array<uint32_t, 26>> samples_;
};

} // namespace sdlfe
