// A stand-in for IFORCE2.dll that adds rows to the mech lab's weapon table in memory, leaving Mech3.exe
// untouched on disk. Its exports forward to the renamed original (iforce2_proxy.def). See ../README.md.
//
// It runs before the game's entry point. Whatever goes wrong, it changes nothing, writes one line to
// weapon-patch.log beside it, and lets the game run as it is: it never fails the load.
#include <windows.h>

#define IMAGE 0x400000u
#define TABLE 0x5e64f0u
#define ROW 0x18u
#define OLD_ROWS 37u
#define MAX_ROWS 127u  // the count compares are signed imm8
#define MAX_FAMILY 14u
#define MAX_NAME 31u
#define MAX_ROW_FILE 0x10000u
#define MAX_MSG_FILE 0x1000000u

// Each is the instruction's address; `cmp r32, imm8`, its ModRM, then the count.
static const struct { DWORD at; BYTE modrm; } COUNTS[] = {
    {0x44b37b, 0xfd}, {0x45de3b, 0xff}, {0x45deef, 0xff}, {0x470ffe, 0xfe}, {0x484904, 0xf9},
    {0x484924, 0xf9}, {0x484944, 0xf9}, {0x484964, 0xf9}, {0x4b4bfe, 0xfe},
};
#define COUNT_SITES (sizeof COUNTS / sizeof COUNTS[0])

// The address of each disp32 or imm32 that points into the table, with its offset into a row.
static const struct { DWORD at; DWORD offset; } REFS[] = {
    {0x4848b8, 0x00}, {0x48490f, 0x00}, {0x48492f, 0x04}, {0x4853c6, 0x08},
    {0x4848e3, 0x0c}, {0x48494f, 0x10}, {0x48496f, 0x14},
};
#define REF_SITES (sizeof REFS / sizeof REFS[0])
#define END_REF 0x4848cfu  // the name lookup's end: the table's end, not a field
#define SITES (COUNT_SITES + REF_SITES + 1)

// Found by the installer before it removes this file: it removes only what carries this.
static const char MARKER[] = "weapon-patch IFORCE2 proxy";

typedef struct {
  char name[MAX_NAME + 1];
  DWORD family, tech, ammo, label;
} Row;

static char g_dir[MAX_PATH];
static DWORD g_dir_len;

static DWORD str_len(const char* s) {
  DWORD n = 0;
  while (s[n]) n++;
  return n;
}

