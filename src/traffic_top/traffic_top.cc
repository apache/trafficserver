/** @file

    Main file for the traffic_top application.

    traffic_top is a real-time monitoring tool for Apache Traffic Server (ATS).
    It displays statistics in a curses-based terminal UI, similar to htop/btop++.

    Features:
    - Real-time display of cache hits, requests, connections, bandwidth
    - Multiple pages for different stat categories (responses, cache, SSL, etc.)
    - Graph visualization of key metrics over time
    - Batch mode for scripting with JSON/text output
    - Responsive layout adapting to terminal size (80, 120, 160+ columns)

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

// -------------------------------------------------------------------------
// Signal handling
// -------------------------------------------------------------------------
// We use sig_atomic_t for thread-safe signal flags that can be safely
// accessed from both signal handlers and the main loop.
//
// g_shutdown:       Set by SIGINT/SIGTERM to trigger clean exit
// g_window_resized: Set by SIGWINCH to trigger terminal size refresh
// -------------------------------------------------------------------------
volatile sig_atomic_t g_shutdown       = 0;
volatile sig_atomic_t g_window_resized = 0;

/**
 * Signal handler for SIGINT (Ctrl+C) and SIGTERM.
 * Sets the shutdown flag to trigger a clean exit from the main loop.
 */
void
signal_handler(int)
{
  g_shutdown = 1;
}

/**
 * Signal handler for SIGWINCH (window resize).
 * Sets a flag that the main loop checks to refresh terminal dimensions.
 */
void
resize_handler(int)
{
  g_window_resized = 1;
}

/**
 * Register signal handlers for clean shutdown and window resize.
 *
 * SIGINT/SIGTERM: Trigger clean shutdown (restore terminal, exit gracefully)
 * SIGWINCH: Trigger terminal size refresh for responsive layout
 */
void
setup_signals()
{
  // Handler for clean shutdown on Ctrl+C or kill
  struct sigaction sa;
  sa.sa_handler = signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);

  // Handler for terminal window resize
  // SA_RESTART ensures system calls aren't interrupted by this signal
  struct sigaction sa_resize;
  sa_resize.sa_handler = resize_handler;
  sigemptyset(&sa_resize.sa_mask);
  sa_resize.sa_flags = SA_RESTART;
  sigaction(SIGWINCH, &sa_resize, nullptr);
}

/**
 * Run in interactive curses mode.
 *
 * This is the main event loop for the interactive TUI. It:
 * 1. Initializes the display with ncurses for input handling
 * 2. Fetches stats from ATS via RPC on each iteration
 * 3. Renders the current page based on terminal size
 * 4. Handles keyboard input for navigation and mode switching
 *
 * The loop uses a timeout-based approach:
 * - Quick timeout (500ms) during initial connection attempts
 * - Normal timeout (sleep_time) once connected
 *
 * Display modes:
 * - Absolute: Shows raw counter values (useful at startup before rates can be calculated)
 * - Rate: Shows per-second rates (automatically enabled once we have two data points)
 *
 * @param stats Reference to the Stats object for fetching ATS metrics
 * @param sleep_time Seconds between stat refreshes (user-configurable)
 * @param ascii_mode If true, use ASCII characters instead of Unicode box-drawing
 * @return 0 on success, 1 on error
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

  // State variables for the main loop
  Page current_page      = Page::Main; // Currently displayed page
  bool connected         = false;      // Whether we have a successful RPC connection
  int  anim_frame        = 0;          // Animation frame for "connecting" spinner
  bool first_display     = true;       // True until first successful render
  int  connect_retry     = 0;          // Number of connection retry attempts
  bool user_toggled_mode = false;      // True if user manually pressed 'a' to toggle mode
  bool running           = true;       // Main loop control flag (false = exit)

  // Try initial connection - start with absolute values since we can't calculate rates yet
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

    // getch() blocks for up to current_timeout milliseconds, then returns ERR
    // This allows the UI to update even if no key is pressed
    int ch = getch();

    // -------------------------------------------------------------------------
    // Keyboard input handling
    // -------------------------------------------------------------------------
    // Navigation keys:
    //   1-8        - Jump directly to page N
    //   Left/m     - Previous page (wraps around)
    //   Right/r    - Next page (wraps around)
    //   h/?        - Show help page
    //   b/ESC      - Return from help to main
    //
    // Mode keys:
    //   a          - Toggle absolute/rate display mode
    //   q          - Quit the application
    // -------------------------------------------------------------------------
    switch (ch) {
    // Quit application
    case 'q':
    case 'Q':
      running = false;
      break;

    // Show help page
    case 'h':
    case 'H':
    case '?':
      current_page = Page::Help;
      break;

    // Direct page navigation (1-8)
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

    // Toggle between absolute values and per-second rates
    case 'a':
    case 'A':
      stats.toggleAbsolute();
      user_toggled_mode = true; // Disable auto-switch once user takes control
      break;

    // Navigate to previous page (with wraparound)
    case KEY_LEFT:
    case 'm':
    case 'M':
      if (current_page != Page::Help) {
        int p = static_cast<int>(current_page);
        if (p > 0) {
          current_page = static_cast<Page>(p - 1);
        } else {
          // Wrap to last page
          current_page = static_cast<Page>(Display::getPageCount() - 1);
        }
      }
      break;

    // Navigate to next page (with wraparound)
    case KEY_RIGHT:
    case 'r':
    case 'R':
      if (current_page != Page::Help) {
        int p = static_cast<int>(current_page);
        if (p < Display::getPageCount() - 1) {
          current_page = static_cast<Page>(p + 1);
        } else {
          // Wrap to first page
          current_page = Page::Main;
        }
      }
      break;

    // Return from help page
    case 'b':
    case 'B':
    case KEY_BACKSPACE:
    case 27: // ESC key
      if (current_page == Page::Help) {
        current_page = Page::Main;
      }
      break;

    default:
      // Any other key exits help page (convenience feature)
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
 *
 * Batch mode outputs statistics in a machine-readable format (JSON or text)
 * suitable for scripting, logging, or piping to other tools. Unlike interactive
 * mode, it doesn't use curses and writes directly to stdout or a file.
 *
 * Output formats:
 * - Text: Tab-separated values with column headers (vmstat-style)
 * - JSON: One JSON object per line with timestamp, host, and stat values
 *
 * @param stats Reference to the Stats object for fetching ATS metrics
 * @param sleep_time Seconds to wait between iterations
 * @param count Number of iterations (-1 for infinite, 0 defaults to 1)
 * @param format Output format (Text or JSON)
 * @param output_path File path to write output (empty string = stdout)
 * @return 0 on success, 1 on error
 */
