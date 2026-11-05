#include "mech3/data_folder.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace mech3::data_folder {

namespace {

std::string trimmed(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
  return s;
}

std::string clean(const std::string& p) {
  std::string s = fs::path(p).lexically_normal().generic_string();
  while (s.size() > 1 && s.back() == '/') s.pop_back();
  return s;
}

bool same(const std::string& a, const std::string& b) {
  std::error_code ec;
  return clean(a) == clean(b) || fs::equivalent(a, b, ec);
}

// `inner` is under `outer`, or is it.
bool within(const std::string& inner, const std::string& outer) {
  const std::string i = clean(inner), o = clean(outer);
  return i == o || (i.size() > o.size() && i.compare(0, o.size(), o) == 0 && (i[o.size()] == '/' || o == "/"));
}

bool log_name(const std::string& name) {
  return name.size() >= 4 && name.compare(name.size() - 4, 4, ".log") == 0;
}

// What is not the player's data at a folder's top: this feature's own two files, and logs.
// And what a file manager leaves behind (Finder, Explorer), which no move copies or removes.
bool not_data(const std::string& name) {
  std::string low = name;
  for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return name == kPointer || name == kMarker || name == kCopying || log_name(name) || low == ".ds_store" ||
         low == "thumbs.db" || low == "desktop.ini";
}

// Any of the player's data in the folder; a folder that cannot be listed may hold some.
bool holds_data(const std::string& dir) {
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    if (!not_data(it->path().filename().string())) return true;
  return static_cast<bool>(ec);
}

// `from`'s files as paths relative to it, without the ones that stay.
std::vector<std::string> files_of(const std::string& from, uint64_t* bytes, std::string* error) {
  std::vector<std::string> out;
  std::error_code ec;
  const fs::path root(from);
  fs::recursive_directory_iterator it(root, ec), end;
  if (ec) {
    *error = "Cannot read " + from + ": " + ec.message();
    return out;
  }
  // Anything that cannot be read or copied stops the move: it would be lost at the switch.
  for (; it != end; it.increment(ec)) {
    const std::string rel = it->path().lexically_relative(root).generic_string();
    if (rel.find('/') == std::string::npos && not_data(rel)) continue;
    const bool link = it->is_symlink(ec);
    if (!ec && !link && it->is_directory(ec)) continue;
    if (ec || !it->is_regular_file(ec)) {
      *error = "Cannot move " + rel + " (not a plain file, a link to a folder, or unreadable). Nothing was changed.";
      return out;
    }
    const uintmax_t size = it->file_size(ec);
    if (ec) {
      *error = "Cannot read " + rel + ": " + ec.message() + ". Nothing was changed.";
      return out;
    }
    out.push_back(rel);
    *bytes += size;
  }
  // An increment that fails ends the walk here, with what is left of the folder unread.
  if (ec) {
    *error = "Cannot read a folder in " + from + ": " + ec.message() + ". Nothing was changed.";
    return out;
  }
  std::sort(out.begin(), out.end());
  return out;
}

bool same_bytes(const fs::path& a, const fs::path& b) {
  std::ifstream x(a, std::ios::binary), y(b, std::ios::binary);
  if (!x || !y) return false;
  std::vector<char> p(1 << 20), q(1 << 20);
  for (;;) {
    x.read(p.data(), static_cast<std::streamsize>(p.size()));
    y.read(q.data(), static_cast<std::streamsize>(q.size()));
    if (x.gcount() != y.gcount() || std::memcmp(p.data(), q.data(), static_cast<size_t>(x.gcount())) != 0) return false;
    if (!x || !y) return x.eof() && y.eof();
  }
}

bool write_text(const fs::path& p, const std::string& text) {
  std::ofstream o(p, std::ios::binary | std::ios::trunc);
  o << text;
  o.flush();
  return static_cast<bool>(o);
}

// The folders above `rel` deepest first, so each can go when empty.
std::vector<std::string> folders_of(const std::vector<std::string>& rels) {
  std::vector<std::string> out;
  for (const std::string& r : rels)
    for (fs::path p = fs::path(r).parent_path(); !p.empty(); p = p.parent_path())
      if (std::find(out.begin(), out.end(), p.generic_string()) == out.end()) out.push_back(p.generic_string());
  std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
  return out;
}

// mw3.cfg's values under `from` (a disc install in <folder>/install) under `to`.
bool rewrite_config(const std::string& from, const std::string& to) {
  const fs::path cfg = fs::path(to) / "mw3.cfg";
  std::error_code ec;
  if (!fs::exists(cfg, ec)) return true;
  std::vector<std::string> lines;
  {
    std::ifstream f(cfg);
    std::string line;
    while (std::getline(f, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      const size_t eq = line.find('=');
      if (eq != std::string::npos) line = line.substr(0, eq + 1) + moved_path(line.substr(eq + 1), from, to);
      lines.push_back(line);
    }
  }
  std::string text;
  for (const std::string& l : lines) text += l + "\n";
  return write_text(cfg, text);
}

}  // namespace

bool plain_name(const std::string& path) {
  return std::all_of(path.begin(), path.end(), [](char c) { return static_cast<unsigned char>(c) <= 0x7f; });
}

std::function<void()>& after_switch_for_test() {
  static std::function<void()> hook;
  return hook;
}

const uint64_t*& free_for_test() {
  static const uint64_t* value = nullptr;
  return value;
}

std::string moved_path(const std::string& path, const std::string& from, const std::string& to) {
  if (path.empty() || !fs::path(path).is_absolute() || !within(path, from)) return path;
  const std::string rest = clean(path).substr(clean(from).size());
  return (fs::path(to).generic_string() + rest);
}

std::string unusable(const std::string& dir) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec))
    return "Your MechWarrior 3 data folder is not there: " + dir + "\n\nIf it is on another drive, connect that drive.";
  if (!fs::exists(fs::path(dir) / kMarker, ec))
    return "This folder is not your MechWarrior 3 data folder (it has no " + std::string(kMarker) + "): " + dir +
           "\n\nIf it is on another drive, that drive may not be connected.";
  const fs::path probe = fs::path(dir) / ".mw3-write-test";
  bool wrote = write_text(probe, std::string(4096, 'x'));
  fs::remove(probe, ec);
  if (!wrote) return "Your MechWarrior 3 data folder cannot be written (read-only or full): " + dir;
  return {};
}

