#pragma once
// Ours, not original code (docs/program.md, The log): a crash says why, in the
// log and in a message box, rather than the window just going away.
#include <string>

namespace sdlfe {

// The handlers for a crash and for an uncaught exception. `log` is named in
// the box; with `box` false only the log says it (hidden runs do not install
// this at all; MW3_LAUNCHER_WATCHES: the launcher shows the box, see
// Launcher::watch_game). MW3_CRASH_TEST=1 ends with exit code 70 after the
// log line, for the test.
void crash_report_install(const std::string& program, const std::string& log, bool box);

// The start of main: `--crash-box <text>` shows the box and is all this run
// does. A crash's handler cannot use SDL, so it starts its own program for this.
bool crash_box_main(int argc, char** argv);

// A start that cannot go on: the log line and, unless hidden, the box.
void fatal_box(const std::string& message, bool hidden);

}  // namespace sdlfe