int
run_batch(Stats &stats, int sleep_time, int count, OutputFormat format, const char *output_path)
{
  // Open output file if specified, otherwise use stdout
  FILE *output = stdout;

  if (output_path[0] != '\0') {
    output = fopen(output_path, "w");
    if (!output) {
      fprintf(stderr, "Error: Cannot open output file '%s': %s\n", output_path, strerror(errno));
      return 1;
    }
  }

  Output out(format, output);

  // In batch mode, default to single iteration if count not specified
  // This makes `traffic_top -b` useful for one-shot queries
  if (count == 0) {
    count = 1;
  }

  // Main batch loop - runs until count reached or signal received
  int iterations = 0;
  while (!g_shutdown && (count < 0 || iterations < count)) {
    // Fetch stats from ATS via RPC
    if (!stats.getStats()) {
      out.printError(stats.getLastError());
      if (output != stdout) {
        fclose(output);
      }
      return 1;
    }

    // Output the stats in the requested format
    out.printStats(stats);
    ++iterations;

    // Sleep between iterations (but not after the last one)
    if (count < 0 || iterations < count) {
      sleep(sleep_time);
    }
  }

  // Clean up output file if we opened one
  if (output != stdout) {
    fclose(output);
  }

  return 0;
}

} // anonymous namespace

/**
 * Main entry point for traffic_top.
 *
 * Parses command-line arguments and launches either:
 * - Interactive mode: curses-based TUI with real-time stats display
 * - Batch mode: machine-readable output (JSON or text) for scripting
 *
 * Example usage:
 *   traffic_top                    # Interactive mode with default settings
 *   traffic_top -s 1               # Update every 1 second
 *   traffic_top -b -j              # Single JSON output to stdout
 *   traffic_top -b -c 10 -o out.txt # 10 text outputs to file
 *   traffic_top -a                 # Use ASCII instead of Unicode
 */
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

  // Initialize output file path to empty string
  g_output_file[0] = '\0';

  // Setup version info for --version output
  auto &version = AppVersionInfo::setup_version("traffic_top");

  // Define command-line arguments
  // Format: {name, short_opt, description, type, variable, default, callback}
  // Types: "I" = int, "F" = flag (bool), "S1023" = string up to 1023 chars
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

  // Parse command-line arguments (exits on --help or --version)
  process_args(&version, argument_descriptions, countof(argument_descriptions), argv, USAGE);

  // Initialize ATS runroot and layout for finding RPC socket
  runroot_handler(argv);
  Layout::create();

  // Validate arguments
  if (g_sleep_time < 1) {
    fprintf(stderr, "Error: Sleep time must be at least 1 second\n");
    return 1;
  }

  // Setup signal handlers for clean shutdown and window resize
  setup_signals();

  // Create the stats collector (initializes lookup table and validates config)
  Stats stats;

  // Run in the appropriate mode
  int result;
  if (g_batch_mode) {
    // Batch mode: output to stdout/file for scripting
    OutputFormat format = g_json_format ? OutputFormat::Json : OutputFormat::Text;
    result              = run_batch(stats, g_sleep_time, g_count, format, g_output_file);
  } else {
    // Interactive mode: curses-based TUI
    result = run_interactive(stats, g_sleep_time, g_ascii_mode != 0);
  }

  return result;
}