static BYTE lower(BYTE c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int same_name(const char* a, const char* b) {
  for (; lower(*a) == lower(*b); a++, b++)
    if (!*a) return 1;
  return 0;
}

// The path of a file beside this DLL; 0 when it does not fit.
static int beside(const char* file, char* out, DWORD size) {
  DWORD n = str_len(file), i;
  if (g_dir_len + n + 1 > size) return 0;
  for (i = 0; i < g_dir_len; i++) out[i] = g_dir[i];
  for (i = 0; i <= n; i++) out[g_dir_len + i] = file[i];
  return 1;
}

static char* put_str(char* p, const char* s) {
  while (*s) *p++ = *s++;
  return p;
}

static char* put_num(char* p, DWORD v) {
  char digits[10];
  int n = 0;
  do {
    digits[n++] = '0' + v % 10;
    v /= 10;
  } while (v);
  while (n) *p++ = digits[--n];
  return p;
}

// The one line of this run. A log that can't be written is not an error.
static void log_line(const char* what, DWORD number, const char* detail) {
  char path[MAX_PATH], line[256];
  char* p = line;
  HANDLE f;
  DWORD done;
  if (!beside("weapon-patch.log", path, sizeof path)) return;
  p = put_str(p, MARKER);
  p = put_str(p, ": ");
  p = put_str(p, what);
  if (number) {
    *p++ = ' ';
    p = put_num(p, number);
  }
  if (detail) {
    p = put_str(p, ": ");
    p = put_str(p, detail);
  }
  *p++ = '\r';
  *p++ = '\n';
  f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return;
  WriteFile(f, line, (DWORD)(p - line), &done, NULL);
  CloseHandle(f);
}

// A whole file in pages of its own, with a NUL after it; the caller frees it. NULL when missing or too big.
static BYTE* read_file(const char* path, DWORD limit, DWORD* size) {
  HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  BYTE* data = NULL;
  DWORD high = 0, n, got = 0;
  if (f == INVALID_HANDLE_VALUE) return NULL;
  n = GetFileSize(f, &high);
  if (n != INVALID_FILE_SIZE && !high && n <= limit) {
    data = VirtualAlloc(NULL, n + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (data && (!ReadFile(f, data, n, &got, NULL) || got != n)) {
      VirtualFree(data, 0, MEM_RELEASE);
      data = NULL;
    }
  }
  CloseHandle(f);
  *size = n;
  return data;
}

// ---- the exe ----

static BYTE at8(DWORD va) { return *(const BYTE*)(ULONG_PTR)va; }
static DWORD at32(DWORD va) { return *(const DWORD UNALIGNED*)(ULONG_PTR)va; }

// Mech3.exe 1.2's code at every site, or nothing is touched. Reads stay inside the exe's image.
static int known_exe(void) {
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)GetModuleHandleA(NULL);
  const IMAGE_NT_HEADERS32* nt;
  DWORD i;
  if ((ULONG_PTR)dos != IMAGE || dos->e_magic != IMAGE_DOS_SIGNATURE || (DWORD)dos->e_lfanew > 0x1000) return 0;
  nt = (const IMAGE_NT_HEADERS32*)((const BYTE*)dos + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.SizeOfImage < TABLE + OLD_ROWS * ROW - IMAGE) return 0;
  for (i = 0; i < COUNT_SITES; i++)
    if (at8(COUNTS[i].at) != 0x83 || at8(COUNTS[i].at + 1) != COUNTS[i].modrm || at8(COUNTS[i].at + 2) != OLD_ROWS)
      return 0;
  for (i = 0; i < REF_SITES; i++)
    if (at32(REFS[i].at) != TABLE + REFS[i].offset) return 0;
  return at32(END_REF) == TABLE + OLD_ROWS * ROW;
}

// A row's name against the game's, which the site check has made safe to read.
static int game_has_name(const char* name) {
  DWORD i;
  for (i = 0; i < OLD_ROWS; i++)
    if (same_name(name, (const char*)(ULONG_PTR)at32(TABLE + i * ROW))) return 1;
  return 0;
}

// ---- the row file, the same grammar as patch_weapons.py ----

// Words on a NUL-ended line, split in place; returns how many, up to max.
static DWORD split(char* p, char** words, DWORD max) {
  DWORD n = 0;
  for (;;) {
    while (*p == ' ' || *p == '\t' || *p == '\r') p++;
    if (!*p) return n;
    if (n == max) return max + 1;
    words[n++] = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r') p++;
    if (*p) *p++ = 0;
  }
}

static int same(const char* a, const char* b) {
  for (; *a == *b; a++, b++)
    if (!*a) return 1;
  return 0;
}

// A whole number: decimal, or hex after 0x when hex is allowed. Short, so it can't overflow.
static int number(const char* s, int hex, DWORD* out) {
  DWORD v = 0, n = 0, base = 10;
  if (hex && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) base = 16, s += 2;
  else if (hex && s[0] == '0' && s[1]) return 0;
  for (; *s; s++, n++) {
    DWORD d;
    if (*s >= '0' && *s <= '9') d = *s - '0';
    else if (base == 16 && lower(*s) >= 'a' && lower(*s) <= 'f') d = lower(*s) - 'a' + 10;
    else return 0;
    if (n == 8) return 0;
    v = v * base + d;
  }
  if (!n) return 0;
  *out = v;
  return 1;
}

static const char* parse_row(char** w, Row* row) {
  DWORD i;
  for (i = 0; w[0][i]; i++) {
    BYTE c = (BYTE)w[0][i];
    if (i == MAX_NAME || !((c >= '0' && c <= '9') || (lower(c) >= 'a' && lower(c) <= 'z') || c == '_'))
      return "bad name";
    row->name[i] = c;
  }
  row->name[i] = 0;
  if (!number(w[1], 0, &row->family) || row->family > MAX_FAMILY) return "family is not 0..14";
  if (same(w[2], "none")) row->tech = 0;
  else if (same(w[2], "clan")) row->tech = 1;
  else if (same(w[2], "sphere")) row->tech = 2;
  else if (same(w[2], "both")) row->tech = 3;
  else return "tech is not none, clan, sphere or both";
  if (same(w[3], "yes")) row->ammo = 1;
  else if (same(w[3], "no")) row->ammo = 0;
  else return "ammo is not yes or no";
  if (!number(w[4], 1, &row->label) || !row->label || row->label >= 0x10000) return "label is not a message id";
  return NULL;
}

// The rows of the file's text, which it splits in place. Any bad line refuses the whole file: NULL is good.
static const char* parse_rows(char* text, DWORD size, Row* rows, DWORD* count, DWORD* line) {
  char* p = text;
  char* end = text + size;
  DWORD i;
  for (i = 0; i < size; i++) {
    BYTE c = (BYTE)text[i];
    if (c >= 0x7f || (c < 0x20 && c != '\t' && c != '\r' && c != '\n')) return "a character that is not plain text";
  }
  *count = 0;
  *line = 0;
  while (p < end) {
    char* words[5];
    char* stop = p;
    DWORD n;
    const char* bad;
    (*line)++;
    while (stop < end && *stop != '\n') stop++;
    *stop = 0;  // the buffer has a byte past the file for this
    for (i = 0; p + i < stop; i++)
      if (p[i] == '#') {
        p[i] = 0;
        break;
      }
    n = split(p, words, 5);
    p = stop + 1;
    if (!n) continue;
    if (n != 5) return "wants name family tech ammo label";
    if (*count == MAX_ROWS - OLD_ROWS) return "too many rows, 127 weapons in all";
    bad = parse_row(words, &rows[*count]);
    if (bad) return bad;
    if (game_has_name(rows[*count].name)) return "the table already has that name";
    for (i = 0; i < *count; i++)
      if (same_name(rows[i].name, rows[*count].name)) return "that name is listed twice";
    (*count)++;
  }
  return NULL;
}

// ---- Mech3Msg.dll's message table, read from the file with every offset checked ----

static const BYTE* g_msg;
static DWORD g_msg_size;

static DWORD msg16(DWORD at, int* ok) {
  if (at > g_msg_size || g_msg_size - at < 2) return *ok = 0, 0;
  return *(const WORD UNALIGNED*)(g_msg + at);
}

static DWORD msg32(DWORD at, int* ok) {
  if (at > g_msg_size || g_msg_size - at < 4) return *ok = 0, 0;
  return *(const DWORD UNALIGNED*)(g_msg + at);
}

// The offset of a resource directory's entry for an id, or of its first entry when id is ~0; 0 when none.
static DWORD res_entry(DWORD dir, DWORD id, int* ok) {
  DWORD n = msg16(dir + 12, ok) + msg16(dir + 14, ok), i;
  for (i = 0; i < n && *ok; i++) {
    DWORD e = dir + 16 + i * 8;
    if (id == ~0u || msg32(e, ok) == id) return e;
  }
  return 0;
}

// Whether the DLL has each row's message: 1 yes, 0 a row's is missing (its index in *missing), -1 can't read it.
static int check_messages(const Row* rows, DWORD count, DWORD* missing) {
  int ok = 1;
  DWORD pe = msg32(0x3c, &ok), sections = msg16(pe + 6, &ok), table = pe + 24 + msg16(pe + 20, &ok);
  DWORD i, rva = 0, raw = 0, e, leaf, data, blocks, r;
  for (i = 0; i < sections && ok; i++) {
    DWORD s = table + i * 40;
    if (msg32(s, &ok) == 0x7273722e && msg16(s + 4, &ok) == 0x0063) {  // ".rsrc"
      rva = msg32(s + 12, &ok);
      raw = msg32(s + 20, &ok);
      break;
    }
  }
  if (!raw || !ok) return -1;
  e = res_entry(raw, 11, &ok);  // RT_MESSAGETABLE
  if (!e || !ok) return -1;
  e = res_entry(raw + (msg32(e + 4, &ok) & 0x7fffffff), ~0u, &ok);
  if (!e || !ok) return -1;
  e = res_entry(raw + (msg32(e + 4, &ok) & 0x7fffffff), ~0u, &ok);
  if (!e || !ok) return -1;
  leaf = raw + msg32(e + 4, &ok);
  data = msg32(leaf, &ok) - rva + raw;
  blocks = msg32(data, &ok);
  if (!ok || blocks > 0x10000) return -1;
  for (r = 0; r < count; r++) {
    int found = 0;
    for (i = 0; i < blocks && !found; i++) {
      DWORD lo = msg32(data + 4 + i * 12, &ok), hi = msg32(data + 8 + i * 12, &ok);
      found = rows[r].label >= lo && rows[r].label <= hi;
    }
    if (!ok) return -1;
    if (!found) {
      *missing = r;
      return 0;
    }
  }
  return 1;
}

// ---- the patch ----

// Every write, or none: what it can't protect it puts back. Each site's page protection goes back as it was.
static int write_sites(const DWORD* at, const DWORD* value, const BYTE* size, DWORD n) {
  DWORD i, old_value[SITES];
  for (i = 0; i < n; i++) {
    DWORD prot, unused;
    if (!VirtualProtect((void*)(ULONG_PTR)at[i], size[i], PAGE_EXECUTE_READWRITE, &prot)) break;
    if (size[i] == 1) {
      old_value[i] = at8(at[i]);
      *(BYTE*)(ULONG_PTR)at[i] = (BYTE)value[i];
    } else {
      old_value[i] = at32(at[i]);
      *(DWORD UNALIGNED*)(ULONG_PTR)at[i] = value[i];
    }
    if (!VirtualProtect((void*)(ULONG_PTR)at[i], size[i], prot, &unused)) {
      i++;
      break;
    }
  }
  if (i == n) return 1;
  while (i--) {  // a protection call failed: undo, as far as it lets us
    DWORD prot, unused;
    if (!VirtualProtect((void*)(ULONG_PTR)at[i], size[i], PAGE_EXECUTE_READWRITE, &prot)) continue;
    if (size[i] == 1) *(BYTE*)(ULONG_PTR)at[i] = (BYTE)old_value[i];
    else *(DWORD UNALIGNED*)(ULONG_PTR)at[i] = old_value[i];
    VirtualProtect((void*)(ULONG_PTR)at[i], size[i], prot, &unused);
  }
  return 0;
}

// The game's rows, then the new ones, then their names, in pages made read-only once full.
static BYTE* build_table(const Row* rows, DWORD count) {
  DWORD total = OLD_ROWS + count, size = total * ROW + count * (MAX_NAME + 1), i, prot;
  BYTE* table = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!table) return NULL;
  for (i = 0; i < OLD_ROWS * ROW; i++) table[i] = at8(TABLE + i);
  for (i = 0; i < count; i++) {
    DWORD UNALIGNED* row = (DWORD UNALIGNED*)(table + (OLD_ROWS + i) * ROW);
    char* name = (char*)(table + total * ROW + i * (MAX_NAME + 1));
    DWORD c;
    for (c = 0; (name[c] = rows[i].name[c]); c++) {}
    row[0] = (DWORD)(ULONG_PTR)name;
    row[1] = rows[i].label;
    row[2] = rows[i].family;
    row[3] = OLD_ROWS + i;
    row[4] = rows[i].tech;
    row[5] = rows[i].ammo;
  }
  if (!VirtualProtect(table, size, PAGE_READONLY, &prot)) {
    VirtualFree(table, 0, MEM_RELEASE);
    return NULL;
  }
  return table;
}

