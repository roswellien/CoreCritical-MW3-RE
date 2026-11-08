#pragma once
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "gamez/zgame/unported.h"
#include "mech3/mech_config.h"
#include "content/content.h"

// The pilot (docs/pilot.md): the player's record at [0x0060861c], one per
// file in pilots\, and the five games it owns (the campaign saves, Instant
// Action, multiplayer, training and the mech lab's), with their stores of
// mechs and parts. Original files unknown: the pilot's functions sit between
// mech3\mech_skin.cpp and mech3\mech_waynet.cpp (0x004779f0..0x004794f0),
// the games and the slot between mech3\mech_data.cpp and mech3\mech_map.cpp
// (0x00433cc0..0x004379e0, by the option screens), the store between
// mech3\mech_salvage.cpp and mech3\mech_skin.cpp (0x00470ce0..0x00473400).
// The original's Win32 file handle is a std::FILE* here (FIX (A), pilot.md).
namespace mech3 {

using zgame::Handle;
class Pilot;

// The calls out of the pilot module into parts not ported, by address.
class PilotHooks : public zgame::Unported {
 public:
  // Fills a store with everything the mech lab offers (0x00470e30: every
  // mech, weapon, ammo and equipment the tech tables allow; marked `full`).
  // `game` is the kind of the game being made (GameType).
  virtual void store_fill(class Store& s, int32_t game);
  // A new campaign's starting mech (0x004733b0): `mech` with the dfn's
  // configuration named `config` added to the store; 1 on success.
  virtual int32_t store_add_mech(class Store& s, int32_t mech, const char* config);
  // The slot's saved configuration (0x004242a0 with the name "temp", and
  // 0x00424480): the ported Config's load and save by default.
  virtual Config* config_load(const char* name, std::FILE* f);
  virtual void config_save(Config* c, std::FILE* f);
  virtual const char* message(int32_t id);  // 0x00576040, Mech3Msg.dll's text
  // 0x00575fd0: message `id` with FormatMessage's %1!d!, %2!d! ... filled.
  virtual std::string format_ints(int32_t id, std::initializer_list<int32_t> args);
  virtual int32_t campaign_missions(int32_t campaign);  // 0x00484840, ported
  // A slot's configuration copied (0x00424010(store 0, flag 0): ECX the
  // name, EDX the configuration) and its free slots counted again
  // (0x00425bd0): the ported Config's by default.
  virtual Config* config_copy(const Config& c);
  virtual void config_recount(Config& c);
  // The mission's salvage ([0x00609990], a store the mission fills).
  virtual class Store* mission_salvage() { return nullptr; }
  // 0x005a8a70: zSave writes the world into a saved game (`kind` 2 here);
  // the ported zSave by default.
  virtual void save_world(const char* path, int32_t kind);
  // Managed saves publish inventory only after a successful native snapshot.
  virtual bool save_world_checked(const char* path, int32_t kind);
  // 0x005a8a90: zSave reads a saved game into the world; false when it did not.
  virtual bool load_world(const char* path, int32_t kind);
  // 0x004ac150: the game's message box; 1 for yes.
  virtual int32_t message_box(int32_t type, int32_t caption, int32_t text) {
    return static_cast<int32_t>(unported("0x004ac150", {type, caption, text}));
  }
};
void set_pilot_hooks(PilotHooks* h);
PilotHooks& pilot_hooks();

// The store (0x00470ce0, vtable 0x005c36e0), 0x14 bytes: a list of items
// (the loadouts' kinds, mech_config.h) with their counts. A `full` store is
// the mech lab's everything (store_fill) and saves as no items.
class Store;
// 0x00424010 (ECX the name, EDX the loadout, then the store and `take`): a
// copy; with a store, cut to the items it holds (then modified, the sinks
// recounted) and with `take` taken out of it.
Config* config_copy_store(const char* name, const Config& c, Store* store, bool take);

class Store {
 public:
  explicit Store(uint8_t flag = 0);  // 0x00470ce0
  ~Store();                          // 0x00470d20
  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;
  ConfigItem* add_copy(const ConfigItem* item, int32_t n);  // vt+0x00  0x00472630
  EngineItem* add_engine(int32_t engine, uint8_t xl);       // vt+0x04  0x00471b20: one
  AmmoItem* add_ammo(int32_t weapon, int32_t n);            // vt+0x08  0x00471940
  EquipmentItem* add_equipment(int32_t kind, int32_t tech, int32_t n);  // vt+0x0c  0x00471a20
  WeaponItem* add_weapon(int32_t weapon, int32_t tech);     // vt+0x10  0x00471840: one
  bool remove(ConfigItem* item, int32_t n);                 // vt+0x14  0x00471d20
  void remove_mech(int32_t mech);                           // 0x00472a30: one of the mech
  ConfigItem* find(const ConfigItem* item);   // 0x00471460: the item itself, else one like it
  ConfigItem* find_like(const ConfigItem* item);  // 0x004714c0: by vt+0x24
  void add_all(const Store& from);          // 0x004732b0: each of its items, by count
  void assign(const Store& from);           // 0x00473300: emptied, then add_all
  BaseItem* add_mech(int32_t mech);         // 0x00471c10: one of the mech (not -1 or 0x13)
  bool add_config(const Config* c);         // 0x00472cf0: each item of the loadout, by count (false: none)
  bool remove_config(const Config* c);      // 0x00472d40: each item of the loadout, by count
  ConfigItem* has_mech(int32_t mech);       // 0x00409f00: the mech's item, or none
  int32_t count_like(const ConfigItem* item) const;  // 0x00473510
  // 0x004735f0: the store (and `other`'s, given) holds the mech (unless -1)
  // and every item of the loadout by count; a full store or no loadout:
  // true (the exe's `other` == loadout test never holds).
  bool has_config(int32_t mech, const Store* other, const Config* c);
  bool has_config(int32_t mech, const Config* other, const Config* c);  // the slot's loadout as `other`
  bool take_config(int32_t mech, const Config* c);  // 0x00473440: the mech and the items out (not a full store)
  bool give_config(int32_t mech, const Config* c);  // 0x00473400: back in
  void clear();                             // the items deleted, the list emptied
  void fill_all();                          // 0x00470e30: everything the lab offers, `full` set
  uint32_t save(std::FILE* f) const;        // 0x00471790 -> 0x004717a0
  void load(std::FILE* f);                  // 0x00471530 -> 0x00471540
  int32_t count() const { return static_cast<int32_t>(items.size()); }