Resolved resolve(const std::string& usual) {
  Resolved r;
  r.dir = usual;
  std::ifstream f(fs::path(usual) / kPointer);
  std::string line;
  if (!f || !std::getline(f, line)) return r;
  line = trimmed(line);
  if (line.empty()) return r;
  r.dir = line, r.pointer = true;
  r.error = unusable(line);
  return r;
}

std::string destination(const std::string& picked, const std::string& usual) {
  std::error_code ec;
  if (!fs::exists(picked, ec) || same(picked, usual) || fs::is_empty(picked, ec) || fs::exists(fs::path(picked) / kMarker, ec))
    return picked;
  return (fs::path(picked) / "MechWarrior 3 Data").string();  // someone's folder, a drive's root
}

Plan plan(const std::string& from, const std::string& to, const std::string& usual) {
  Plan p;
  std::error_code ec;
  if (same(from, to)) return p.error = "Your data is already in that folder.", p;
#if defined(_WIN32)
  // SDL's dialog gives UTF-8 and fs::path reads narrow strings in the ANSI code
  // page, so another name would come out garbled (docs/program.md, The data folder).
  if (!same(to, usual) && !plain_name(to))
    return p.error = "For now the folder's name must use plain English letters: " + to, p;
#endif
  if (within(to, from)) return p.error = "That folder is inside your data folder. Pick one outside it.", p;
  if (within(from, to)) return p.error = "Your data folder is inside that folder. Pick another.", p;
  if (fs::exists(to, ec) && !fs::is_directory(to, ec)) return p.error = "That is a file, not a folder: " + to, p;
  // A copy cut off (power, a crash) left its flag: that folder is ours to finish.
  if (fs::exists(to, ec) && holds_data(to) && !fs::exists(fs::path(to) / kCopying, ec))
    return p.error = same(to, usual) ? "The usual folder still holds an older copy of your data. Remove what is in it first: " + to
                                     : "That folder already holds files: " + to + ". Pick an empty one.",
           p;
  std::string err;
  const std::vector<std::string> files = files_of(from, &p.bytes, &err);
  if (!err.empty()) return p.error = err, p;
  p.files = static_cast<int>(files.size());
  // Free space and writing, at the nearest folder that is there.
  fs::path at(to);
  while (!at.empty() && !fs::exists(at, ec) && at != at.parent_path()) at = at.parent_path();
  if (at.empty() || !fs::is_directory(at, ec)) return p.error = "That folder cannot be reached: " + to, p;
  const fs::space_info sp = fs::space(at, ec);
  if (ec) return p.error = "That folder cannot be reached: " + to, p;
  p.free = free_for_test() ? *free_for_test() : sp.available;
  const uint64_t margin = 16u << 20;
  if (p.free < p.bytes + margin) {
    char text[160];
    std::snprintf(text, sizeof text, "Not enough room there: your data needs %.1f GB, and that drive has %.1f GB free.",
                  static_cast<double>(p.bytes + margin) / 1e9, static_cast<double>(p.free) / 1e9);
    return p.error = text, p;
  }
  const fs::path probe = at / ".mw3-write-test";
  const bool wrote = write_text(probe, "x");
  fs::remove(probe, ec);
  if (!wrote) return p.error = "That folder cannot be written to: " + at.string(), p;
  return p;
}

