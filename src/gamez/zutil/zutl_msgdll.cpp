#include "gamez/zutil/zutl_msgdll.h"
#include "platform/host_path.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "gamez/zutil/zutl_msg.h"

namespace zutl {

namespace {

struct Pe {
  std::vector<uint8_t> b;
  uint32_t image_base = 0;
  struct Section {
    uint32_t va, size, raw, raw_size;
  };
  std::vector<Section> sections;
  uint32_t dir_rva[16] = {};

  bool in(size_t off, size_t n) const { return off + n <= b.size() && off + n >= off; }
  uint32_t u32(size_t off) const {
    uint32_t v = 0;
    if (in(off, 4)) std::memcpy(&v, &b[off], 4);
    return v;
  }
  uint16_t u16(size_t off) const {
    uint16_t v = 0;
    if (in(off, 2)) std::memcpy(&v, &b[off], 2);
    return v;
  }
  // The file offset of an RVA, or 0 when no section holds it.
  size_t off(uint32_t rva) const {
    for (const Section& s : sections)
      if (rva >= s.va && rva < s.va + s.size && rva - s.va < s.raw_size) return s.raw + (rva - s.va);
    return 0;
  }
  const char* str(uint32_t rva) const {
    const size_t o = off(rva);
    if (!o) return nullptr;
    for (size_t i = o; i < b.size(); ++i)
      if (b[i] == 0) return reinterpret_cast<const char*>(&b[o]);
    return nullptr;
  }

