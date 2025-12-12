/** @file

    Main file for the traffic_top application.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one
    or more contributor license agreements.  See the NOTICE file
    distributed with this work for additional information
    regarding copyright ownership.  The ASF licenses this file
    to you under the Apache License, Version 2.0 (the
    "License"); you may not use this file except in compliance
    with the License.  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <csignal>

#include "tscore/ink_config.h"

// Prevent ncurses macros from conflicting with C++ stdlib
#define NOMACROS         1
#define NCURSES_NOMACROS 1

#if defined HAVE_NCURSESW_CURSES_H
#include <ncursesw/curses.h>
#elif defined HAVE_NCURSESW_H
#include <ncursesw.h>
#elif defined HAVE_NCURSES_CURSES_H
#include <ncurses/curses.h>
#elif defined HAVE_NCURSES_H
#include <ncurses.h>
#elif defined HAVE_CURSES_H
#include <curses.h>
#else
#error "SysV or X/Open-compatible Curses header file required"
#endif

#include "tscore/Layout.h"
#include "tscore/ink_args.h"
#include "tscore/Version.h"
#include "tscore/runroot.h"

#include "Stats.h"
#include "Display.h"
#include "Output.h"

using namespace traffic_top;

namespace
{
// Timeout constants (in milliseconds)
constexpr int FIRST_DISPLAY_TIMEOUT_MS = 1000; // Initial display timeout for responsiveness
constexpr int CONNECT_RETRY_TIMEOUT_MS = 500;  // Timeout between connection retry attempts
constexpr int MAX_CONNECTION_RETRIES   = 10;   // Max retries before falling back to normal timeout
constexpr int MS_PER_SECOND            = 1000; // Milliseconds per second for timeout conversion

// Command-line options
int  g_sleep_time  = 5;   // Seconds between updates
int  g_count       = 0;   // Number of iterations (0 = infinite)
int  g_batch_mode  = 0;   // Batch mode flag
int  g_ascii_mode  = 0;   // ASCII mode flag (no Unicode)
int  g_json_format = 0;   // JSON output format
char g_output_file[1024]; // Output file path

// Signal handling for clean shutdown and window resize
volatile sig_atomic_t g_shutdown       = 0;
volatile sig_atomic_t g_window_resized = 0;

void
signal_handler(int)
{
  g_shutdown = 1;
}

void
resize_handler(int)
{
  g_window_resized = 1;
}

void
setup_signals()
{
  struct sigaction sa;
  sa.sa_handler = signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);

  // Handle window resize
  struct sigaction sa_resize;
  sa_resize.sa_handler = resize_handler;
  sigemptyset(&sa_resize.sa_mask);
  sa_resize.sa_flags = SA_RESTART;
  sigaction(SIGWINCH, &sa_resize, nullptr);
}

/**
 * Run in interactive curses mode.
 */
int
run_interactive(Stats &stats, int sleep_time, bool ascii_mode)
{
  Display display;
  display.setAsciiMode(ascii_mode);

  if (!display.initialize()) {
    fprintf(stderr, "Failed to initialize display\n");
    return 1;
  }

  Page current_page      = Page::Main;
  bool connected         = false;
  int  anim_frame        = 0;
  bool first_display     = true;
  int  connect_retry     = 0;
  bool user_toggled_mode = false; // Track if user manually changed mode
  bool running           = true;  // Main loop control flag

  // Try initial connection - start with absolute values
  if (stats.getStats()) {
    connected = true;
  }

  while (running && !g_shutdown) {
    // Handle window resize
    if (g_window_resized) {
      g_window_resized = 0;
      // Notify ncurses about the resize
      endwin();
      refresh();
    }

    // Auto-switch from absolute to rate mode once we can calculate rates
    // (unless user has manually toggled the mode)
    if (!user_toggled_mode && stats.isAbsolute() && stats.canCalculateRates()) {
      stats.setAbsolute(false);
    }

    // Render current page
    display.render(stats, current_page, stats.isAbsolute());

    // Draw status bar
    std::string host_display = stats.getHost();
    if (!connected) {
      const char *anim = "|/-\\";
      host_display     = std::string("connecting ") + anim[anim_frame % 4];
      ++anim_frame;
    }
    display.drawStatusBar(host_display, current_page, stats.isAbsolute(), connected);
    fflush(stdout);

    // Use short timeout when first starting or still connecting
    // This allows quick display updates and responsive connection retry
    int current_timeout;
    if (first_display && connected) {
      // First successful display - short timeout for responsiveness
      current_timeout = FIRST_DISPLAY_TIMEOUT_MS;
      first_display   = false;
    } else if (!connected && connect_retry < MAX_CONNECTION_RETRIES) {
      // Still trying to connect - retry quickly
      current_timeout = CONNECT_RETRY_TIMEOUT_MS;
      ++connect_retry;
    } else {
      // Normal operation - use configured sleep time
      current_timeout = sleep_time * MS_PER_SECOND;
    }
    timeout(current_timeout);

    int ch = getch();

    // Handle input
    switch (ch) {
    case 'q':
    case 'Q':
      running = false;
      break;

    case 'h':
    case 'H':
    case '?':
      current_page = Page::Help;
      break;

    case '1':
      current_page = Page::Main;
      break;
    case '2':
      current_page = Page::Response;
      break;
    case '3':
      current_page = Page::Connection;
      break;
    case '4':
      current_page = Page::Cache;
      break;
    case '5':
      current_page = Page::SSL;
      break;
    case '6':
      current_page = Page::Errors;
      break;
    case '7':
    case 'p':
    case 'P':
      current_page = Page::Performance;
      break;
    case '8':
    case 'g':
    case 'G':
      current_page = Page::Graphs;
      break;

    case 'a':
    case 'A':
      stats.toggleAbsolute();
      user_toggled_mode = true; // User manually changed mode, don't auto-switch
      break;

    case KEY_LEFT:
    case 'm':
    case 'M':
      if (current_page != Page::Help) {
        int p = static_cast<int>(current_page);
        if (p > 0) {
          current_page = static_cast<Page>(p - 1);
        } else {
          current_page = static_cast<Page>(Display::getPageCount() - 1);
        }
      }
      break;

    case KEY_RIGHT:
    case 'r':
    case 'R':
      if (current_page != Page::Help) {
        int p = static_cast<int>(current_page);
        if (p < Display::getPageCount() - 1) {
          current_page = static_cast<Page>(p + 1);
        } else {
          current_page = Page::Main;
        }
      }
      break;

    case 'b':
    case 'B':
    case KEY_BACKSPACE:
    case 27: // ESC
      if (current_page == Page::Help) {
        current_page = Page::Main;
      }
      break;

    default:
      // Any key exits help
      if (current_page == Page::Help && ch != ERR) {
        current_page = Page::Main;
      }
      break;
    }

    // Refresh stats
    bool was_connected = connected;
    connected          = stats.getStats();

    // Reset retry counter when we successfully connect
    if (connected && !was_connected) {
      connect_retry = 0;
    }
  }

  display.shutdown();
  return 0;
}