Moved move_data(const std::string& from, const std::string& to, const std::string& usual,
                const std::function<bool(double, const std::string&)>& progress) {
  Moved m;
  const Plan p = plan(from, to, usual);
  if (!p.error.empty()) return m.error = p.error, m;
  std::error_code ec;
  const bool to_usual = same(to, usual);
  std::vector<fs::path> made_folders;  // from the top: what cleaning up may remove when empty
  for (fs::path at(to); !at.empty() && !fs::exists(at, ec) && at != at.parent_path(); at = at.parent_path())
    made_folders.insert(made_folders.begin(), at);
  std::vector<fs::path> made_files;
  bool made_marker = false;
  // Takes away only what this run made; a folder only when that leaves it empty.
  auto undo = [&] {
    std::error_code e;
    for (const fs::path& f : made_files) fs::remove(f, e);
    for (const std::string& d : folders_of(m.copied)) fs::remove(fs::path(to) / d, e);
    fs::remove(fs::path(to) / kCopying, e);
    if (made_marker) fs::remove(fs::path(to) / kMarker, e);
    for (auto it = made_folders.rbegin(); it != made_folders.rend(); ++it) fs::remove(*it, e);
    m.copied.clear();
    m.bytes = 0;
  };
  fs::create_directories(to, ec);
  if (ec) return m.error = "Cannot make " + to + ": " + ec.message(), m;
  if (!to_usual) {
    made_marker = !fs::exists(fs::path(to) / kMarker, ec);
    if (!write_text(fs::path(to) / kMarker, "The MechWarrior 3 rebuild's data folder. Leave this file here.\n")) {
      undo();
      return m.error = "Cannot write in " + to, m;
    }
  }
  if (!write_text(fs::path(to) / kCopying, "A copy of the player's data is not finished here.\n")) {
    undo();
    return m.error = "Cannot write in " + to, m;
  }
  uint64_t all = 0;
  std::string err;
  const std::vector<std::string> files = files_of(from, &all, &err);
  if (!err.empty()) return undo(), m.error = err, m;
  uint64_t done = 0;
  for (const std::string& rel : files) {
    const fs::path src = fs::path(from) / rel, dst = fs::path(to) / rel;
    if (progress && !progress(all ? static_cast<double>(done) / static_cast<double>(all) : 0.0, rel)) {
      undo();
      m.cancelled = true;
      return m;
    }
    fs::create_directories(dst.parent_path(), ec);
    if (!ec) {
      made_files.push_back(dst);
      m.copied.push_back(rel);
      fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    }
    if (ec) {
      const std::string why = ec.message();
      undo();
      return m.error = "Copying " + rel + " failed: " + why + ". Nothing was changed.", m;
    }
    if (!same_bytes(src, dst)) {
      undo();
      return m.error = "The copy of " + rel + " does not match the original. Nothing was changed.", m;
    }
    const uint64_t size = fs::file_size(src, ec);
    done += size;
    m.bytes += size;
  }
  if (!rewrite_config(from, to)) {
    undo();
    return m.error = "Cannot write mw3.cfg in " + to + ". Nothing was changed.", m;
  }
  // The switch, last: a new name moved into place, or the usual folder needing none.
  // Cut off before it, the pointer still names the old folder, which stays in use.
  const fs::path pointer = fs::path(usual) / kPointer;
  const fs::path next = fs::path(usual) / (std::string(kPointer) + ".new");
  // As it was, to put back if the switch turns out wrong.
  const bool had_pointer = fs::exists(pointer, ec);
  std::string old_pointer;
  if (had_pointer) {
    std::ifstream f(pointer, std::ios::binary);
    std::stringstream text;
    text << f.rdbuf();
    old_pointer = text.str();
  }
  bool switched;
  if (to_usual) {
    fs::remove(pointer, ec);
    switched = !ec;
  } else {
    fs::create_directories(usual, ec);
    switched = write_text(next, to + "\n");
    if (switched) fs::rename(next, pointer, ec), switched = !ec;
    if (!switched) fs::remove(next, ec);
  }
  if (after_switch_for_test()) after_switch_for_test()();
  const Resolved now = resolve(usual);
  if (!switched || !now.error.empty() || !same(now.dir, to)) {
    // The pointer back first: undo takes away the folder it may be naming.
    bool back;
    if (had_pointer) {
      back = write_text(next, old_pointer);
      if (back) fs::rename(next, pointer, ec), back = !ec;
      if (!back) fs::remove(next, ec);
    } else {
      fs::remove(pointer, ec);
      back = !ec && !fs::exists(pointer, ec);
    }
    if (!back) {  // both copies stay: the pointer may name this one
      m.copied.clear();
      return m.error = "Cannot switch over to " + to + ", or switch back. Both copies are kept; check " + pointer.string() + ".", m;
    }
    undo();
    return m.error = "Cannot switch over to " + to + " (the note in " + usual + " cannot be written). Nothing was changed.", m;
  }
  fs::remove(fs::path(to) / kCopying, ec);
  m.ok = true;
  return m;
}