  uint8_t b04;                       // +0x04  the constructor's flag
  std::list<ConfigItem*> items;      // +0x08, its count +0x0c
  uint8_t full = 0;                  // +0x10  set with b04, and by store_fill
};

// A mech in a game (0x00433cc0, vtable 0x005bec38), 0x18 bytes: the player's
// own (a game's +0x20), the campaign's lancemates, Instant Action's and the
// mech lab's line-ups. Its configuration is its own (deleted with it).
struct GameSlot {
  explicit GameSlot(int32_t mech = -1, Config* config = nullptr, int32_t skin = 0);  // 0x00433cc0
  ~GameSlot();                       // 0x00433d60
  GameSlot(const GameSlot&) = delete;
  GameSlot& operator=(const GameSlot&) = delete;
  void save(Pilot& p);               // vt+0x00  0x00433ef0
  void load(Pilot& p);               // vt+0x04  0x00433f80
  void copy_from(const GameSlot& s); // 0x00433d90
  bool set_config(const Config* c);  // 0x00433df0(c, 0, 0): a copy of c, or none
  // 0x00433df0 with a store: the old loadout's items given back, the new
  // one copied out of the store (`take`), cut to what it holds.
  bool set_config(const Config* c, Store* store, bool take);

  int32_t mech;                      // +0x04
  Config* config = nullptr;          // +0x08
  int32_t x0c = 0;                   // +0x0c
  int32_t skin;                      // +0x10
  int32_t x14 = 0;                   // +0x14
};

enum GameType : int32_t { kMechLab = 0, kCampaign = 1, kInstantAction = 2, kMultiplayer = 3, kTraining = 4 };

// A game of the pilot's (vtable 0x005beca0, docs/gametypes.md): its store,
// the player's mech, and what its kind adds. Only the slots the pilot and its
// screen use are virtual here.
class Game {
 public:
  explicit Game(Pilot* p);           // the shared head of every constructor
  virtual ~Game() = default;         // vt+0x00  0x004377a0 (deleting 0x00437840)
  virtual void save();               // vt+0x18  0x00434560: the slot, then the store
  virtual void load();               // vt+0x1c  0x00434580
  // vt+0x28 (0x004b3d30): the mission load's "restored from a saved game?";
  // true for every game but the campaign's (docs/save.md).
  virtual bool restore() { return true; }
  virtual int32_t type() const { return -1; }  // vt+0x2c  0x004a5260
  // vt+0x04 .. +0x10, the briefing's game_type, db_name, scenario_name and
  // scenario_desc (campaign.md section 3): 0x0040bae0's 0 unless a game has its own.
  virtual const char* game_text(int32_t slot) { (void)slot; return nullptr; }

