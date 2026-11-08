
void check_local_light_core() {
  using namespace mw34::effects;
  using namespace std;

  // --- Part 1: rank_frame_lights ---
  {
    vector<Candidate> cands;
    cands.reserve(128);
    for (int i = 0; i < 128; ++i) {
      Candidate c{};
      c.light = Light{};
      c.cone = {1.0f, 1.0f, 1.0f, 1.0f};
      c.source = static_cast<uint64_t>(i + 1);
      c.weapon = (i < 32);
      c.light.start = {static_cast<float>(i), 0.0f, 0.0f, 10.0f};
      c.light.end = {static_cast<float>(i), 0.0f, 0.0f, 0.0f};
      c.light.color = {1.0f, 1.0f, 1.0f, 1.0f};
      cands.push_back(c);
    }

    Point eye{0.0f, 0.0f, 0.0f};
    Uniform u1 = rank_frame_lights(cands, eye, 64, 16);
    require(u1.params[0] == 64, "rank total 64");
    require(u1.params[1] == 32, "rank weapons 32");
    require(u1.params[2] == 32, "rank fixtures 32");

    vector<Candidate> rev = cands;
    reverse(rev.begin(), rev.end());
    Uniform u2 = rank_frame_lights(rev, eye, 64, 16);
    require(memcmp(&u1, &u2, sizeof u1) == 0,
            "rank byte-identical after reverse");

    // Reserve sixteen slots for weapons even when all sources are fixtures.
    for (auto &c : cands)
      c.weapon = false;
    Uniform u3 = rank_frame_lights(cands, eye, 64, 16);
    require(u3.params[0] == 48, "rank total 48 all fixture");
    require(u3.params[1] == 0, "rank weapons 0 all fixture");
    require(u3.params[2] == 48, "rank fixtures 48 all fixture");
  }

  // --- Part 2: ObservedLights ---
  {
    ObservedLights obs;
    Emitter e{};
    e.kind = 2;
    e.radius = 10.0f;
    e.intensity = 2.0f;
    e.lifetime = 1.0f;
    e.color = {1.0f, 0.5f, 0.2f};

    vector<Point> pts = {{1.0f, 2.0f, 3.0f}, {3.0f, 4.0f, 5.0f}};
    obs.observe(e, pts, 0.0);
    require(obs.size() == 1, "obs size 1");
    auto cands = obs.candidates(0.0);
    require(cands.size() == 1, "obs candidates 1");
    // centroid = (2,3,4)
    require(fabs(cands[0].light.start[0] - 2.0f) < 1e-5, "centroid x");
    require(fabs(cands[0].light.start[1] - 3.0f) < 1e-5, "centroid y");
    require(fabs(cands[0].light.start[2] - 4.0f) < 1e-5, "centroid z");

    // Observe same emitter at t=0.1 -> same id, size stays 1
    vector<Point> pts2 = {{2.1f, 3.0f, 4.0f}};
    obs.observe(e, pts2, 0.1);
    require(obs.size() == 1, "obs size still 1");
    auto cands2 = obs.candidates(0.1);
    require(cands2.size() == 1 && cands2[0].source == cands[0].source,
            "obs dedup retains source");

    // candidates at 1.2 -> empty (lifetime 1.0, expired)
    auto cands3 = obs.candidates(1.2);
    require(cands3.empty(), "obs candidates expired");

    // Observe at time 2 then rewind to 1 -> empty
    obs.clear();
    obs.observe(e, pts, 2.0);
    auto cands4 = obs.candidates(1.0);
    require(cands4.empty(), "obs rewind empty");

    obs.clear();
    require(obs.size() == 0, "obs cleared");
  }

  // --- Part 3: Beam kind 1 ---
  {
    ObservedLights obs;
    Emitter e{};
    e.kind = 1;
    e.radius = 10.0f;
    e.intensity = 1.0f;
    e.lifetime = 1.0f;
    e.color = {1.0f, 1.0f, 1.0f};
    vector<Point> pts = {{0.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}};
    obs.observe(e, pts, 0.0);
    auto cands = obs.candidates(0.0);
    require(cands.size() == 1, "beam candidates 1");
    require(fabs(cands[0].light.end[0] - 10.0f) < 1e-5 &&
                cands[0].light.end[3] == 1,
            "beam endpoint/capsule kind");
  }

  // --- Part 4: Invalid NaN time throws ---
  {
    bool threw = false;
    try {
      ObservedLights obs;
      Emitter e{};
      e.kind = 2;
      e.radius = 1.0f;
      e.intensity = 1.0f;
      e.lifetime = 1.0f;
      e.color = {1.0f, 1.0f, 1.0f};
      vector<Point> pts = {{0.0f, 0.0f, 0.0f}};
      obs.observe(e, pts, numeric_limits<double>::quiet_NaN());
    } catch (...) {
      threw = true;
    }
    require(threw, "NaN time throws");
  }

  // Native creation permits diffuse=0, ambient=1. Both contribute to
  // projected-light intensity; validate them before combining in double.
  {
    zcls::Node node{};
    zcls::Light data{};
    node.put<int32_t>(0x34, 9);
    node.data = &data;
    data.put<uint32_t>(0xb0, 0x202);
    data.put<float>(0xb8, 10);
    data.put<float>(0xa0, 1);
    data.put<float>(0xa4, 1);
    zmod::LightList list{};
    list.count = 1;
    list.list[0].node = &node;
    list.list[0].data = &data;
    const auto before = data;
    const auto lights = collect_native_lights(list);
    require(lights.size() == 1 && lights[0].light.color[3] == 1,
            "Ambient-only native light discarded");
    require(!std::memcmp(before.raw, data.raw, sizeof data.raw),
            "Ambient light collection mutated native data");
    data.put<float>(0x9c, std::numeric_limits<float>::max());
    data.put<float>(0xa0, std::numeric_limits<float>::max());
    bool rejected = false;
    try {
      collect_native_lights(list);
    } catch (const std::exception &) {
      rejected = true;
    }
    require(rejected, "Overflowing diffuse+ambient accepted");
  }
  // Full parent hierarchy and mutation/depth controls.
  {

    zcls::Node root{}, grand{}, parent{}, leaf{};
    zcls::Object3d gd{}, pd{};
    zcls::Light ld{};
    root.put<int32_t>(0x34, 2);
    grand.put<int32_t>(0x34, 5);
    parent.put<int32_t>(0x34, 5);
    leaf.put<int32_t>(0x34, 9);
    grand.data = &gd;
    parent.data = &pd;
    leaf.data = &ld;
    zcls::Node *gp[]{&root};
    zcls::Node *pp[]{&grand};
    zcls::Node *lp[]{&parent};
    grand.parents = gp;
    parent.parents = pp;
    leaf.parents = lp;
    grand.put<int32_t>(0x54, 1);
    parent.put<int32_t>(0x54, 1);
    leaf.put<int32_t>(0x54, 1);
    float g[]{0, 2, 0, -3, 0, 0, 0, 0, 4, 10, 20, 30},
        p[]{1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 2, 3};
    std::memcpy(gd.raw + 0x30, g, 48);
    std::memcpy(pd.raw + 0x30, p, 48);
    ld.put<float>(0xc, 4);
    ld.put<float>(0x10, 5);
    ld.put<float>(0x14, 6);
    auto before = ld;
    auto gb = gd;
    auto pb = pd;
    auto matrix = mw34::effects::light_world_transform(leaf);
    if (std::abs(matrix[3] + 11) > 1e-5 || std::abs(matrix[7] - 30) > 1e-5 ||
        std::abs(matrix[11] - 66) > 1e-5)
      throw std::runtime_error("full parent hierarchy placement lost");
    if (std::memcmp(before.raw, ld.raw, sizeof(ld.raw)) ||
        std::memcmp(gb.raw, gd.raw, sizeof(gd.raw)) ||
        std::memcmp(pb.raw, pd.raw, sizeof(pd.raw)))
      throw std::runtime_error("native data mutated");
    auto rejects = [](auto fn) {
      try {
        fn();
      } catch (const std::exception &) {
        return;
      }
      throw std::runtime_error("bad hierarchy accepted");
    };
    lp[0] = &leaf;
    rejects([&] { mw34::effects::light_world_transform(leaf); });
    lp[0] = &parent;
    leaf.put<int32_t>(0x54, 2);
    rejects([&] { mw34::effects::light_world_transform(leaf); });
    leaf.put<int32_t>(0x54, 1);
    std::vector<zcls::Node> chain(65);
    std::vector<std::array<zcls::Node *, 1>> links(65);
    for (size_t i = 0; i < 65; ++i) {
      chain[i].put<int32_t>(0x34, 6);
      if (i) {
        links[i][0] = &chain[i - 1];
        chain[i].parents = links[i].data();
        chain[i].put<int32_t>(0x54, 1);
      }
    }
    chain[0].put<int32_t>(0x34, 5);
    chain[0].data = &gd;
    auto at64 = mw34::effects::light_world_transform(chain[63]);
    if (at64[3] != 10)
      throw std::runtime_error("64-level root skipped");
    rejects([&] { mw34::effects::light_world_transform(chain[64]); });
  }
}