/**
 * Run in batch mode (non-interactive).
 */
int
run_batch(Stats &stats, int sleep_time, int count, OutputFormat format, const char *output_path)
{
  FILE *output = stdout;

  if (output_path[0] != '\0') {
    output = fopen(output_path, "w");
    if (!output) {
      fprintf(stderr, "Error: Cannot open output file '%s': %s\n", output_path, strerror(errno));
      return 1;
    }
  }

  Output out(format, output);

  // Default count to 1 if not specified in batch mode
  if (count == 0) {
    count = 1;
  }

  int iterations = 0;
  while (!g_shutdown && (count < 0 || iterations < count)) {
    if (!stats.getStats()) {
      out.printError(stats.getLastError());
      if (output != stdout) {
        fclose(output);
      }
      return 1;
    }

    out.printStats(stats);
    ++iterations;

    if (count < 0 || iterations < count) {
      sleep(sleep_time);
    }
  }

  if (output != stdout) {
    fclose(output);
  }

  return 0;
}

} // anonymous namespace

int
main([[maybe_unused]] int argc, const char **argv)
{
  static const char USAGE[] = "Usage: traffic_top [options]\n"
                              "\n"
                              "Interactive mode (default):\n"
                              "  Display real-time ATS statistics in a curses interface.\n"
                              "  Use number keys (1-8) to switch pages, 'p' for performance, 'g' for graphs, 'q' to quit.\n"
                              "\n"
                              "Batch mode (-b):\n"
                              "  Output statistics to stdout/file for scripting.\n";

  g_output_file[0] = '\0';

  auto &version = AppVersionInfo::setup_version("traffic_top");

  const ArgumentDescription argument_descriptions[] = {
    {"sleep",  's', "Seconds between updates (default: 5)",                                "I",     &g_sleep_time,  nullptr, nullptr},
    {"count",  'c', "Number of iterations (default: 1 in batch, infinite in interactive)", "I",     &g_count,       nullptr, nullptr},
    {"batch",  'b', "Batch mode (non-interactive output)",                                 "F",     &g_batch_mode,  nullptr, nullptr},
    {"output", 'o', "Output file for batch mode (default: stdout)",                        "S1023", g_output_file,  nullptr, nullptr},
    {"json",   'j', "Output in JSON format (batch mode)",                                  "F",     &g_json_format, nullptr, nullptr},
    {"ascii",  'a', "Use ASCII characters instead of Unicode",                             "F",     &g_ascii_mode,  nullptr, nullptr},
    HELP_ARGUMENT_DESCRIPTION(),
    VERSION_ARGUMENT_DESCRIPTION(),
    RUNROOT_ARGUMENT_DESCRIPTION(),
  };

  process_args(&version, argument_descriptions, countof(argument_descriptions), argv, USAGE);

  runroot_handler(argv);
  Layout::create();

  // Validate arguments
  if (g_sleep_time < 1) {
    fprintf(stderr, "Error: Sleep time must be at least 1 second\n");
    return 1;
  }

  setup_signals();

  Stats stats;

  int result;
  if (g_batch_mode) {
    OutputFormat format = g_json_format ? OutputFormat::Json : OutputFormat::Text;
    result              = run_batch(stats, g_sleep_time, g_count, format, g_output_file);
  } else {
    result = run_interactive(stats, g_sleep_time, g_ascii_mode != 0);
  }

  return result;
}