  Pilot* pilot;                      // +0x04
  Store store;                       // +0x08
  std::string text;                  // +0x1c  (a CString: the campaign's name or its file)
  GameSlot slot;                     // +0x20  the player's mech (3 at first)
};

// "Mech lab" (0x004373e0, vtable 0x005bee08), 0x84 bytes: the mech lab's and
// the debug test levels' game, the startup default. Saves only the head.
class LabGame : public Game {
 public:
  explicit LabGame(Pilot* p);        // 0x004373e0
  ~LabGame() override;               // 0x004374d0
  int32_t type() const override { return kMechLab; }  // 0x0040bae0
  int32_t level = 7;                 // +0x38  vt+0x50 (0x004379e0) sets it: 6 the lab, 7, 8 the dev levels
  GameSlot lineup[3];                // +0x3c, +0x54, +0x6c  mechs 3, 8, 13
};

// A campaign (0x004345a0 new, 0x00434820 read from the file; vtable
// 0x005bec40), 200 bytes. The pilot's first is a new one; the others are
// the saved games, each kept only while its file in pilots\games exists.
class CampaignGame : public Game {
 public:
  CampaignGame(Pilot* p, int32_t campaign, int32_t mission);  // 0x004345a0
  explicit CampaignGame(Pilot* p);   // 0x00434820: read from the pilot's file
  ~CampaignGame() override;          // 0x00434740
  void save() override;              // vt+0x18  0x00434990
  void load() override;              // vt+0x1c  0x00434ae0
  int32_t type() const override { return kCampaign; }  // 0x00499270
  const char* game_text(int32_t slot) override;
  const char* name();                // vt+0x0c  0x00434900: "Op N, Mission M" (into text)
  const char* path();                // vt+0x14  0x00434980: its saved game (into text)
  const char* path_for(const char* pilot_name);  // 0x00434950
  bool readable();                   // vt+0x20  0x00435330: its saved game exists
  bool restore() override;           // vt+0x28  0x004352d0: its saved game's world read (zSave)
  void merge_salvage();              // 0x00434cb0, at the end of load
  int32_t operation() const { return campaign; }  // vt+0x48  0x00437870
  int32_t mission_number() const { return mission; }  // vt+0x4c  0x00437880
  // `save` false: the world's save left to the caller (record_mission's FIX).
  CampaignGame* next_mission(bool next, bool save = true);  // vt+0x24  0x00434e00
  GameSlot* slot_at(int32_t i);      // vt+0x34  0x00435110: 0 the player's, then the lancemates
  int32_t slot_count() const;        // vt+0x38  0x00435180 (vt+0x3c 0x00437820 the same)
  // The mission start's lancemate i (player_setup): appended when it is the
  // next, else the slot there keeps its mech and loadout and takes only the
  // new one's record index (+0x14), and the new one is deleted.
  void set_lancemate(int32_t i, GameSlot* s);  // 0x004351c0
  void set_lancemate_count(int32_t n) { x9c = n; }  // 0x00435230
  void reset_lancemates(bool del);   // 0x00435240
  void copy_lancemates(const CampaignGame& from);  // 0x00434300