  bool parse() {
    if (!in(0, 0x40) || b[0] != 'M' || b[1] != 'Z') return false;
    const uint32_t pe = u32(0x3c);
    if (!in(pe, 24) || std::memcmp(&b[pe], "PE\0\0", 4) != 0) return false;
    const uint16_t nsec = u16(pe + 6), optsz = u16(pe + 20);
    const size_t opt = pe + 24;
    if (u16(opt) != 0x10b) return false;  // PE32
    image_base = u32(opt + 28);
    for (int i = 0; i < 16; ++i) dir_rva[i] = u32(opt + 96 + 8 * i);
    for (int i = 0; i < nsec; ++i) {
      const size_t s = opt + optsz + 40 * i;
      if (!in(s, 40)) return false;
      const uint32_t vsize = u32(s + 8), va = u32(s + 12), rsize = u32(s + 16), raw = u32(s + 20);
      sections.push_back({va, vsize > rsize ? vsize : rsize, raw, rsize});
    }
    return true;
  }
};

// The RT_MESSAGETABLE resource (type 11): every language's MESSAGE_RESOURCE_DATA
// blocks of {low id, high id, offset} and entries {length, flags, text}.
void read_messages(const Pe& pe, std::map<int32_t, std::string>& out) {
  const size_t base = pe.off(pe.dir_rva[2]);
  if (!base) return;
  auto entries = [&](size_t dir, auto&& fn) {
    const uint32_t n = static_cast<uint32_t>(pe.u16(dir + 12)) + pe.u16(dir + 14);
    for (uint32_t k = 0; k < n && k < 0x1000; ++k) fn(pe.u32(dir + 16 + 8 * k), pe.u32(dir + 20 + 8 * k));
  };
  entries(base, [&](uint32_t type, uint32_t d) {
    if (type != 11 || !(d & 0x80000000u)) return;
    entries(base + (d & 0x7fffffff), [&](uint32_t, uint32_t d2) {
      if (!(d2 & 0x80000000u)) return;
      entries(base + (d2 & 0x7fffffff), [&](uint32_t, uint32_t d3) {
        const size_t o = pe.off(pe.u32(base + d3));
        if (!o) return;
        const uint32_t blocks = pe.u32(o);
        for (uint32_t k = 0; k < blocks && k < 0x10000; ++k) {
          const uint32_t lo = pe.u32(o + 4 + 12 * k), hi = pe.u32(o + 8 + 12 * k);
          size_t p = o + pe.u32(o + 12 + 12 * k);
          for (uint32_t id = lo; id <= hi && pe.in(p, 4); ++id) {
            const uint16_t len = pe.u16(p), flags = pe.u16(p + 2);
            if (len < 4 || !pe.in(p, len)) break;
            std::string text;
            if (flags & 1) {  // UTF-16: the game's texts are Latin-1
              for (size_t i = p + 4; i + 1 < p + len; i += 2) {
                const uint16_t c = pe.u16(i);
                if (c == 0) break;
                text += static_cast<char>(c < 0x100 ? c : '?');
              }
            } else {
              for (size_t i = p + 4; i < p + len && pe.b[i]; ++i) text += static_cast<char>(pe.b[i]);
            }
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
            out[static_cast<int32_t>(id)] = text;
            p += len;
          }
        }
      });
    });
  });
}

// ZLocGetID's table: the export calls a binary search (`mov ebx, table` ...
// `cmp ebx, end`) over 8-byte {name, id} entries.
void read_ids(const Pe& pe, std::map<std::string, int32_t>& out) {
  const size_t ex = pe.off(pe.dir_rva[0]);
  if (!ex) return;
  const uint32_t nnames = pe.u32(ex + 0x18), funcs = pe.u32(ex + 0x1c), names = pe.u32(ex + 0x20),
                 ords = pe.u32(ex + 0x24);
  uint32_t fn = 0;
  for (uint32_t i = 0; i < nnames; ++i) {
    const char* n = pe.str(pe.u32(pe.off(names) + 4 * i));
    if (n && std::strcmp(n, "ZLocGetID") == 0) fn = pe.u32(pe.off(funcs) + 4 * pe.u16(pe.off(ords) + 2 * i));
  }
  if (!fn) return;
  size_t code = pe.off(fn);
  if (!code) return;
  for (size_t i = code; i < code + 16 && pe.in(i, 5); ++i) {  // follow the call into the search
    if (pe.b[i] == 0xe8) {
      code = pe.off(fn + static_cast<uint32_t>(i - code) + 5 + pe.u32(i + 1));
      break;
    }
  }
  uint32_t table = 0, end = 0;
  for (size_t i = code; code && i < code + 0xc0 && pe.in(i, 6); ++i) {
    if (!table && pe.b[i] == 0xbb) table = pe.u32(i + 1);
    if (pe.b[i] == 0x81 && pe.b[i + 1] == 0xfb) end = pe.u32(i + 2);
  }
  if (!table || end <= table || end - table > 0x100000) return;
  for (uint32_t va = table; va < end; va += 8) {
    const size_t o = pe.off(va - pe.image_base);
    if (!o) break;
    const char* name = pe.str(pe.u32(o) - pe.image_base);
    if (name) out[name] = static_cast<int32_t>(pe.u32(o + 4));
  }
}

const MessageDll* g_dll = nullptr;
char g_buffers[16][0x200];  // 0x00576040's rotating buffers
int g_next = 0;

int32_t get_id(const char* name) {
  if (!g_dll || !name) return 0;
  auto it = g_dll->ids.find(name);
  return it == g_dll->ids.end() ? 0 : it->second;
}
const char* get_text(int32_t id) {
  if (!g_dll) return nullptr;
  auto it = g_dll->text.find(id);
  if (it == g_dll->text.end()) return nullptr;
  char* b = g_buffers[g_next];
  g_next = (g_next + 1) & 15;
  std::strncpy(b, it->second.c_str(), sizeof g_buffers[0] - 1);
  b[sizeof g_buffers[0] - 1] = 0;
  return b;
}

}  // namespace

bool read_message_dll(const std::string& host_path, MessageDll& out) {
  std::ifstream f(platform::host_path(std::filesystem::u8path(host_path)), std::ios::binary);
  if (!f) return false;
  Pe pe;
  pe.b.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  if (!pe.parse()) return false;
  out = MessageDll{};
  read_messages(pe, out.text);
  read_ids(pe, out.ids);
  return !out.text.empty();
}

void install_messages(const MessageDll* dll) {
  g_dll = dll;
  messages().get_id = dll ? get_id : nullptr;
  messages().get_text = dll ? get_text : nullptr;
}

std::string format_message(int32_t id, const char* arg) {
  const char* t = messages().get_text ? messages().get_text(id) : nullptr;
  std::string s = t ? t : "";
  const size_t at = s.find("%1");
  if (at != std::string::npos) {
    size_t n = 2;  // "%1" or "%1!d!" / "%1!s!"
    if (at + 5 <= s.size() && s[at + 2] == '!' && s[at + 4] == '!') n = 5;
    s.replace(at, n, arg ? arg : "");
  }
  return s;
}

std::string format_message(int32_t id, std::initializer_list<const char*> args) {
  const char* t = messages().get_text ? messages().get_text(id) : nullptr;
  const std::string in = t ? t : "";
  std::string s;
  for (size_t i = 0; i < in.size(); ++i) {
    const size_t k = i + 1 < in.size() ? static_cast<size_t>(in[i + 1] - '1') : args.size();
    if (in[i] != '%' || k >= args.size()) {
      s += in[i];
      continue;
    }
    const char* a = args.begin()[k];
    s += a ? a : "";
    ++i;
    if (i + 1 < in.size() && in[i + 1] == '!') {  // "%1!s!", "%1!2d!": the spec skipped
      const size_t end = in.find('!', i + 2);
      if (end != std::string::npos) i = end;
    }
  }
  return s;
}

// Each %N reads its argument by its printf type, as FormatMessage does: "!d!",
// "!i!", "!u!" an int, "!x!" / "!X!" one in hex (a width or flags before the
// type kept, "!2d!"), "!s!" or none a C string; they are taken in N's order
// (the exe's 32-bit stack slots).
std::string format_message_v(int32_t id, va_list args) {
  const char* t = messages().get_text ? messages().get_text(id) : nullptr;
  int n = 0;
  std::string spec[9];
  for (const char* p = t; p && *p; ++p)
    if (p[0] == '%' && p[1] >= '1' && p[1] <= '9') {
      const int k = p[1] - '1';
      if (k + 1 > n) n = k + 1;
      if (spec[k].empty()) {
        const char* end = p[2] == '!' ? std::strchr(p + 3, '!') : nullptr;
        spec[k] = end && end > p + 3 ? std::string(p + 3, end) : "s";
      }
    }
  std::string num[9];
  const char* a[9] = {};
  for (int i = 0; i < n; ++i) {
    const std::string& f = spec[i].empty() ? std::string("s") : spec[i];
    const char c = f.back();
    if (c == 'd' || c == 'i' || c == 'u' || c == 'x' || c == 'X') {
      const int v = va_arg(args, int);
      char b[32];
      std::snprintf(b, sizeof b, ("%" + f).c_str(), v);
      num[i] = b;
      a[i] = num[i].c_str();
    } else {
      a[i] = va_arg(args, const char*);
    }
  }
  return format_message(id, {a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8]});
}

std::string format_message_f(int32_t id, ...) {
  va_list args;
  va_start(args, id);
  std::string s = format_message_v(id, args);
  va_end(args);
  return s;
}

std::string format_message_ints(int32_t id, std::initializer_list<int32_t> args) {
  std::string s[4];
  size_t n = 0;
  for (int32_t v : args)
    if (n < 4) s[n++] = std::to_string(v);
  return format_message(id, {s[0].c_str(), s[1].c_str(), s[2].c_str(), s[3].c_str()});
}

}  // namespace zutl