static void apply(void) {
  char path[MAX_PATH];
  BYTE* text = NULL;
  Row* rows = NULL;
  BYTE* msg = NULL;
  BYTE* table;
  DWORD text_size = 0, msg_size = 0, count = 0, line = 0, missing = 0, i, n = 0, total;
  DWORD at[SITES], value[SITES];
  BYTE size[SITES];
  const char* bad;
  int found;

  if (!known_exe()) {
    log_line("no changes: not an exe it knows (each patch site differs from Mech3.exe 1.2's)", 0, NULL);
    return;
  }
  if (!beside("weapon-rows.txt", path, sizeof path) || !(text = read_file(path, MAX_ROW_FILE, &text_size))) {
    log_line("no changes: no weapon-rows.txt it can read", 0, NULL);
    return;
  }
  rows = VirtualAlloc(NULL, (MAX_ROWS - OLD_ROWS) * sizeof(Row), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!rows) {
    log_line("no changes: out of memory", 0, NULL);
    goto done;
  }
  bad = parse_rows((char*)text, text_size, rows, &count, &line);
  if (bad) {
    log_line("no changes: weapon-rows.txt line", line, bad);
    goto done;
  }
  if (!count) {
    log_line("no changes: weapon-rows.txt has no rows", 0, NULL);
    goto done;
  }
  if (!beside("Mech3Msg.dll", path, sizeof path) || !(msg = read_file(path, MAX_MSG_FILE, &msg_size))) {
    log_line("no changes: can't read Mech3Msg.dll to check the message ids", 0, NULL);
    goto done;
  }
  g_msg = msg;
  g_msg_size = msg_size;
  found = check_messages(rows, count, &missing);
  if (found < 0) {
    log_line("no changes: can't read Mech3Msg.dll's message table", 0, NULL);
    goto done;
  }
  if (!found) {
    log_line("no changes: Mech3Msg.dll has no message for row", missing + 1, rows[missing].name);
    goto done;
  }
  table = build_table(rows, count);
  if (!table) {
    log_line("no changes: can't make the table", 0, NULL);
    goto done;
  }
  total = OLD_ROWS + count;
  for (i = 0; i < COUNT_SITES; i++) at[n] = COUNTS[i].at + 2, value[n] = total, size[n++] = 1;
  for (i = 0; i < REF_SITES; i++)
    at[n] = REFS[i].at, value[n] = (DWORD)(ULONG_PTR)table + REFS[i].offset, size[n++] = 4;
  at[n] = END_REF, value[n] = (DWORD)(ULONG_PTR)table + total * ROW, size[n++] = 4;
  if (!write_sites(at, value, size, n)) {
    VirtualFree(table, 0, MEM_RELEASE);
    log_line("no changes: can't change the exe's page protection", 0, NULL);
    goto done;
  }
  FlushInstructionCache(GetCurrentProcess(), NULL, 0);
  log_line("weapons in the lab:", total, NULL);
done:
  if (text) VirtualFree(text, 0, MEM_RELEASE);
  if (rows) VirtualFree(rows, 0, MEM_RELEASE);
  if (msg) VirtualFree(msg, 0, MEM_RELEASE);
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID reserved) {
  (void)reserved;
  if (reason == DLL_PROCESS_ATTACH) {
    DWORD n = GetModuleFileNameA(self, g_dir, sizeof g_dir);
    if (n && n < sizeof g_dir) {  // else no log path and no row file: nothing happens
      while (n && g_dir[n - 1] != '\\' && g_dir[n - 1] != '/') n--;
      g_dir_len = n;
      g_dir[n] = 0;
      apply();
    }
  }
  return TRUE;
}