  uint8_t b38 = 0;                   // +0x38
  Store store3c, store50, store64, store78;  // +0x3c, +0x50, +0x64, +0x78
  std::vector<GameSlot*> lancemates; // +0x90 (+0x8c its allocator byte)
  int32_t x9c = 0;                   // +0x9c  the mission's lancemates (0x00435230; never set by the constructors)
  int32_t mission = 0;               // +0xa0
  int32_t campaign = 0;              // +0xa4  the operation, 1..4
  // Host-owned identities and graph state. Legacy binary fields retain their
  // format without a profile. Managed records append a versioned run trailer;
  // the expected run also binds the checked bundle and its filename.
  std::string content_campaign, content_mission, content_run;
  cc::content::Json content_state = cc::content::Json::object();
  bool content_placeholder = false;
  bool content_checkpoint_expected = false;
  int32_t a8 = 3;                    // +0xa8  set to +0xac on loading
  int32_t ac = 3;                    // +0xac
  uint8_t b0 = 0;                    // +0xb0
  int32_t b4[4] = {};                // +0xb4
  int32_t c4 = 0;                    // +0xc4
};

// The Instant Action settings the game keeps outside the game object and the
// pilot's file carries ([0x00605820..0x0060587f]; the Action screen's).
struct ActionSetup {
  int32_t g820[5] = {};              // 0x00605820
  int32_t g838[6] = {};              // 0x00605838
  int32_t g858[6] = {};              // 0x00605858
  int32_t g874 = 0, g878 = 0, g87c = 0;  // 0x00605874, 0x00605878, 0x0060587c
};
ActionSetup& action_setup();

// Instant Action (0x00436650, vtable 0x005bed50), 0x17c bytes: its record.
class ActionGame : public Game {
 public:
  explicit ActionGame(Pilot* p);     // 0x00436650
  ~ActionGame() override;            // 0x00436770
  void save() override;              // vt+0x18  0x00436b50
  void load() override;              // vt+0x1c  0x00436cc0
  int32_t type() const override { return kInstantAction; }  // 0x004993e0
  // 0x004378a0 (message 0x10c), 0x004487d0 (+0x38), 0x0040a840 (+0x3c), 0x00437970 (+0x40)
  const char* game_text(int32_t slot) override;

  // The line-up (0x0049dc80's): slot i (vt+0x34; 0 the player's, then
  // lineup[i - 1]), allies first, then enemies (the screen keeps them so).
  GameSlot* slot_at(int32_t i);      // vt+0x34  0x004368b0
  int32_t slot_count() const { return count + 1; }  // vt+0x38  0x00436a30
  bool ally(int32_t i) const;        // 0x00436a90: slot i is on the player's side (0 is)
  bool remove(int32_t i);            // 0x00436af0: lineup[i] (0-based) out, the rest moved up
  void drop_enemies();               // 0x00436ac0
  // A slot appended: its mech, side, a copy of the loadout and its paint;
  // null when the ten are taken.
  GameSlot* add(int32_t mech, uint8_t side, const Config* config, int32_t paint);  // 0x00436840
  // 0x004368e0 (+0x38), 0x00436950 (+0x3c), 0x004369c0 (+0x40): a copy of
  // `text` (the Action screen's world, the scenario's name and its description).
  void set_text(char** field, const char* text);