Removed remove_old(const std::string& from, const std::string& to, const std::vector<std::string>& copied,
                   const std::string& usual) {
  Removed r;
  std::error_code ec;
  const Resolved now = resolve(usual);
  if (!now.error.empty() || !same(now.dir, to) || same(from, to))
    return r.error = "Your data folder is not the new one, so the old copy was kept.", r;
  // Only a folder that is ours: the usual one or one with our marker.
  if (!same(from, usual) && !fs::exists(fs::path(from) / kMarker, ec))
    return r.error = "That folder is not marked as ours, so the old copy was kept.", r;
  for (const std::string& rel : copied)
    if (!fs::is_regular_file(fs::path(to) / rel, ec))
      return r.error = "The new copy lacks " + rel + ", so the old copy was kept.", r;
  for (const std::string& rel : copied) {
    if (fs::remove(fs::path(from) / rel, ec)) ++r.files;
    else if (ec) r.failed.push_back(rel);
  }
  for (const std::string& d : folders_of(copied)) fs::remove(fs::path(from) / d, ec);  // only when empty
  if (!same(from, usual) && fs::exists(fs::path(from) / kMarker, ec) && r.failed.empty()) {
    fs::remove(fs::path(from) / kMarker, ec);
    fs::remove(from, ec);  // only when empty: logs keep it
  }
  return r;
}

}  // namespace mech3::data_folder
