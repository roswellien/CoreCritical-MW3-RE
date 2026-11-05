#include "frontend/sdl/crash_report.h"

#include <SDL3/SDL.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>
#else
#include <limits.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#if defined(__GLIBC__) || defined(__APPLE__)
#include <execinfo.h>
#define MW3_HAVE_BACKTRACE 1
#endif
#endif

namespace sdlfe {

namespace {

char g_program[64] = "mw3";
char g_tail[1024] = "";  // the box's text after the reason
bool g_box = false;
bool g_test = false;

void set_text(const std::string& program, const std::string& log) {
  std::snprintf(g_program, sizeof g_program, "%s", program.c_str());
  std::snprintf(g_tail, sizeof g_tail, ".\n\nThe log is %s\nPlease include it if you report this.", log.c_str());
}

[[noreturn]] void on_terminate() {
  const char* what = "";
  try {
    if (auto e = std::current_exception()) std::rethrow_exception(e);
  } catch (const std::exception& e) {
    what = e.what();
  } catch (...) {
  }
  std::fprintf(stderr, "%s: uncaught exception: %s\n", g_program, what);
  std::abort();  // the crash's report from here
}

#if defined(_WIN32)

void say_crash(const char* reason) {
  std::fflush(stderr);
  Sleep(100);  // the log's thread passes this on (run_log.cpp)
  if (g_test) _exit(70);
  char text[1200];
  std::snprintf(text, sizeof text, "MechWarrior 3 crashed (%s)%s", reason, g_tail);
  // Topmost: with no owner it could open behind a fullscreen game.
  if (g_box) MessageBoxA(nullptr, text, "MechWarrior 3", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
}

LONG WINAPI on_exception(EXCEPTION_POINTERS* e) {
  std::fprintf(stderr, "%s: crashed: exception 0x%08lx at %p\n", g_program, e->ExceptionRecord->ExceptionCode,
               e->ExceptionRecord->ExceptionAddress);
  char reason[32];
  std::snprintf(reason, sizeof reason, "exception 0x%08lx", e->ExceptionRecord->ExceptionCode);
  say_crash(reason);
  return EXCEPTION_EXECUTE_HANDLER;
}

// abort() (an uncaught exception's end too) never reaches the filter above: the
// CRT raises this and exits with 3.
void on_abort(int) {
  std::fprintf(stderr, "%s: crashed: aborted\n", g_program);
  say_crash("aborted");
  _exit(3);
}

#else

char g_exe[PATH_MAX] = "";
char g_stack[64 * 1024];  // a stack overflow's handler needs its own

void put(const char* s) {
  ssize_t ignored = write(2, s, std::strlen(s));
  (void)ignored;
}

void append(char* to, size_t size, const char* s) {
  size_t n = std::strlen(to);
  while (*s && n + 1 < size) to[n++] = *s++;
  to[n] = 0;
}

const char* signal_name(int sig) {
  switch (sig) {
    case SIGSEGV: return "segmentation fault";
    case SIGBUS: return "bus error, its files gone or unreadable";
    case SIGFPE: return "arithmetic error";
    case SIGILL: return "illegal instruction";
    case SIGABRT: return "aborted";
  }
  return "signal";
}

// Only what is safe in a signal handler: write, fork, exec, raise. SA_RESETHAND:
// the raise ends the program by the signal, as without this.
void on_signal(int sig) {
  static char text[1200];
  put(g_program), put(": crashed: "), put(signal_name(sig)), put("\n");
#ifdef MW3_HAVE_BACKTRACE
  void* frames[48];
  backtrace_symbols_fd(frames, backtrace(frames, 48), 2);
#endif
  usleep(100000);  // the log's thread passes this on (run_log.cpp)
  if (g_test) _exit(70);
  if (g_box && g_exe[0] && fork() == 0) {
    setsid();  // on its own: the box stays after the game's window has gone
    text[0] = 0;
    append(text, sizeof text, "MechWarrior 3 crashed (");
    append(text, sizeof text, signal_name(sig));
    append(text, sizeof text, ")");
    append(text, sizeof text, g_tail);
    char flag[] = "--crash-box";
    char* argv[] = {g_exe, flag, text, nullptr};
    execv(g_exe, argv);
    _exit(127);
  }
  raise(sig);
}

void own_path() {
#if defined(__APPLE__)
  uint32_t size = sizeof g_exe;
  if (_NSGetExecutablePath(g_exe, &size) != 0) g_exe[0] = 0;
#else
  const ssize_t n = readlink("/proc/self/exe", g_exe, sizeof g_exe - 1);
  g_exe[n > 0 ? n : 0] = 0;
#endif
}

#endif

}  // namespace

void crash_report_install(const std::string& program, const std::string& log, bool box) {
  set_text(program, log);
  g_box = box && !std::getenv("MW3_LAUNCHER_WATCHES");
  g_test = std::getenv("MW3_CRASH_TEST") != nullptr;
  std::set_terminate(on_terminate);
#if defined(_WIN32)
  SetUnhandledExceptionFilter(on_exception);
  std::signal(SIGABRT, on_abort);
#else
  own_path();
#ifdef MW3_HAVE_BACKTRACE
  void* first[1];
  backtrace(first, 1);  // loads what it needs now, not in the handler
#endif
  stack_t alt{};
  alt.ss_sp = g_stack, alt.ss_size = sizeof g_stack;
  sigaltstack(&alt, nullptr);
  struct sigaction sa {};
  sa.sa_handler = on_signal;
  sa.sa_flags = SA_ONSTACK | SA_RESETHAND;
  sigemptyset(&sa.sa_mask);
  for (int sig : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT}) sigaction(sig, &sa, nullptr);
#endif
  if (g_test) {
    if (std::strcmp(std::getenv("MW3_CRASH_TEST"), "abort") == 0) std::abort();
#if defined(_WIN32)
    volatile int* nothing = nullptr;  // the CRT's raise(SIGSEGV) never reaches the filter
    *nothing = 0;
#else
    std::raise(SIGSEGV);
#endif
  }
}

bool crash_box_main(int argc, char** argv) {
  if (argc != 3 || std::strcmp(argv[1], "--crash-box") != 0) return false;
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MechWarrior 3", argv[2], nullptr);
  return true;
}

void fatal_box(const std::string& message, bool hidden) {
  std::fprintf(stderr, "mw3: %s\n", message.c_str());
  if (!hidden && !SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MechWarrior 3", message.c_str(), nullptr))
    std::fprintf(stderr, "mw3: no message box either: %s\n", SDL_GetError());
}

}  // namespace sdlfe