  char* x38 = nullptr;               // +0x38  the world's name (malloc'd by the Action screen, freed here)
  char* x3c = nullptr;               // +0x3c  its name (vt+0x0c, 0x0040a840: the objectives' header)
  char* x40 = nullptr;               // +0x40  its description
  int32_t mode = 0;                  // +0x44  1 endless, 2 waves (the Action screen's; not set by the constructor)
  uint8_t field_bases = 0;           // +0x48  the mission's mobile HQs made (the screen's)
  // +0x4c..+0x68, the pilot screen's lines (messages 0x184..0x18a, 0xdb7).
  int32_t player_kills = 0, lancemate_kills = 0, enemy_kills = 0;
  int32_t attack_wins = 0, attack_losses = 0, defend_wins = 0, defend_losses = 0;
  int32_t waves = 0;                 // +0x68  progression waves (the best wave in wave mode)
  int32_t x6c = 1;                   // +0x6c  the wave being fought
  int32_t x70 = 0;                   // +0x70  waves won (not set by the constructor)
  int32_t x74 = -1, x78 = 0;         // +0x74, +0x78
  int32_t count = 0;                 // +0x7c  line-up slots in use
  GameSlot lineup[10];               // +0x80, 0x18 each
  uint8_t side[10] = {};             // +0x170  each slot's: 1 an ally
};

// [0x00608e2c]: the game is team play (the connection screen's, mp_connect.h).
bool multiplayer_team_play();

// The lobby's records (docs/multiplayer-port.md, piece 11), kept by the
// multiplayer game and made again from the network's players and teams at
// each refresh (0x00498710). A score line (0x10 bytes): its text for
// SCORES_LIST and the order it sorts by.
struct ScoreLine {
  std::string text;                  // +0x00
  int32_t score = 0;                 // +0x04  kills less suicides, not under 0 (0x004361e0)
  uint32_t key = 0;                  // +0x08  the lines' order, largest first (0x00435b60)
  int32_t kind = 0;                  // +0x0c  1 a player's, 2 a team's
};

// A player or a team of the lobby's lists (vtable 0x005c7cc4); the lists'
// items point at them. vt+0 tells which.
struct LobbyEntry {
  virtual ~LobbyEntry() = default;
  virtual int32_t kind() const = 0;  // vt+0x00  1 a player (0x00499270), 2 a team (0x004993e0)
  uint32_t id = 0;                   // +0x04
};

// A player (0x00499030, vtable 0x005c7cc0), 0x30 bytes: its network record
// and data (the lobby's kind 1: skin and state, multiplayer-port.md 11).
struct LobbyPlayer : LobbyEntry {
  int32_t kind() const override { return 1; }
  uint32_t team = 0;                 // +0x08  0 none
  uint8_t host = 0;                  // +0x0c
  std::string name;                  // +0x10
  int32_t state = 2;                 // +0x14  1 in the mech lab, 3 ready (its data's; 2 without)
  uint8_t left = 0;                  // +0x18  gone from the game (0x00495b10's users)
  int32_t skin = 1;                  // +0x1c  1..8 (its data's)
  int32_t order = 0;                 // +0x20  its place in the lobby's count ([0x005e7920])
  int32_t kills = 0;                 // +0x24
  int32_t deaths = 0;                // +0x28  its suicides
  ScoreLine* score = nullptr;        // +0x2c  the game's
};

// A team (0x00499280, vtable 0x005c7cc8), 0x20 bytes; its data (8 bytes) the
// owner and the skin.
struct LobbyTeam : LobbyEntry {
  int32_t kind() const override { return 2; }
  std::string name;                  // +0x08
  int32_t count = 0;                 // +0x0c  its players, counted by the lists (0x00498710)
  int32_t skin = 0;                  // +0x10  its data's second word
  int32_t kills = 0;                 // +0x14  its players' (0x004361e0)
  uint32_t owner = 0;                // +0x18  the first to join it; its data's first word
  ScoreLine* score = nullptr;        // +0x1c  the game's
};

// Multiplayer (0x004353a0, vtable 0x005becf8), 0xb0 bytes.
class MultiplayerGame : public Game {
 public:
  explicit MultiplayerGame(Pilot* p);  // 0x004353a0
  ~MultiplayerGame() override;       // 0x00435500
  void save() override;              // vt+0x18  0x00435690
  void load() override;              // vt+0x1c  0x004357d0
  int32_t type() const override { return kMultiplayer; }  // 0x0041ec30
  GameSlot* slot_at(int32_t i) { return i == 0 ? &slot : nullptr; }  // vt+0x34  0x00437800
  int32_t world() const;
  // The briefing's texts: "M U L T I P L A Y E R" (vt+0x04, 0x00437920), the
  // world's name (vt+0x08, 0x00436500: a shipped one's message, an add-on's
  // mpinfo.zrd db_name, else "Unknown"), the session's name (vt+0x0c,
  // 0x00437950), the players as START listed them (vt+0x10, 0x00437930).
  const char* game_text(int32_t slot) override;
  void set_roster(const std::vector<std::string>& lines);  // 0x00436470: each line and "\n"
  void set_world(int32_t value);
  std::string content_multiplayer_mission;

