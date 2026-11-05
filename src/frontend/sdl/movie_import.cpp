#include "frontend/sdl/movie_import.h"

#include <SDL3/SDL.h>

#include <cctype>
#include <cstdio>
#include <filesystem>

#include "frontend/sdl/sdl_child.h"
#include "gamez/zutil/zutl_path.h"

namespace sdlfe {
namespace fs = std::filesystem;

namespace {
std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
}  // namespace

// In a Mac app SDL's base is Contents/Resources, and the importer is beside
// mw3 in Contents/MacOS.
std::string importer_path() {
  if (const char* stand_in = SDL_getenv("MW3_IMPORTER")) return stand_in;  // checks: a stand-in
  const char* base = SDL_GetBasePath();
  if (!base) return {};
#if defined(_WIN32)
  const fs::path p = fs::path(base) / "mw3import.exe";
#else
  const fs::path p = fs::path(base) / "mw3import";
#endif
  std::error_code ec;
  if (fs::exists(p, ec)) return p.string();
#if defined(__APPLE__)
  const fs::path res = fs::path(base).parent_path();  // the base ends in '/'
  if (res.filename() == "Resources") {
    const fs::path q = res.parent_path() / "MacOS" / "mw3import";
    if (fs::exists(q, ec)) return q.string();
  }
#endif
  return {};
}

int movies_missing(const std::string& source, const std::string& write_dir) {
  std::error_code ec;
  int n = 0;
  for (const auto& e : fs::directory_iterator(zutl::any_case(source, "video"), ec)) {
    if (lower(e.path().extension().string()) != ".avi") continue;
    if (!fs::exists(fs::path(write_dir) / "video" / (lower(e.path().stem().string()) + ".fmv"), ec)) ++n;
  }
  return n;
}

int run_importer(const std::string& source, const std::string& write_dir,
                 const std::function<bool(int, int, const std::string&)>& progress) {
  const std::string importer = importer_path();
  if (importer.empty()) return -1;
  const char* args[] = {importer.c_str(), "--install", source.c_str(), "--write-dir", write_dir.c_str(), "--missing", nullptr};
  SDL_Process* proc = start_child(args);
  if (!proc) return -1;
  SDL_IOStream* out = static_cast<SDL_IOStream*>(
      SDL_GetPointerProperty(SDL_GetProcessProperties(proc), SDL_PROP_PROCESS_STDOUT_POINTER, nullptr));
  bool stopped = false;
  std::string line;
  char buf[512];
  size_t n;
  while (out && (n = SDL_ReadIO(out, buf, sizeof buf)) > 0) {
    for (size_t i = 0; i < n; ++i) {
      if (buf[i] != '\n') {
        line += buf[i];
        continue;
      }
      int k = 0, t = 0, used = 0;
      if (std::sscanf(line.c_str(), "progress %d %d %n", &k, &t, &used) >= 2) {
        if (!stopped && progress && !progress(k, t, used > 0 ? line.substr(static_cast<size_t>(used)) : std::string())) {
          SDL_KillProcess(proc, false);
          stopped = true;
        }
      } else if (!line.empty()) {
        std::printf("%s\n", line.c_str());  // the importer's own lines, passed on
      }
      line.clear();
    }
  }
  int code = -1;
  SDL_WaitProcess(proc, true, &code);
  SDL_DestroyProcess(proc);
  if (stopped) return 1;
  return movies_missing(source, write_dir) == 0 ? 0 : -1;  // judged by the files (a Windows child's code reads 0)
}

}  // namespace sdlfe
