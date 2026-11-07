#include "platform/reg_file.h"
#include "platform/host_path.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace platform {

namespace {
std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// A section's key: which root, and whether its path is the game's. "SOFTWARE\
// WOW6432Node\..." (a 32-bit program's key on 64-bit Windows) counts as the same.
int section_root(std::string sec, const std::string& key) {
  std::string path;
  int root = -1;
  for (const char* p : {"hkey_current_user\\", "hkcu\\"})
    if (lower(sec).rfind(p, 0) == 0) root = 1, path = sec.substr(std::strlen(p));
  for (const char* p : {"hkey_local_machine\\", "hklm\\"})
    if (lower(sec).rfind(p, 0) == 0) root = 0, path = sec.substr(std::strlen(p));
  if (root < 0) return -1;
  std::string l = lower(path);
  const std::string wow = "software\\wow6432node\\";
  if (l.rfind(wow, 0) == 0) l = "software\\" + l.substr(wow.size());
  return l == lower(key) ? root : -1;
}

// UTF-16LE (regedit's export) to bytes; the names and values are ASCII.
std::string narrow(const std::string& raw) {
  if (raw.size() >= 2 && static_cast<uint8_t>(raw[0]) == 0xff && static_cast<uint8_t>(raw[1]) == 0xfe) {
    std::string out;
    for (size_t i = 2; i + 1 < raw.size(); i += 2) out += raw[i];
    return out;
  }
  return raw;
}

bool hex_byte(const std::string& s, size_t& i, uint8_t* out) {
  while (i < s.size() && (s[i] == ',' || std::isspace(static_cast<unsigned char>(s[i])))) ++i;
  if (i >= s.size()) return false;
  unsigned v = 0;
  int n = 0;
  while (i < s.size() && std::isxdigit(static_cast<unsigned char>(s[i])) && n < 2) {
    v = v * 16 + static_cast<unsigned>(std::isdigit(static_cast<unsigned char>(s[i])) ? s[i] - '0'
                                                                                      : std::tolower(s[i]) - 'a' + 10);
    ++i, ++n;
  }
  if (n == 0) return false;
  *out = static_cast<uint8_t>(v);
  return true;
}
}  // namespace

RegFile::~RegFile() {
  if (dirty_) flush();
}

bool RegFile::parse(const std::string& raw, const std::string& key) {
  keys_[0].clear();
  keys_[1].clear();
  std::istringstream in(narrow(raw));
  std::string line, joined;
  int root = -1;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && line.back() == '\\') {  // a hex value continues
      joined += line.substr(0, line.size() - 1);
      continue;
    }
    joined += line;
    std::string l = joined;
    joined.clear();
    size_t a = l.find_first_not_of(" \t");
    if (a == std::string::npos || l[a] == ';') continue;
    l = l.substr(a);
    if (l[0] == '[') {
      size_t b = l.rfind(']');
      root = b == std::string::npos ? -1 : section_root(l.substr(1, b - 1), key);
      continue;
    }
    if (root < 0 || l[0] != '"') continue;
    std::string name;
    size_t i = 1;
    for (; i < l.size() && l[i] != '"'; ++i) {
      if (l[i] == '\\' && i + 1 < l.size()) ++i;
      name += l[i];
    }
    if (i >= l.size() || i + 1 >= l.size() || l[i + 1] != '=') continue;
    std::string v = l.substr(i + 2);
    Value val;
    val.name = name;
    if (lower(v).rfind("dword:", 0) == 0) {
      uint32_t d = static_cast<uint32_t>(std::strtoul(v.c_str() + 6, nullptr, 16));
      val.type = 4;
      for (int k = 0; k < 4; ++k) val.bytes.push_back(static_cast<uint8_t>(d >> (8 * k)));
    } else if (lower(v).rfind("hex:", 0) == 0) {
      val.type = 3;
      size_t k = 4;
      uint8_t byte;
      while (hex_byte(v, k, &byte)) val.bytes.push_back(byte);
    } else {
      continue;  // strings and the other types: not the game's
    }
    keys_[root][lower(name)] = val;
  }
  return true;
}

std::string RegFile::format(const std::string& key) const {
  std::ostringstream out;
  out << "Windows Registry Editor Version 5.00\n";
  out << "; MechWarrior 3's settings (the rebuild keeps the registry here; docs/settings.md)\n";
  const char* roots[2] = {"HKEY_LOCAL_MACHINE", "HKEY_CURRENT_USER"};
  for (int r : {1, 0}) {
    if (keys_[r].empty()) continue;
    out << "\n[" << roots[r] << "\\" << key << "]\n";
    for (const auto& kv : keys_[r]) {
      const Value& v = kv.second;
      out << '"';
      for (char c : v.name) {
        if (c == '"' || c == '\\') out << '\\';
        out << c;
      }
      out << "\"=";
      char buf[16];
      if (v.type == 4 && v.bytes.size() == 4) {
        uint32_t d = 0;
        for (int k = 0; k < 4; ++k) d |= static_cast<uint32_t>(v.bytes[k]) << (8 * k);
        std::snprintf(buf, sizeof buf, "dword:%08x", d);
        out << buf << "\n";
        continue;
      }
      out << "hex:";
      size_t col = v.name.size() + 7;
      for (size_t k = 0; k < v.bytes.size(); ++k) {
        std::snprintf(buf, sizeof buf, "%02x", v.bytes[k]);
        out << buf;
        col += 3;
        if (k + 1 < v.bytes.size()) {
          out << ',';
          if (col > 76) out << "\\\n  ", col = 2;
        }
      }
      out << "\n";
    }
  }
  return out.str();
}

void RegFile::load(const std::string& key) {
  key_ = key;
  std::ifstream f(host_path(std::filesystem::u8path(path_)), std::ios::binary);
  if (!f) {
    keys_[0].clear();
    keys_[1].clear();
    return;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  parse(ss.str(), key);
}

void RegFile::flush() {
  dirty_ = false;
  std::ofstream f(host_path(std::filesystem::u8path(path_)), std::ios::binary | std::ios::trunc);
  if (!f) {
    std::fprintf(stderr, "mw3: cannot write the settings file %s\n", path_.c_str());
    return;
  }
  f << format(key_);
}

// The file stands for both keys: no installer made HKLM's (FIX (A)).
bool RegFile::open(const std::string& key, bool per_user, bool write) {
  (void)write;
  if (per_user || key != key_ || (keys_[0].empty() && keys_[1].empty())) load(key);
  return true;
}

bool RegFile::query(bool per_user, const std::string& name, uint32_t* size) {
  auto it = keys_[per_user ? 1 : 0].find(lower(name));
  if (it == keys_[per_user ? 1 : 0].end()) return false;
  *size = static_cast<uint32_t>(it->second.bytes.size());
  return true;
}

bool RegFile::read(bool per_user, const std::string& name, void* data, uint32_t* size) {
  auto it = keys_[per_user ? 1 : 0].find(lower(name));
  if (it == keys_[per_user ? 1 : 0].end() || it->second.bytes.size() > *size) return false;
  std::memcpy(data, it->second.bytes.data(), it->second.bytes.size());
  *size = static_cast<uint32_t>(it->second.bytes.size());
  return true;
}

bool RegFile::write(bool per_user, const std::string& name, uint32_t type, const void* data, uint32_t size) {
  Value v;
  v.name = name;
  v.type = type;
  v.bytes.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
  keys_[per_user ? 1 : 0][lower(name)] = std::move(v);
  dirty_ = true;
  return true;
}

void RegFile::close(bool) {
  if (dirty_) flush();
}

}  // namespace platform