  // The lobby's records by player or team id (0x00495b10..0x00495d20 on
  // +0x48 and +0x54), and their lists emptied (0x00495a50, 0x00495c60,
  // 0x00495d70 on +0x38).
  LobbyPlayer* lobby_player(uint32_t id);       // 0x00495b10: null none
  uint32_t lobby_team_id(uint32_t player);      // 0x00495b60: its team, 0 none
  int32_t lobby_skin(uint32_t player);          // 0x00495bb0: 0 none
  bool in_team(uint32_t player, uint32_t team);  // 0x00495c00
  LobbyTeam* lobby_team(uint32_t id);           // 0x00495d20: null none
  void clear_lobby();                           // the three, as the lists' refresh does
  // The network game's lookups and scores (0x00435910..0x00435b40, 0x004361e0,
  // 0x00435b60). A player's side in play: its place in the lobby's count,
  // or in team play its team's skin (a team is its colour); 0 none.
  uint32_t net_team(uint32_t player);           // 0x00435910
  int32_t net_skin(uint32_t player);            // 0x00435960: its own skin, or its team's in team play
  int32_t add_kills(uint32_t player, int32_t n);   // 0x004359b0: -1 no such player
  int32_t set_kills(uint32_t player, int32_t n);   // 0x004359f0
  int32_t kills_of(uint32_t player);               // 0x00435a30
  int32_t score_of(uint32_t player);               // 0x00435a50: its team's in team play
  int32_t add_deaths(uint32_t player, int32_t n);  // 0x00435ac0
  int32_t set_deaths(uint32_t player, int32_t n);  // 0x00435b00
  int32_t deaths_of(uint32_t player);              // 0x00435b40
  void rescore();                                  // 0x004361e0
  void score_texts(int32_t name_width, int32_t number_width);  // 0x00435b60: the lines, sorted

  // +0x38..+0x5c: the lobby's (pointers in the exe's lists, owned here).
  std::vector<std::unique_ptr<ScoreLine>> score_lines;      // +0x3c (its allocator +0x38)
  std::list<std::unique_ptr<LobbyPlayer>> lobby_players;    // +0x4c, its count +0x50 (at +0x48)
  std::list<std::unique_ptr<LobbyTeam>> lobby_teams;        // +0x58, its count +0x5c (at +0x54)
  // +0x60: the rules as locked (0x0049aca0 the host's, 0x00499cf0 a
  // joiner's), with a random word, and their CRC-32 (+0x88): once for the
  // host's 51 s check, dropped as FIX (trap); kept, read by nothing.
  struct Rules {
    uint32_t flags = 0;              // +0x60  the 0x20 message's (1 respawn .. 0x20 locked)
    int16_t world = 0, kills = 0, time = 0, weight = 0;  // +0x64..+0x6a  the lists' values
    int32_t seed = 0;                // +0x6c  rand()
  } rules;
  uint32_t rules_crc = 0;            // +0x88
  int32_t x70 = 0;                   // +0x70  KILLS_LIST's: the score to win (never set by the constructor; saved)
  float x74 = 10.0f;                 // +0x74  TIME_LIST's: minutes
  int32_t x78 = 90;                  // +0x78  WEIGHT_LIST's: tons
  // The network game's clock and end (0x0046ef70); the start clears them.
  float clock_next = 0;              // +0x7c  when the host sends the clock (0x11) again
  float end_time = 0;                // +0x80  (not set by the constructor)
  int32_t end_state = 0;             // +0x84  0, 1 ending, 3 ended
  uint8_t b8c = 1, b8d = 0, b8e = 0, b8f = 1;  // +0x8c..+0x8f
  int32_t x90 = 0, x94 = 1;          // +0x90, +0x94  DATABASE_LIST's world: its folder (0 c1) and number
  int32_t kills = 0, deaths = 0;     // +0x98, +0x9c  the pilot screen's line (0x27e)
  int32_t best = 0, best_count = 0;  // +0xa0, +0xa4  the best score and how many have it (0x00435b60)
  uint8_t ba8 = 0;                   // +0xa8  MOBILE_CHECKBOX's
  std::string roster;                // +0xac  (0x00436470)
};

// Training (0x00436e30, vtable 0x005beda8), 0x8c bytes.
class TrainingGame : public Game {
 public:
  explicit TrainingGame(Pilot* p);   // 0x00436e30
  ~TrainingGame() override;          // 0x00436f10
  void save() override;              // vt+0x18  0x00436fe0
  void load() override;              // vt+0x1c  0x00437020
  int32_t type() const override { return kTraining; }  // 0x00473a20
  // 0x00437980 (message 0x10b), 0x004487d0 (+0x38), 0x00437990 / 0x004379b0
  // (the mission's name and description, vt+0x4c 0x00437970 the mission)
  const char* game_text(int32_t slot) override { return text_of(slot, mission); }
  static const char* text_of(int32_t slot, int32_t mission);
  // A training mission's name and description (0x00437060, 0x004370d0: the
  // message, i within 1..4).
  static int32_t name_id(int32_t i);
  static int32_t text_id(int32_t i);

