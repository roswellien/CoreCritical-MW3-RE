using EnvJson = nlohmann::json;
std::string env_test_digest(const std::string &data) {
  platform::integrity::Sha256 hash;
  hash.update(data.data(), data.size());
  return hash.hex();
}
void env_test_write(const std::filesystem::path &path,
                    const std::string &data) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary);
  file.write(data.data(), data.size());
  require(bool(file), "Could not write environment fixture");
}
std::string env_test_page(bool compressed = false) {
  std::string out(compressed ? "BWSNR3\0\0" : "BWSNR2\0\0", 8);
  write_u32(out, 1);
  write_u32(out, 1);
  write_u32(out, 1);
  write_u32(out, 1);
  write_u64(out, 73);
  std::string name = "authored";
  name.resize(32, '\0');
  out += name;
  if (!compressed)
    return out + prepared_fixture::normals(0xffff8080);
  out.append("BWNRM3\0\0", 8);
  write_u32(out, 1);
  write_u32(out, 1);
  write_u32(out, 0);
  out += name;
  write_u32(out, 1);
  write_u32(out, 1);
  const unsigned char pixel[] = {128, 128, 255, 255};
  write_u64(out, mw34::materials::normal_hash(pixel, 4));
  write_u64(out, 8);
  for (auto value : pixel) {
    out += char(0);
    out += char(value);
  }
  return out;
}
struct EnvironmentFixture {
  std::filesystem::path root;
  EnvJson manifest, registry;
  explicit EnvironmentFixture(std::filesystem::path path,
                              std::string region = "training",
                              bool materials = false)
      : root(std::filesystem::canonical(path.parent_path()) / path.filename()) {
    manifest = {{"schema_version", 2},
                {"id", "test." + root.filename().string()},
                {"native_payload",
                 {{"profile", "prepared-environment-v1"},
                  {"registry", "environment/environment.json"},
                  {"path_base", "pack-root"},
                  {"required_formats", {"BWSNR2", "BWSNR3"}}}},
                {"files", EnvJson::object()}};
    registry = {{"schema_version", 2},
                {"payload_profile", "prepared-environment-v1"},
                {"operation", region},
                {"banks", EnvJson::object()}};
    std::string bank;
    for (unsigned value : {0U, 1U, 0U, 1U, 0U, 0U})
      write_u32(bank, value);
    bank.resize(64, '\0'); // One complete native header + texture-table entry.
    put("environment/bank.bin", bank, "ZBD-texture-v1");
    std::vector<std::string> dirs = {
        region == "training" ? "t1" : "c" + region.substr(9)};
    if (region == "operation4")
      dirs.push_back("c4b");
    if (region == "shared-mechs")
      for (const char *name : {"rmechtex", "rmechtex16"})
        registry["banks"][std::string("zbd/") + name + ".zbd"] =
            "environment/bank.bin";
    else
      for (const auto &dir : dirs)
        for (const char *name : {"rtexture", "rtexture2", "rtexture3",
                                 "texture", "texture1", "texture2"})
          registry["banks"]["zbd/" + dir + "/" + name + ".zbd"] =
              "environment/bank.bin";
    put("environment/authored.bin", env_test_page(), "BWSNR2");
    put("environment/authored.txt", "MW34SNA1\nauthored.bin\n", "MW34SNA1");
    registry["authored"] = {"environment/authored.bin"};
    registry["authored_manifest"] = "environment/authored.txt";
    if (region != "shared-mechs") {
      put("environment/world.bin", env_test_page(true), "BWSNR3");
      put("environment/world.txt", "MW34SNP1\nworld.bin\n", "MW34SNP1");
      registry["world"] = {"environment/world.bin"};
      registry["world_manifest"] = "environment/world.txt";
      registry["detail"] = EnvJson::object();
      for (const char *role : {"water", "surf", "cloud"}) {
        const auto name = std::string("environment/") + role + ".bin";
        put(name, env_test_page(), "BWSNR2");
        registry["detail"][role] = name;
      }
      std::string effects("MWFX1\0\0\0", 8);
      write_u32(effects, 0);
      put("environment/effects.bin", effects, "MWFX1");
      registry["effects"] = "environment/effects.bin";
    }
    if (materials) {
      put("environment/material.bin", build_bwsfm1(1, .1f, .2f), "BWSFM1");
      registry["surface_materials"] = "environment/material.bin";
      manifest["native_payload"]["required_capabilities"] = {
          {"surface-materials", 1}};
    }
    save();
  }
  void put(const std::string &name, const std::string &bytes,
           const std::string &format = {}) {
    env_test_write(root / std::filesystem::u8path(name), bytes);
    EnvJson pin = {{"bytes", bytes.size()}, {"sha256", env_test_digest(bytes)}};
    if (!format.empty())
      pin["format"] = format;
    manifest["files"][name] = pin;
  }
  void save() {
    put("environment/environment.json", registry.dump());
    env_test_write(root / "pack.json", manifest.dump());
  }
  std::string path() const { return root.u8string(); }
};
template <class F> void env_test_reject(F action, const char *message) {
  bool rejected = false;
  try {
    action();
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, message);
}
void check_environment_packs() {
  using namespace platform::prepared;
  using platform::integrity::Sha256;
  require(
      env_test_digest("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
      "Empty SHA256 mismatch");
  Sha256 partial;
  partial.update("a", 1);
  const auto first = partial.hex();
  partial.update("bc", 2);
  require(first == env_test_digest("a") &&
              partial.hex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9"
                               "cb410ff61f20015ad" &&
              partial.hex() == partial.hex(),
          "Incremental SHA256 mismatch");
  env_test_reject([&] { partial.update(nullptr, 1); },
                  "SHA256 accepted null nonempty input");
  env_test_reject(
      [&] {
        partial.update("x", std::numeric_limits<std::uint64_t>::max() / 8 + 1);
      },
      "SHA256 accepted length overflow");
  struct HashVector {
    std::size_t size;
    const char *digest;
  };
  const HashVector vectors[] = {
      {1, "e7cf46a078fed4fafd0b5e3aff144802b853f8ae459a4f0c14add3314b7cc3a6"},
      {55, "2900465fcb533e05a158fd2b3be0e5e3b03740d83060aa3580e0d98a96bf2384"},
      {56, "31454ff48ef36af2f08fd511bdc37d9d5855ac23e992e5ff5445cb6b7674a674"},
      {63, "5f6401b96532c36de4e65beec0409b69b1d181864c8009b7a04f43e5d56350d1"},
      {64, "94eb5de4943613fd048dc93393ab06877405faa39c11f53e9386083339833e7e"},
      {65, "fc518669b6eb4b4dd91827ecacef86689c725bd5bab888fd3b26dbb196eec954"},
      {119, "b0dc41b1a384e2f1203f0351b38fbeaafceef577ce1191d5bfc25da39f721eae"},
      {120, "5df24dd802ac26132ce608dcb5f09841eef039ee0f152acf98d26d17fe4e88e6"},
      {128, "0aedd4856f8eba0963627336ad5144a9a7dbe12498e6066f0165fc97d8ddee4c"},
      {1024,
       "ffbad8f947474cfdd5b2bb22d7e0bf5ee8ba2b7af859d0c2bb28622db6a4be47"},
      {65537,
       "3123afca912e156e19bcd2055eb92e944cf6e6fdb0eef16d65464342ad1f5a1a"},
  };
  for (const auto &test : vectors) {
    std::string bytes(test.size, '\0');
    for (std::size_t i = 0; i < bytes.size(); ++i)
      bytes[i] = char((i * 37 + 11) & 255);
    Sha256 hash;
    for (std::size_t i = 0; i < bytes.size(); i += 13)
      hash.update(bytes.data() + i,
                  std::min<std::size_t>(13, bytes.size() - i));
    require(hash.hex() == test.digest, "SHA256 padding/binary vector mismatch");
  }
  for (char op = '1'; op <= '4'; ++op)
    for (char mission = '1'; mission <= (op < '3' ? '4' : '6'); ++mission)
      require(environment_region(std::string("c") + op + "m" + mission) ==
                  std::string("operation") + op,
              "Campaign region mismatch");
  for (char mission = '1'; mission <= '4'; ++mission)
    require(environment_region(std::string("t1m") + mission) == "training",
            "Training region mismatch");
  require(environment_region("c4bm6") == "operation4",
          "Late operation4 alias mismatch");
  env_test_reject([] { environment_region("t1m5"); },
                  "Unknown mission region accepted");
  const auto folder = make_private_test_dir("mw34_environment");
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() {
      std::error_code ec;
      std::filesystem::remove_all(path, ec);
    }
  } cleanup{folder};
  std::string file_bytes(65537, '\0');
  for (std::size_t i = 0; i < file_bytes.size(); ++i)
    file_bytes[i] = char((i * 37 + 11) & 255);
  env_test_write(folder / "hash.bin", file_bytes);
  require(platform::integrity::sha256_file(folder / "hash.bin") ==
              vectors[10].digest,
          "Chunked file SHA256 mismatch");
  // Later pages may contain keys already owned by an earlier catalog.
  // Loading another key from that page must never publish the duplicate,
  // even after either page has been evicted and decoded again.
  for (bool exclusion : {false, true}) {
    const auto owner =
        folder / (exclusion ? "owner-excluded.bin" : "owner.bin");
    auto first = env_test_page();
    if (exclusion)
      for (unsigned i = 0; i < 4; ++i)
        first[80 + i] = char(0xff);
    env_test_write(owner, first);
    auto later = env_test_page();
    const auto key = later.substr(12, 52);
    auto second_key = key;
    second_key[12] = 74;
    later[8] = 2;
    later.insert(64, second_key);
    later[124] = 2;
    later.insert(132, std::string(4, '\0'));
    env_test_write(folder / "later.bin", later);
    env_test_write(folder / "owner.txt",
                   "MW34SNP1\n" + owner.filename().string() + "\n");
    env_test_write(folder / "later.txt", "MW34SNP1\nlater.bin\n");
    mw34::materials::WorldNormalCache cache;
    cache.load_world_manifest(folder / "owner.txt");
    cache.load_world_manifest(folder / "later.txt");
    const mw34::materials::SurfaceKey a{1, 1, 1, 73}, b{1, 1, 1, 74};
    const auto expected = cache.surface_normal_index(a);
    require(exclusion ? expected == -1 : expected >= 0,
            "First catalog lookup failed");
    cache.load_page(1, 4);
    require(cache.bindings.at(a) == expected,
            "Later page replaced first catalog ownership");
    require(cache.surface_normal_index(b) >= 0,
            "Later page lost its unique key");
    cache.load_page(0, 4);
    cache.load_page(1, 4);
    require(cache.bindings.at(a) == expected,
            "Eviction/reload replaced first catalog ownership");
  }
  EnvironmentFixture training(folder / "training"),
      operation(folder / "operation", "operation4"),
      shared(folder / "shared", "shared-mechs");
  training.put(
      "environment/lighting.json",
      R"({"schema_version":1,"missions":{},"maps":{"BIG LAKE":{"default":"noon","presets":{"noon":{"strength":0.85}}}}})",
      "MW34-lighting-v1");
  training.registry["lighting"] = "environment/lighting.json";
  training.save();
  std::fenv_t original;
  require(std::fegetenv(&original) == 0, "Cannot save test FP environment");
  std::feclearexcept(FE_ALL_EXCEPT);
  std::feraiseexcept(FE_DIVBYZERO);
  const auto rounding = std::fegetround();
  load_environment_packs({training.path()});
  const auto flags = std::fetestexcept(FE_ALL_EXCEPT);
  const auto after_rounding = std::fegetround();
  std::fesetenv(&original);
  require(flags == FE_DIVBYZERO && after_rounding == rounding,
          "Environment admission changed native FP state");
  const auto combined = load_environment_packs(
      {training.path(), shared.path(), operation.path()});
  require(combined.regions == std::vector<std::string>(
                                  {"operation4", "training", "shared-mechs"}) &&
              combined.banks.size() == 20 &&
              combined.world_manifests.size() == 2 &&
              combined.authored_manifests.size() == 3,
          "Environment region selection mismatch");
  const auto stock = zutl::find("zbd/t1/texture.zbd");
  struct BankRestore {
    std::shared_ptr<const zutl::BankOverrides> prior;
    ~BankRestore() { zutl::set_bank_overrides(std::move(prior)); }
  };
  {
    BankRestore restore{zutl::set_bank_overrides(
        std::make_shared<const zutl::BankOverrides>(combined.banks))};
    require(zutl::find("ZBD\\T1\\TEXTURE.ZBD") ==
                combined.banks.at("zbd/t1/texture.zbd"),
            "Native bank read did not use admitted pack");
    const auto saved_mounts = zutl::mounts();
    struct MountRestore {
      zutl::Mounts saved;
      ~MountRestore() { zutl::mounts() = std::move(saved); }
    } mounts_restore{saved_mounts};
    zutl::mounts().read = {folder.generic_string(),
                           (folder / "zbd/t1").generic_string(),
                           (folder / "zbd").generic_string()};
    require(zutl::find("RTEXTURE.ZBD") ==
                combined.banks.at("zbd/t1/rtexture.zbd"),
            "Bare native mission bank bypassed pack");
    require(zutl::find("rmechtex.zbd") == combined.banks.at("zbd/rmechtex.zbd"),
            "Bare common bank bypassed pack");
    zutl::mounts().read = {folder.generic_string(),
                           (folder / "zbd/c2").generic_string()};
    require(zutl::find("rtexture.zbd").empty(),
            "Unselected mission inherited another region bank");
  }
  require(zutl::find("zbd/t1/texture.zbd") == stock,
          "Bank removal did not restore stock routing");
  EnvironmentFixture duplicate(folder / "duplicate");
  env_test_reject(
      [&] { load_environment_packs({training.path(), duplicate.path()}); },
      "Duplicate environment region ownership was accepted");
  EnvironmentPackCache cache;
  cache.resolve({training.path()});
  const auto generation = cache.generation();
  cache.resolve({training.path()});
  require(cache.hits() == 1 && cache.generation() == generation,
          "Environment cache did not hit admitted inputs");
  std::string changed;
  {
    std::ifstream in(training.root / "environment/bank.bin", std::ios::binary);
    changed.assign(std::istreambuf_iterator<char>(in), {});
  }
  changed += 'x';
  training.put("environment/bank.bin", changed, "ZBD-texture-v1");
  training.save();
  cache.resolve({training.path()});
  require(cache.generation() > generation,
          "Changed pack did not invalidate cache");
  const auto &empty = cache.resolve({});
  require(empty.banks.empty() && empty.world_manifests.empty() &&
              empty.authored_manifests.empty() && empty.regions.empty() &&
              empty.resources.empty(),
          "Pack removal retained resources");
  env_test_write(training.root / "environment/bank.bin",
                 std::string(changed.size(), 'x'));
  env_test_reject([&] { cache.resolve({training.path()}); },
                  "Corrupt pack reused cache admission");
  EnvironmentFixture broken_normal(folder / "broken-normal"),
      broken_material(folder / "broken-material", "operation1", true);
  auto corrupt = [](const std::filesystem::path &path) {
    std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
    f.seekg(-1, std::ios::end);
    char c{};
    f.get(c);
    c ^= 1;
    f.seekp(-1, std::ios::end);
    f.put(c);
  };
  corrupt(broken_normal.root / "environment/world.bin");
  corrupt(broken_material.root / "environment/material.bin");
  env_test_reject([&] { load_environment_packs({broken_normal.path()}); },
                  "Corrupt BWSNR3 pin accepted");
  env_test_reject([&] { load_environment_packs({broken_material.path()}); },
                  "Corrupt BWSFM1 pin accepted");
  // A relocated manifest must resolve its own pinned sibling, not a
  // similarly named file under the registry directory.
  EnvironmentFixture misplaced(folder / "misplaced");
  misplaced.put("catalogs/world.txt", "MW34SNP1\nworld.bin\n", "MW34SNP1");
  misplaced.manifest["files"].erase("environment/world.txt");
  misplaced.registry["world_manifest"] = "catalogs/world.txt";
  misplaced.save();
  env_test_reject([&] { load_environment_packs({misplaced.path()}); },
                  "Manifest admitted an unpinned sibling page");
  EnvironmentFixture relocated(folder / "relocated");
  relocated.put("catalogs/world.txt", "MW34SNP1\nworld.bin\n", "MW34SNP1");
  relocated.put("catalogs/world.bin", env_test_page(true), "BWSNR3");
  relocated.manifest["files"].erase("environment/world.txt");
  relocated.manifest["files"].erase("environment/world.bin");
  relocated.registry["world_manifest"] = "catalogs/world.txt";
  relocated.registry["world"] = {"catalogs/world.bin"};
  relocated.save();
  require(load_environment_packs({relocated.path()}).world_manifests.size() ==
              1,
          "Pinned relocated catalog rejected");
  for (unsigned offset : {12U, 16U}) {
    EnvironmentFixture broken(folder /
                              ("broken-count-" + std::to_string(offset)));
    std::string bank(64, '\0');
    bank[4] = 1;
    bank[offset] = char(0xff);
    bank[offset + 3] = char(0x80);
    broken.put("environment/bank.bin", bank, "ZBD-texture-v1");
    broken.save();
    env_test_reject([&] { load_environment_packs({broken.path()}); },
                    "Negative native bank count admitted");
  }
  EnvironmentFixture truncated(folder / "truncated-bank");
  std::string short_bank(24, '\0');
  short_bank[4] = 1;
  short_bank[12] = 1;
  truncated.put("environment/bank.bin", short_bank, "ZBD-texture-v1");
  truncated.save();
  env_test_reject([&] { load_environment_packs({truncated.path()}); },
                  "Truncated bank table admitted");
  EnvironmentFixture traversal(folder / "traversal");
  traversal.manifest["files"]["../outside.bin"] =
      traversal.manifest["files"]["environment/bank.bin"];
  traversal.save();
  env_test_reject([&] { load_environment_packs({traversal.path()}); },
                  "Pack traversal accepted");
  EnvironmentFixture duplicate_json(folder / "duplicate-json");
  const auto text = duplicate_json.manifest.dump();
  env_test_write(duplicate_json.root / "pack.json",
                 "{\"id\":\"ambiguous\"," + text.substr(1));
  env_test_reject([&] { load_environment_packs({duplicate_json.path()}); },
                  "Duplicate JSON key accepted");
  EnvironmentFixture link(folder / "link");
  const auto bank = link.root / "environment/bank.bin",
             external = folder / "outside.bin";
  std::filesystem::rename(bank, external);
  std::error_code error;
  std::filesystem::create_symlink(external, bank, error);
  if (!error)
    env_test_reject([&] { load_environment_packs({link.path()}); },
                    "Symlink escape accepted");
  else
    std::printf("Environment symlink creation unavailable on this host: %s\n",
                error.message().c_str());
  env_test_reject([&] { platform::integrity::sha256_file(folder / "missing"); },
                  "SHA256 accepted missing file");
}
