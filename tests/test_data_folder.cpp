// The data folder elsewhere (mech3/data_folder.h; docs/program.md, The data folder):
// a move's copy, check and switch, and every way it must leave things as they were.
#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "check.h"
#include "mech3/data_folder.h"

namespace fs = std::filesystem;
using namespace mech3::data_folder;

namespace {

std::string put(const std::string& path, const std::string& text) {
  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  std::ofstream(path, std::ios::binary) << text;
  return path;
}
std::string read(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

// A usual folder with a player's data, and a place for the rest.
struct Scene {
  std::string root, usual;
  explicit Scene(const char* name) {
    root = fs::path(MW3_TEST_TMP).generic_string() + "/data_folder_" + name;  // moved_path writes /
    std::error_code ec;
    fs::permissions(root, fs::perms::owner_all, fs::perm_options::add, ec);
    fs::remove_all(root, ec);
    usual = root + "/usual";
    put(usual + "/mw3.cfg", "install=" + usual + "/install\nwindowed=1\nmw1_game=/somewhere/else\n");
    put(usual + "/pilots/a.pil", "pilot");
    put(usual + "/install/zbd/reader.zbd", std::string(70000, 'r'));
    put(usual + "/mw3.log", "the log");
  }
};
bool pointer_is_unset(const Scene& s) { return !fs::exists(s.usual + "/" + kPointer); }

}  // namespace

TEST(data_folder_no_pointer_is_the_usual_folder) {
  Scene s("none");
  const Resolved r = resolve(s.usual);
  EXPECT(r.dir == s.usual && !r.pointer && r.error.empty());
  EXPECT(fs::directory_iterator(s.usual) != fs::directory_iterator());
  EXPECT(pointer_is_unset(s));  // resolving writes nothing
}

TEST(data_folder_move_copies_checks_and_switches) {
  Scene s("move");
  const std::string to = s.root + "/drive/mine";
  double last = -1;
  const Moved m = move_data(s.usual, to, s.usual, [&](double at, const std::string&) { return last = at, true; });
  EXPECT(m.ok && m.error.empty() && m.copied.size() == 3);
  EXPECT(last >= 0);
  const Resolved r = resolve(s.usual);
  EXPECT(r.pointer && r.dir == to && r.error.empty());
  EXPECT(read(to + "/pilots/a.pil") == "pilot" && fs::file_size(to + "/install/zbd/reader.zbd") == 70000);
  EXPECT(fs::exists(to + "/" + kMarker) && !fs::exists(to + "/" + kCopying));
  // The install inside it follows; a path elsewhere does not; the logs stay.
  const std::string cfg = read(to + "/mw3.cfg");
  EXPECT(cfg.find("install=" + to + "/install\n") != std::string::npos && cfg.find("mw1_game=/somewhere/else") != std::string::npos);
  EXPECT(!fs::exists(to + "/mw3.log") && fs::exists(s.usual + "/mw3.log"));
  // The old copy stays until asked; asked, only what was copied goes.
  EXPECT(fs::exists(s.usual + "/pilots/a.pil"));
  put(s.usual + "/pilots/made_since.pil", "not copied");
  const Removed d = remove_old(s.usual, to, m.copied, s.usual);
  EXPECT(d.error.empty() && d.failed.empty() && d.files == 3);
  EXPECT(!fs::exists(s.usual + "/pilots/a.pil") && !fs::exists(s.usual + "/install"));
  EXPECT(fs::exists(s.usual + "/pilots/made_since.pil") && fs::exists(s.usual + "/mw3.log"));
  EXPECT(fs::exists(s.usual + "/" + kPointer));
}

TEST(data_folder_moves_back_to_the_usual_folder) {
  Scene s("back");
  const std::string to = s.root + "/drive/mine";
  const Moved m = move_data(s.usual, to, s.usual, nullptr);
  EXPECT(m.ok);
  EXPECT(remove_old(s.usual, to, m.copied, s.usual).error.empty());
  const Moved b = move_data(to, s.usual, s.usual, nullptr);
  EXPECT(b.ok && b.error.empty());
  EXPECT(pointer_is_unset(s) && resolve(s.usual).dir == s.usual && !resolve(s.usual).pointer);
  EXPECT(read(s.usual + "/mw3.cfg").find("install=" + s.usual + "/install\n") != std::string::npos);
  // The marked folder removed once empty.
  EXPECT(remove_old(to, s.usual, b.copied, s.usual).error.empty());
  EXPECT(!fs::exists(to));
}

TEST(data_folder_a_target_holding_files_is_refused_and_left_alone) {
  Scene s("foreign");
  const std::string to = s.root + "/theirs";
  put(to + "/their_file.txt", "keep me");
  EXPECT(fs::path(destination(to, s.usual)) == fs::path(to + "/MechWarrior 3 Data"));  // a drive's root: a folder inside it
  const Plan p = plan(s.usual, to, s.usual);
  EXPECT(p.error.find("already holds files") != std::string::npos);
  const Moved m = move_data(s.usual, to, s.usual, nullptr);
  EXPECT(!m.ok && !m.error.empty() && pointer_is_unset(s));
  EXPECT(read(to + "/their_file.txt") == "keep me" && !fs::exists(to + "/" + kMarker));
  // And cleaning up after a failure inside a folder inside it leaves theirs.
  const Moved in = move_data(s.usual, destination(to, s.usual), s.usual, nullptr);
  EXPECT(in.ok);
  EXPECT(read(to + "/their_file.txt") == "keep me");
}

TEST(data_folder_a_full_target_is_refused_before_copying) {
  Scene s("full");
  const std::string to = s.root + "/small";
  const uint64_t none = 1000;
  free_for_test() = &none;
  const Plan p = plan(s.usual, to, s.usual);
  const Moved m = move_data(s.usual, to, s.usual, nullptr);
  free_for_test() = nullptr;
  EXPECT(p.error.find("Not enough room") != std::string::npos);
  EXPECT(!m.ok && !fs::exists(to) && pointer_is_unset(s));
}

#if !defined(_WIN32)
// Permissions do not stop root; skip there.
TEST(data_folder_a_read_only_target_is_refused) {
  if (geteuid() == 0) return;
  Scene s("readonly");
  const std::string to = s.root + "/ro";
  fs::create_directories(to);
  fs::permissions(to, fs::perms::owner_read | fs::perms::owner_exec);
  const Plan p = plan(s.usual, to, s.usual);
  const Moved m = move_data(s.usual, to, s.usual, nullptr);
  fs::permissions(to, fs::perms::owner_all);
  EXPECT(p.error.find("cannot be written") != std::string::npos);
  EXPECT(!m.ok && pointer_is_unset(s) && fs::is_empty(to));
}

// A failure partway, and a cancel: all that was made is gone, the old folder
// is whole and still in use.
TEST(data_folder_failed_or_cancelled_changes_nothing) {
  if (geteuid() == 0) return;
  Scene s("failed");
  const std::string to = s.root + "/drive/mine";
  chmod((s.usual + "/pilots/a.pil").c_str(), 0);
  const Moved bad = move_data(s.usual, to, s.usual, nullptr);
  chmod((s.usual + "/pilots/a.pil").c_str(), 0644);
  EXPECT(!bad.ok && bad.error.find("a.pil") != std::string::npos);
  EXPECT(pointer_is_unset(s) && resolve(s.usual).dir == s.usual);
  EXPECT(!fs::exists(to) && !fs::exists(s.root + "/drive"));
  int calls = 0;
  const Moved stopped = move_data(s.usual, to, s.usual, [&](double, const std::string&) { return ++calls < 2; });
  EXPECT(stopped.cancelled && !stopped.ok && stopped.copied.empty());
  EXPECT(pointer_is_unset(s) && !fs::exists(to));
  EXPECT(read(s.usual + "/pilots/a.pil") == "pilot");
  // Removing is refused while the pointer does not name the new folder.
  EXPECT(!remove_old(s.usual, to, {"pilots/a.pil"}, s.usual).error.empty());
  EXPECT(fs::exists(s.usual + "/pilots/a.pil"));
}
#endif

#if !defined(_WIN32)
// A subfolder that cannot be read ends the walk without an error on the
// iterator's own terms: the move must stop, not lose what is in it.
TEST(data_folder_an_unreadable_subfolder_stops_the_move) {
  if (geteuid() == 0) return;
  Scene s("unreadable");
  const std::string to = s.root + "/drive/mine", locked = s.usual + "/saves";
  put(locked + "/game.sav", "a save");
  chmod(locked.c_str(), 0);
  const Plan p = plan(s.usual, to, s.usual);
  const Moved m = move_data(s.usual, to, s.usual, nullptr);
  chmod(locked.c_str(), 0755);
  EXPECT(!p.error.empty() && p.error.find("Cannot read a folder") != std::string::npos);
  EXPECT(!m.ok && !m.error.empty() && pointer_is_unset(s) && !fs::exists(to));
  EXPECT(read(locked + "/game.sav") == "a save");
}

// A target that cannot be listed may hold anything.
TEST(data_folder_a_target_that_cannot_be_listed_is_refused) {
  if (geteuid() == 0) return;
  Scene s("unlisted");
  const std::string to = s.root + "/theirs";
  put(to + "/their_file.txt", "keep me");
  chmod(to.c_str(), 0300);  // writable, so only the listing can refuse it
  const Plan p = plan(s.usual, to, s.usual);
  chmod(to.c_str(), 0755);
  EXPECT(p.error.find("already holds files") != std::string::npos);
  EXPECT(read(to + "/their_file.txt") == "keep me");
}

// A link to a folder is not walked: its files would be lost at the switch.
TEST(data_folder_a_link_to_a_folder_stops_the_move) {
  Scene s("link");
  std::error_code ec;
  put(s.root + "/elsewhere/track02.ogg", "music");
  fs::create_directory_symlink(s.root + "/elsewhere", s.usual + "/music", ec);
  EXPECT(!ec);
  EXPECT(plan(s.usual, s.root + "/drive/mine", s.usual).error.find("music") != std::string::npos);
}
#endif

// A switch that turns out wrong: the pointer as it was, then the copy away.
TEST(data_folder_a_failed_switch_puts_the_pointer_back) {
  Scene s("switch");
  const std::string to = s.root + "/drive/mine";
  after_switch_for_test() = [&] { fs::remove(to + "/" + kMarker); };
  const Moved none = move_data(s.usual, to, s.usual, nullptr);
  after_switch_for_test() = nullptr;
  EXPECT(!none.ok && none.error.find("Nothing was changed") != std::string::npos);
  EXPECT(pointer_is_unset(s) && resolve(s.usual).dir == s.usual && resolve(s.usual).error.empty());
  EXPECT(!fs::exists(to));
  // With a pointer already there, from a folder it names to another.
  const Moved first = move_data(s.usual, to, s.usual, nullptr);
  EXPECT(first.ok);
  const std::string other = s.root + "/drive2/mine";
  after_switch_for_test() = [&] { fs::remove(other + "/" + kMarker); };
  const Moved second = move_data(to, other, s.usual, nullptr);
  after_switch_for_test() = nullptr;
  EXPECT(!second.ok && resolve(s.usual).dir == to && resolve(s.usual).error.empty());
  EXPECT(!fs::exists(other) && read(to + "/pilots/a.pil") == "pilot");
}

// A file manager's leavings do not block moving back, and nothing touches them.
TEST(data_folder_a_file_managers_leavings_are_not_data) {
  Scene s("leavings");
  const std::string to = s.root + "/drive/mine";
  put(s.usual + "/.DS_Store", "finder");
  put(s.usual + "/pilots/.DS_Store", "in the player's own folder: copied, so removed with it");
  const Moved m = move_data(s.usual, to, s.usual, nullptr);
  EXPECT(m.ok && !fs::exists(to + "/.DS_Store"));
  EXPECT(remove_old(s.usual, to, m.copied, s.usual).error.empty());
  EXPECT(read(s.usual + "/.DS_Store") == "finder");  // not copied, so not removed
  EXPECT(fs::exists(to + "/pilots/.DS_Store") && !fs::exists(s.usual + "/pilots"));
  put(s.usual + "/Thumbs.db", "explorer");
  put(s.usual + "/desktop.ini", "explorer");
  const Moved b = move_data(to, s.usual, s.usual, nullptr);
  EXPECT(b.ok && b.error.empty() && pointer_is_unset(s));
  EXPECT(read(s.usual + "/Thumbs.db") == "explorer" && read(s.usual + "/.DS_Store") == "finder");
  EXPECT(fs::exists(s.usual + "/pilots/a.pil"));
}

TEST(data_folder_plain_names) {
  EXPECT(plain_name("E:/Games/MechWarrior 3 Data"));
  EXPECT(!plain_name("E:/Jeux \xc3\xa9"));
  EXPECT(plain_name(""));
}

// A copy cut off (power, a crash): the pointer never moved, the old folder is
// in use, and the half copy, flagged, may be finished by a new move.
TEST(data_folder_an_interrupted_copy_leaves_the_old_folder_in_use) {
  Scene s("cut");
  const std::string to = s.root + "/drive/mine";
  put(to + "/" + kMarker, "ours");
  put(to + "/" + kCopying, "flag");
  put(to + "/pilots/a.pil", "half");
  EXPECT(pointer_is_unset(s) && resolve(s.usual).dir == s.usual);
  const Moved m = move_data(s.usual, to, s.usual, nullptr);
  EXPECT(m.ok && read(to + "/pilots/a.pil") == "pilot" && !fs::exists(to + "/" + kCopying));
}

TEST(data_folder_a_missing_drive_is_said_and_makes_nothing) {
  Scene s("missing");
  const std::string to = s.root + "/gone/mine";
  put(s.usual + "/" + kPointer, to + "\n");
  const Resolved r = resolve(s.usual);
  EXPECT(r.pointer && r.dir == to && r.error.find("not there") != std::string::npos);
  EXPECT(!fs::exists(to) && !fs::exists(s.root + "/gone"));
  // A mount point with the drive away: the folder there, but not ours.
  fs::create_directories(to);
  EXPECT(resolve(s.usual).error.find("not your MechWarrior 3 data folder") != std::string::npos);
  EXPECT(fs::is_empty(to));
}

TEST(data_folder_remove_old_touches_only_a_folder_of_ours) {
  Scene s("ours");
  const std::string to = s.root + "/drive/mine";
  const Moved m = move_data(s.usual, to, s.usual, nullptr);
  EXPECT(m.ok);
  const std::string other = s.root + "/other";
  put(other + "/pilots/a.pil", "someone else's");
  const Removed d = remove_old(other, to, {"pilots/a.pil"}, s.usual);
  EXPECT(!d.error.empty() && read(other + "/pilots/a.pil") == "someone else's");
  // The pointer not naming the new folder (the copy whole): nothing goes.
  fs::remove(s.usual + "/" + kPointer);
  EXPECT(!remove_old(s.usual, to, m.copied, s.usual).error.empty());
  EXPECT(fs::exists(s.usual + "/pilots/a.pil"));
  put(s.usual + "/" + kPointer, to + "\n");
  // The new copy short of a file: nothing goes.
  fs::remove(to + "/pilots/a.pil");
  EXPECT(!remove_old(s.usual, to, m.copied, s.usual).error.empty());
  EXPECT(fs::exists(s.usual + "/pilots/a.pil"));
}

TEST(data_folder_nested_folders_are_refused) {
  Scene s("nested");
  EXPECT(!plan(s.usual, s.usual, s.usual).error.empty());
  EXPECT(!plan(s.usual, s.usual + "/inner", s.usual).error.empty());
  EXPECT(!plan(s.usual, s.root, s.usual).error.empty());
}