  int32_t x38 = 0;                   // +0x38
  uint8_t passed[4] = {};            // +0x3c  each training mission's PASS (mission n at +0x3b + n)
  int32_t mission = 1;               // +0x40  the one chosen, 1..4
  // +0x44: the mission's lessons (mech_objective.h), vt+0x40 0x00437140 reads
  // them, vt+0x44 0x004379d0 frees them.
  std::unique_ptr<struct TrainingLessons> lessons;
};

// The pilot (0x00477b20, vtable 0x005c37a0), 0x84 bytes; its file is
// pilots\<name>.mw3: the 12-byte header, then the games (pilot.md).
class Pilot {
 public:
  explicit Pilot(const char* name);  // 0x00477b20
  ~Pilot();                          // 0x00477e10: saved first while dirty
  Pilot(const Pilot&) = delete;
  Pilot& operator=(const Pilot&) = delete;
  bool read();                       // vt+0x00  0x004790d0
  bool write();                      // vt+0x04  0x00479390
  bool open();                       // 0x00478ce0: the file, its header checked or written
  bool write_header();               // 0x004794f0
  bool save();                       // the inline open / header / write / close of 0x004788b0 and others
  void close();
  std::string file_path() const;     // 0x00478c80: pilots\<name>.mw3
  bool rename(const char* name);     // 0x00477fe0
  bool delete_files();               // 0x004781e0: the campaigns' saved games, then its own
  void select_game(int32_t type);    // 0x00478250
  bool select_campaign(int32_t i);   // 0x00478ed0
  bool record_mission(bool next);    // 0x00478f10: true when the campaign is over
  Store* store();                    // 0x00479430: the current game's
  CampaignGame* campaign();          // 0x00479440: the current game when a campaign
  ActionGame* action();              // 0x00479460
  MultiplayerGame* multiplayer();    // 0x00479480
  TrainingGame* training_game();     // 0x004794a0
  int32_t campaign_count() const { return static_cast<int32_t>(campaigns.size()); }

  int32_t magic = 1;                 // +0x04
  int32_t version;                   // +0x08  0x00423cf0 (15) + 0x20
  int32_t x0c = 0;                   // +0x0c
  std::FILE* file = nullptr;         // +0x10  (INVALID_HANDLE_VALUE when closed)
  Handle mech = 0;                   // +0x14  the pilot's mech in a mission
  char name[0x40] = {};              // +0x18
  uint8_t dirty = 1;                 // +0x58  saved by the destructor
  uint8_t b59 = 0;                   // +0x59
  std::vector<Game*> campaigns;      // +0x60  the new one first, then the saved (+0x5c its allocator byte)
  Game* current = nullptr;           // +0x6c
  ActionGame* action_game = nullptr; // +0x70
  MultiplayerGame* multi = nullptr;  // +0x74
  TrainingGame* training = nullptr;  // +0x78
  LabGame* lab = nullptr;            // +0x7c
  int32_t x80 = 0;                   // +0x80  the mech's +0xd4 +0x1c, kept across a respawn
};

// The pilots ([0x0060860c], their count [0x00608610]) and the current one.
std::list<Pilot*>& pilots();         // 0x004779f0 makes the list
Pilot*& current_pilot();             // [0x0060861c]
Pilot* find_pilot(const char* name);  // 0x00478be0
bool same_name(const char* a, const char* b);  // pilot names that share a file
Pilot* load_pilot(const char* name, bool reload);  // 0x00478700
Pilot* create_pilot(const char* name);  // 0x004788b0: or the one already listed
bool delete_pilot(const char* name);  // 0x00477f50: and its files
void load_pilots();                  // 0x00478560: every pilots\*.mw3
void delete_other_pilots();          // 0x004785f0: all but the current one
Pilot* debug_start_campaign(int32_t campaign, int32_t mission);  // 0x00478a40, "DefaultPlayer"
// Mech3.exe 1.2 @ 0x00484870: the next mission, past the operation's last
// the next operation's first; true when the operation reaches 5 (the end).
bool campaign_advance(int32_t& operation, int32_t& mission);

}  // namespace mech3
