// The pilot, its file and the pilot list (docs/pilot.md).
#include "mech3/pilot.h"
#include "mech3/content_campaign.h"
#include "mech3/content_registry.h"
#include "mech3/content_state.h"

#include <cstring>
#include <filesystem>
#include <string>

#include "gamez/zutil/zfix.h"
#include "gamez/zutil/zutl_path.h"

namespace mech3 {

namespace {
std::list<Pilot*> g_pilots;    // [0x0060860c], the count [0x00608610]
Pilot* g_current = nullptr;    // [0x0060861c]

constexpr int32_t kVersion = 15 + 0x20;  // 0x00423cf0 (the loadouts' 15) + 0x20

namespace fs = std::filesystem;

// FIX (A) for CreateFileA on the install's pilots\ (pilot.md, Files): the
// pilot is the write dir's. A pilot only in a read mount (an imported
// install's) is copied there first, so its progress is kept where the
// rebuild writes. OPEN_EXISTING, then OPEN_ALWAYS: neither truncates.
std::FILE* open_existing(const std::string& path) {
  const std::string w = zutl::write_path(path.c_str());
  std::error_code ec;
  if (!fs::is_regular_file(w, ec)) {
    const std::string src = zutl::find(path.c_str());
    if (src.empty() || !fs::is_regular_file(src, ec)) return nullptr;
    fs::create_directories(fs::path(w).parent_path(), ec);
    if (!fs::copy_file(src, w, fs::copy_options::overwrite_existing, ec)) return nullptr;
  }
  return std::fopen(w.c_str(), "r+b");
}
std::FILE* open_always(const std::string& path) {
  if (std::FILE* f = open_existing(path)) return f;
  const std::string w = zutl::write_path(path.c_str());
  std::error_code ec;
  fs::create_directories(fs::path(w).parent_path(), ec);
  return std::fopen(w.c_str(), "w+b");
}

// FIX (A) for MoveFileA and DeleteFileA: in the write dir; MoveFileA fails
// when the new name exists (std::rename would replace it).
void move_file(const char* from, const char* to) {
  const std::string a = zutl::write_path(from), b = zutl::write_path(to);
  std::error_code ec;
  if (!fs::exists(a, ec)) return;
  if (fs::exists(b, ec) && !fs::equivalent(a, b, ec)) return;  // a new case of the same name moves
  fs::rename(a, b, ec);
}
bool delete_file(const char* path) {
  std::error_code ec;
  return fs::remove(zutl::write_path(path), ec);
}

// ReadFile / WriteFile of a 4-byte field: false on an error, not at the end
// (ReadFile succeeds there, reading nothing).
bool rd4(std::FILE* f, void* p) {
  std::fread(p, 1, 4, f);
  return !std::ferror(f);
}
bool wr4(std::FILE* f, const void* p) { return std::fwrite(p, 1, 4, f) == 4; }
}  // namespace

std::list<Pilot*>& pilots() { return g_pilots; }
Pilot*& current_pilot() { return g_current; }

// Mech3.exe 1.2 @ 0x00477b20. FIX (bug): the name is copied with strcpy into
// 64 bytes; a longer one (a long pilots\ file name) overwrote the games'
// vector and crashed. Here it is cut, and load_pilots skips such files.
Pilot::Pilot(const char* n) : version(kVersion) {
  std::strncpy(name, n ? n : "", sizeof name - 1);
  auto* c = new CampaignGame(this, 1, 1);  // new(200)
  current = c;
  campaigns.push_back(c);
  lab = new LabGame(this);                 // new(0x84)
  action_game = new ActionGame(this);      // new(0x17c)
  multi = new MultiplayerGame(this);       // new(0xb0)
  training = new TrainingGame(this);       // new(0x8c)
}

// Mech3.exe 1.2 @ 0x00477e10, with the base's 0x004794c0
Pilot::~Pilot() {
  if (dirty) {
    if (active_content()) {
      try { save_managed_pilot(*this); }
      catch (const std::exception &error) {
        std::fprintf(stderr, "mw3 content pilot save: %s\n", error.what());
      }
    } else {
      open();
      if (write_header()) write();
    }
  }
  close();
  for (Game*& g : campaigns) delete g, g = nullptr;
  campaigns.clear();
  delete lab;
  delete action_game;
  delete multi;
  delete training;
  lab = nullptr, action_game = nullptr, multi = nullptr, training = nullptr;
  close();
}

void Pilot::close() {
  if (file) std::fclose(file);
  file = nullptr;
}

// Mech3.exe 1.2 @ 0x00478c80
std::string Pilot::file_path() const { return std::string("pilots\\") + name + ".mw3"; }

// Mech3.exe 1.2 @ 0x00478ce0. False when it was already open. A header that
// does not match is written over, the rest of the file kept (OPEN_ALWAYS).
// The header's fields read as 0 when the file is shorter (stack garbage in
// the exe).
bool Pilot::open() {
  if (file) return false;
  const std::string path = file_path();
  if (active_content()) {
    // Managed saves use a separate atomic writer. Reading must never repair an
    // incompatible header in place or manufacture a missing checkpoint index.
    file = std::fopen(zutl::write_path(path.c_str()).c_str(), "rb");
    return file != nullptr;
  }
  bool create = false;
  file = open_existing(path);
  if (!file) {
    create = true;
  } else {
    std::fseek(file, 0, SEEK_SET);
    int32_t m = 0, v = 0, x = 0;
    if (!rd4(file, &m) || !rd4(file, &v) || !rd4(file, &x) || m != magic || v != version) {
      create = true;
      close();
    }
  }
  if (create) {
    file = open_always(path);
    if (file) {
      std::fseek(file, 0, SEEK_SET);
      if (!wr4(file, &magic) || !wr4(file, &version) || !wr4(file, &x0c)) close();
    }
  }
  return file != nullptr;
}

// Mech3.exe 1.2 @ 0x004794f0
bool Pilot::write_header() {
  if (!file) return false;
  std::fseek(file, 0, SEEK_SET);
  if (!wr4(file, &magic) || !wr4(file, &version) || !wr4(file, &x0c)) return false;
  return !active_content() || write_managed_pilot_identity(file);
}

// Two pilot names name the same file: without case (ASCII and the
// Windows-1252 letters) when the fix is on, else byte for byte (strncmp, 64).
bool same_name(const char* a, const char* b) {
  if (!zfix::options().bugs) return std::strncmp(a, b, 0x40) == 0;
  auto fold = [](unsigned char c) -> unsigned char {
    if ((c >= 'A' && c <= 'Z') || (c >= 0xc0 && c <= 0xde && c != 0xd7)) return static_cast<unsigned char>(c + 0x20);
    return c;
  };
  for (int i = 0; i < 0x40; ++i) {
    const unsigned char x = fold(static_cast<unsigned char>(a[i])), y = fold(static_cast<unsigned char>(b[i]));
    if (x != y) return false;
    if (!x) return true;
  }
  return true;
}

// The save the creators and the rename do inline (0x004788b0, 0x00478a40,
// 0x00477fe0, 0x00478f10): open, the header, the games, close.
bool Pilot::save() {
  if (active_content()) return save_managed_pilot(*this);
  open();
  const bool ok = write_header();
  if (ok) write();
  close();
  return ok;
}

// Mech3.exe 1.2 @ 0x004790d0. The four games in the file's order, then the
// saved campaigns: each is kept only when its saved game exists (the others
// leaked in the exe; deleted here). FIX (bug): a damaged count read on past
// the file's end for up to 4 billion campaigns, a hang; here the reading
// stops at the end (docs/fixes.md).
bool Pilot::read() {
  training->load();
  multi->load();
  action_game->load();
  lab->load();
  uint32_t n = 0;
  std::fread(&n, 1, 4, file);
  if (n && campaigns.capacity() < n + 1 && n < 0x10000) campaigns.reserve(n + 1);
  for (; n; --n) {
    // Managed validation can throw while reading a campaign. Finish construction
    // before loading so every partially read slot remains owned on failure.
    std::unique_ptr<CampaignGame> c;
    if (active_content()) {
      c = std::make_unique<CampaignGame>(this, 0, 0);
      c->a8 = c->ac = 0;
      c->load();
    } else {
      c = std::make_unique<CampaignGame>(this);  // new(200), read
    }
    if (c->readable()) campaigns.push_back(c.release());
    if (std::feof(file)) break;
  }
  return true;
}

// Mech3.exe 1.2 @ 0x00479390. The new campaign (the first) is never saved.
bool Pilot::write() {
  training->save();
  multi->save();
  action_game->save();
  lab->save();
  std::vector<Game*> saved;
  for(size_t i=0;i<campaigns.size();++i) {
    auto* game=dynamic_cast<CampaignGame*>(campaigns[i]);
    if (i == 0 && (!game || game->content_mission.empty())) continue;
    if(game && game->content_placeholder)continue;
    saved.push_back(campaigns[i]);
  }
  const int32_t n = static_cast<int32_t>(saved.size());
  std::fwrite(&n, 1, 4, file);
  if (n)
    for (Game* game : saved) game->save();
  dirty = 0;
  return true;
}

// Mech3.exe 1.2 @ 0x00477fe0. The campaigns' saved games are moved to the
// new name, then the file, which is written again. Nothing checks that
// another pilot has the name.
bool Pilot::rename(const char* n) {
  if (std::strcmp(n, name) == 0) return true;
  // FIX (bug): the exe renamed onto another pilot's name, sharing its file.
  if (zfix::options().bugs) {
    Pilot* other = find_pilot(n);
    if (other && other != this) return false;
  }
  const std::string old_path = file_path();
  for (Game* g : campaigns) {
    auto* c = static_cast<CampaignGame*>(g);
    const std::string from = c->path();
    const std::string to = c->path_for(n);
    move_file(from.c_str(), to.c_str());
    if (!c->content_mission.empty()) move_file((from+".ccsave").c_str(),(to+".ccsave").c_str());
  }
  std::strncpy(name, n, sizeof name - 1);
  move_file(old_path.c_str(), file_path().c_str());
  save();
  return true;
}

// Mech3.exe 1.2 @ 0x004781e0, with 0x00433ca0 for each campaign
bool Pilot::delete_files() {
  for (Game* g : campaigns) {
    auto* campaign=static_cast<CampaignGame*>(g);
    const std::string path=campaign->path();
    delete_file(path.c_str());
    if (!campaign->content_mission.empty()) delete_file((path+".ccsave").c_str());
  }
  dirty = 0;
  return delete_file(file_path().c_str());
}

// Mech3.exe 1.2 @ 0x00478250. A campaign is the last one, the newest save.
void Pilot::select_game(int32_t type) {
  switch (type) {
    case kMechLab: current = lab; break;
    case kCampaign: current = campaigns.empty() ? nullptr : campaigns.back(); break;
    case kInstantAction: current = action_game; break;
    case kMultiplayer: current = multi; break;
    case kTraining: current = training; break;
    default: break;
  }
}

// Mech3.exe 1.2 @ 0x00478ed0
bool Pilot::select_campaign(int32_t i) {
  if (i < 0 || i >= campaign_count()) return false;
  current = campaigns[static_cast<size_t>(i)];
  return true;
}

// Mech3.exe 1.2 @ 0x00478f10. For a campaign: its next game (or itself
// settled, without `next`) filed among the saved ones, replacing one of the
// same mission when the player says so (message 0xfb3), and made current;
// then the pilot saved. True when there is no next game: the campaign is over.
// FIX (bug), behind `bugs`: the exe wrote the next game's world (zSave, in
// next_mission) before asking, so No kept the old entry over the new world;
// here the world is written once filed, and not on No (fixes.md).
bool Pilot::record_mission(bool next) {
  if (!current || current->type() != kCampaign) return false;
  const bool fix = zfix::options().bugs;
  CampaignGame* g = static_cast<CampaignGame*>(current)->next_mission(next, !fix);
  if (g && g != current) {
    std::unique_ptr<CampaignGame> pending(g);
    const int32_t m = g->mission_number(), op = g->operation();
    auto it = campaigns.begin();
    for (; it != campaigns.end(); ++it) {
      if ((*it)->type() != kCampaign) continue;
      auto* c = static_cast<CampaignGame*>(*it);
      if (active_content() && c->content_run != g->content_run) continue;
      if (c->operation() == op && c->mission_number() == m) break;
    }
    bool filed = true;
    if (it == campaigns.end()) {
      if (!g->content_mission.empty()) save_content_campaign(*g);
      campaigns.push_back(g);
      pending.release();
    } else if (pilot_hooks().message_box(1, 7, 0xfb3) == 1) {
      if (!g->content_mission.empty()) save_content_campaign(*g);
      Game* old = *it;
      *it = g;
      pending.release();
      delete old;
    } else {
      pending.reset();
      g = static_cast<CampaignGame*>(*it);
      filed = false;
    }
    if (fix && filed && g->content_mission.empty()) pilot_hooks().save_world(g->path(), 2);
    current = g;
  }
  save();
  return g == nullptr;
}

// Mech3.exe 1.2 @ 0x00479430
Store* Pilot::store() { return current ? &current->store : nullptr; }

// Mech3.exe 1.2 @ 0x00479440, 0x00479460, 0x00479480, 0x004794a0
CampaignGame* Pilot::campaign() {
  return current && current->type() == kCampaign ? static_cast<CampaignGame*>(current) : nullptr;
}
ActionGame* Pilot::action() {
  return current && current->type() == kInstantAction ? static_cast<ActionGame*>(current) : nullptr;
}
MultiplayerGame* Pilot::multiplayer() {
  return current && current->type() == kMultiplayer ? static_cast<MultiplayerGame*>(current) : nullptr;
}
TrainingGame* Pilot::training_game() {
  return current && current->type() == kTraining ? static_cast<TrainingGame*>(current) : nullptr;
}

// Mech3.exe 1.2 @ 0x00478be0. FIX (bug): the exe compares with case while the
// file names do not, so "BOB" made over "bob" shared bob.mw3 and clobbered
// it; here names match as Windows file names do (docs/fixes.md).
Pilot* find_pilot(const char* name) {
  if (!name) return nullptr;
  for (Pilot* p : g_pilots)
    if (p && same_name(name, p->name)) return p;
  return nullptr;
}

// Mech3.exe 1.2 @ 0x00478700. A pilot that fails to read is destroyed, which
// (dirty from its constructor) writes it again.
Pilot* load_pilot(const char* name, bool reload) {
  Pilot* p = find_pilot(name);
  if (p && !reload) return p;
  if (active_content()) {
    auto staged = std::make_unique<Pilot>(name);
    staged->dirty = 0;
    const auto previous_action = action_setup();
    try {
      if (!staged->open()) return nullptr;
      int32_t header[3]{};
      if (std::fread(header, 4, 3, staged->file) != 3 || header[0] != staged->magic ||
          header[1] != staged->version)
        cc::content::fail("pilot-header", "Managed pilot header is incompatible or corrupt");
      staged->x0c = header[2];
      read_managed_pilot_identity(staged->file);
      if (!staged->read() || std::ferror(staged->file) || std::feof(staged->file))
        cc::content::fail("pilot-read", "Managed pilot record is truncated or corrupt");
      staged->close();
    } catch (const std::exception& error) {
      action_setup() = previous_action;
      std::fprintf(stderr, "mw3 content pilot load: %s\n", error.what());
      return nullptr;
    }
    staged->dirty = 1;
    Pilot* loaded = staged.release();
    if (p) {
      for (Pilot*& entry : g_pilots)
        if (entry == p) entry = loaded;
      if (g_current == p) g_current = loaded;
      p->dirty = 0;
      delete p;
    } else {
      g_pilots.push_back(loaded);
    }
    return loaded;
  }
  if (!p) p = new Pilot(name);  // new(0x84)
  p->open();
  bool ok = false;
  if (p->file) {
    std::fseek(p->file, 0, SEEK_SET);
    int32_t m = 0, v = 0, x = 0;
    ok = rd4(p->file, &m) && rd4(p->file, &v) && rd4(p->file, &x) && m == p->magic && v == p->version && p->read();
  }
  p->close();
  if (ok) {
    g_pilots.push_back(p);
    return p;
  }
  delete p;
  return nullptr;
}

// Mech3.exe 1.2 @ 0x004788b0. A new pilot's file is written at once, over
// whatever a file of that name held.
Pilot* create_pilot(const char* name) {
  if (!name || !*name) return nullptr;
  Pilot* p = find_pilot(name);
  if (p) return p;
  p = new Pilot(name);  // new(0x84)
  p->save();
  g_pilots.push_back(p);
  return p;
}

// Mech3.exe 1.2 @ 0x00477f50. FIX (bug): the exe leaves [0x0060861c]
// pointing at the freed pilot when it is the one (the pilot screen's DELETE on
// the pilot chosen before); nothing reads it before ACCEPT or CANCEL sets it
// again, but here it is cleared rather than left dangling.
bool delete_pilot(const char* name) {
  Pilot* p = find_pilot(name);
  if (!p) return true;
  if (g_current == p) g_current = nullptr;
  g_pilots.remove(p);
  p->delete_files();
  delete p;
  return true;
}

// Mech3.exe 1.2 @ 0x00478560. FIX (A): FindFirstFileA / FindNextFileA are
// zutl::list (the write dir's and the mounts' pilots\, each name once), and
// _mkdir makes the folders in the write dir; the exe closed the search with
// CloseHandle, not FindClose. FIX (bug): names that do not fit a pilot's 64
// bytes are skipped (they crashed the exe, see the constructor).
void load_pilots() {
  std::error_code ec;
  fs::create_directories(zutl::write_path("pilots\\games"), ec);
  for (const std::string& file : zutl::list("pilots\\*.mw3")) {
    std::string n = file;  // _splitpath's file name
    const size_t slash = n.find_last_of("\\/:");
    if (slash != std::string::npos) n = n.substr(slash + 1);
    const size_t dot = n.rfind('.');
    if (dot != std::string::npos) n = n.substr(0, dot);
    if (n.size() >= sizeof Pilot::name) continue;
    load_pilot(n.c_str(), false);
  }
}

// Mech3.exe 1.2 @ 0x004785f0. Each is destroyed (and so saved while dirty).
void delete_other_pilots() {
  for (auto it = g_pilots.begin(); it != g_pilots.end();) {
    Pilot* p = *it;
    if (p == g_current) {
      ++it;
      continue;
    }
    it = g_pilots.erase(it);
    delete p;
  }
}

// Mech3.exe 1.2 @ 0x00478a40 (the debug menu's campaign entries): the
// "DefaultPlayer" pilot, made and saved when there is none, on that
// operation (1..4) and mission (1..the operation's count).
Pilot* debug_start_campaign(int32_t campaign, int32_t mission) {
  if (campaign > 4) campaign = 4;  // 0x00477ac0
  else if (campaign < 1) campaign = 1;
  const int32_t last = pilot_hooks().campaign_missions(campaign);  // 0x00477af0
  if (mission > last) mission = last;
  else if (mission < 1) mission = 1;
  Pilot* p = find_pilot("DefaultPlayer");
  if (!p) {
    p = new Pilot("DefaultPlayer");
    p->save();
    g_pilots.push_back(p);
  }
  p->select_game(kCampaign);
  if (auto* c = static_cast<CampaignGame*>(p->current)) {
    c->mission = mission;
    c->campaign = campaign;
  }
  return p;
}

bool campaign_advance(int32_t& operation, int32_t& mission) {
  ++mission;
  if (mission > pilot_hooks().campaign_missions(operation)) {
    ++operation;
    mission = 1;
  }
  return operation == 5;
}

}  // namespace mech3
