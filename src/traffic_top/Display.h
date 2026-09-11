/** @file

    Display class for traffic_top using direct ANSI terminal output.

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
#pragma once

#include <string>
#include <tuple>
#include <vector>
#include <termios.h>

#include "Stats.h"
#include "StatType.h"

namespace traffic_top
{

/// Color indices used for selecting colors
namespace ColorPair
{
  constexpr short Red     = 1;
  constexpr short Yellow  = 2;
  constexpr short Green   = 3;
  constexpr short Blue    = 4;
  constexpr short Grey    = 5;
  constexpr short Cyan    = 6;
  constexpr short Border  = 7; // Primary border color (cyan)
  constexpr short Border2 = 8; // Secondary border color (blue)
  constexpr short Border3 = 9; // Tertiary border color (magenta)
  constexpr short Dim     = 10;
  constexpr short Magenta = 11;
  // Bright border colors
  constexpr short Border4 = 12; // Bright blue
  constexpr short Border5 = 13; // Bright yellow
  constexpr short Border6 = 14; // Bright red
  constexpr short Border7 = 15; // Bright green
} // namespace ColorPair

/// Unicode box-drawing characters with rounded corners
namespace BoxChars
{
  constexpr const char *TopLeft     = "╭";
  constexpr const char *TopRight    = "╮";
  constexpr const char *BottomLeft  = "╰";
  constexpr const char *BottomRight = "╯";
  constexpr const char *Horizontal  = "─";
  constexpr const char *Vertical    = "│";

  // ASCII fallback
  constexpr const char *AsciiTopLeft     = "+";
  constexpr const char *AsciiTopRight    = "+";
  constexpr const char *AsciiBottomLeft  = "+";
  constexpr const char *AsciiBottomRight = "+";
  constexpr const char *AsciiHorizontal  = "-";
  constexpr const char *AsciiVertical    = "|";
} // namespace BoxChars

/// Unicode block characters for graphs (8 height levels)
namespace GraphChars
{
  // Block characters from empty to full (index 0-8)
  constexpr const char *Blocks[] = {
    " ", // 0 - empty
    "▁", // 1 - lower 1/8
    "▂", // 2 - lower 2/8
    "▃", // 3 - lower 3/8
    "▄", // 4 - lower 4/8
    "▅", // 5 - lower 5/8
    "▆", // 6 - lower 6/8
    "▇", // 7 - lower 7/8
    "█"  // 8 - full block
  };

  // ASCII fallback characters
  constexpr const char AsciiBlocks[] = {' ', '_', '.', '-', '=', '+', '#', '#', '#'};

  constexpr int NumLevels = 9;
} // namespace GraphChars

/// Available display pages
enum class Page {
  Main        = 0,
  Response    = 1,
  Connection  = 2,
  Cache       = 3,
  SSL         = 4,
  Errors      = 5,
  Performance = 6,
  Graphs      = 7,
  Help        = 8,
  PageCount   = 9
};

/**
 * Display manager for traffic_top curses interface.
 */
class Display
{
public:
  Display();
  ~Display();

  // Non-copyable, non-movable
  Display(const Display &)            = delete;
  Display &operator=(const Display &) = delete;
  Display(Display &&)                 = delete;
  Display &operator=(Display &&)      = delete;

  /**
   * Initialize curses and colors.
   * @return true on success
   */
  bool initialize();

  /**
   * Clean up terminal.
   */
  void shutdown();

  /**
   * Get keyboard input with timeout.
   * @param timeout_ms Timeout in milliseconds (0 = non-blocking, -1 = blocking)
   * @return Character code, or -1 if no input within timeout.
   *         Special keys: KEY_LEFT=0x104, KEY_RIGHT=0x105, KEY_UP=0x103, KEY_DOWN=0x102
   */
  int getInput(int timeout_ms);

  /// Special key codes (compatible with ncurses KEY_* values)
  static constexpr int KEY_NONE  = -1;
  static constexpr int KEY_UP    = 0x103;
  static constexpr int KEY_DOWN  = 0x102;
  static constexpr int KEY_LEFT  = 0x104;
  static constexpr int KEY_RIGHT = 0x105;

  /**
   * Set whether to use ASCII box characters instead of Unicode.
   */
  void
  setAsciiMode(bool ascii)
  {
    _ascii_mode = ascii;
  }

  /**
   * Render the current page.
   */
  void render(Stats &stats, Page page, bool absolute);

  /**
   * Get terminal dimensions.
   */
  void getTerminalSize(int &width, int &height) const;

  /**
   * Draw a box around a region with rounded corners.
   * @param x Starting column
   * @param y Starting row
   * @param width Box width
   * @param height Box height
   * @param title Title to display in top border
   * @param colorIdx Color pair index for the border (use ColorPair::Border, Border2, Border3)
   */
  void drawBox(int x, int y, int width, int height, const std::string &title = "", short colorIdx = ColorPair::Border);

  /**
   * Draw a stat table.
   * @param x Starting column
   * @param y Starting row
   * @param items List of stat keys to display
   * @param stats Stats object to fetch values from
   * @param labelWidth Width for the label column
   */
  void drawStatTable(int x, int y, const std::vector<std::string> &items, Stats &stats, int labelWidth = 14);

  /**
   * Draw stats in a grid layout with multiple columns per row.
   * @param x Starting column
   * @param y Starting row
   * @param boxWidth Width of the containing box
   * @param items List of stat keys to display
   * @param stats Stats object to fetch values from
   * @param cols Number of columns
   */
  void drawStatGrid(int x, int y, int boxWidth, const std::vector<std::string> &items, Stats &stats, int cols = 3);

  /**
   * Format and print a stat value with appropriate color.
   */
  void printStatValue(int x, int y, double value, StatType type);

  /**
   * Draw a mini progress bar for percentage values.
   * @param x Starting column
   * @param y Row
   * @param percent Value 0-100
   * @param width Bar width in characters
   */
  void drawProgressBar(int x, int y, double percent, int width = 8);

  /**
   * Draw a graph line using block characters.
   * @param x Starting column
   * @param y Row
   * @param data Vector of values (0.0-1.0 normalized)
   * @param width Width of graph in characters
   * @param colored Whether to use color gradient
   */
  void drawGraphLine(int x, int y, const std::vector<double> &data, int width, bool colored = true);

  /**
   * Draw a multi-graph box with label, graph, and value on each row.
   * Format: | LABEL  ▂▁▁▂▃▄▅▆▇  VALUE |
   * @param x Starting column
   * @param y Starting row
   * @param width Box width
   * @param graphs Vector of (label, data, value) tuples
   * @param title Optional title for the box header
   */
  void drawMultiGraphBox(int x, int y, int width,
                         const std::vector<std::tuple<std::string, std::vector<double>, std::string>> &graphs,
                         const std::string                                                            &title = "");

  /**
   * Draw the status bar at the bottom of the screen.
   */
  void drawStatusBar(const std::string &host, Page page, bool absolute, bool connected);

  /**
   * Get page name for display.
   */
  static const char *getPageName(Page page);

  /**
   * Get total number of pages.
   */
  static int
  getPageCount()
  {
    return static_cast<int>(Page::PageCount) - 1;
  } // Exclude Help

private:
  // -------------------------------------------------------------------------
  // Page rendering functions
  // -------------------------------------------------------------------------
  // Each page has a dedicated render function that draws the appropriate
  // stats and layout for that category.

  void renderMainPage(Stats &stats);        ///< Overview page with cache, connections, requests
  void renderResponsePage(Stats &stats);    ///< HTTP response code breakdown (2xx, 4xx, 5xx, etc.)
  void renderConnectionPage(Stats &stats);  ///< HTTP/1.x vs HTTP/2 connection details
  void renderCachePage(Stats &stats);       ///< Cache storage, operations, hit rates
  void renderSSLPage(Stats &stats);         ///< SSL/TLS handshake and session statistics
  void renderErrorsPage(Stats &stats);      ///< Connection errors, HTTP errors, cache errors
  void renderPerformancePage(Stats &stats); ///< HTTP milestone timing (request lifecycle)
  void renderGraphsPage(Stats &stats);      ///< Real-time graphs
  void renderHelpPage(const std::string &host, const std::string &version);

  // -------------------------------------------------------------------------
  // Responsive layout functions for the main overview page
  // -------------------------------------------------------------------------
  // The main page adapts its layout based on terminal width:
  // - 80 columns:  2 boxes per row, 2 rows (minimal layout)
  // - 120 columns: 3 boxes per row, more stats visible
  // - 160+ columns: 4 boxes per row, full stat coverage
  // See LAYOUT.md for detailed layout specifications.

  void render80Layout(Stats &stats);  ///< Layout for 80-column terminals
  void render120Layout(Stats &stats); ///< Layout for 120-column terminals
  void render160Layout(Stats &stats); ///< Layout for 160+ column terminals

  /**
   * Draw a row of stat pairs inside a 40-char box.
   * This is the core layout primitive for the main page boxes.
   *
   * Format: | Label1   Value1   Label2   Value2 |
   *         ^-- border                   border--^
   *
   * @param x Box starting column (where the left border is)
   * @param y Row number
   * @param key1 First stat key (from lookup table)
   * @param key2 Second stat key (from lookup table)
   * @param stats Stats object to fetch values from
   * @param borderColor Color for the vertical borders
   */
  void drawStatPairRow(int x, int y, const std::string &key1, const std::string &key2, Stats &stats,
                       short borderColor = ColorPair::Border);

  /**
   * Draw a section header line spanning between two x positions.
   */
  void drawSectionHeader(int y, int x1, int x2, const std::string &title);

  /**
   * Helper to select Unicode or ASCII box-drawing character.
   * @param unicode The Unicode character to use normally
   * @param ascii The ASCII fallback character
   * @return The appropriate character based on _ascii_mode
   */
  const char *
  boxChar(const char *unicode, const char *ascii) const
  {
    return _ascii_mode ? ascii : unicode;
  }

  /**
   * Detect UTF-8 support from environment variables (LANG, LC_ALL, etc.).
   * Used to auto-detect whether to use Unicode or ASCII box characters.
   * @return true if UTF-8 appears to be supported
   */
  static bool detectUtf8Support();

  // -------------------------------------------------------------------------
  // State variables
  // -------------------------------------------------------------------------
  bool           _initialized = false;   ///< True after successful initialize() call
  bool           _ascii_mode  = false;   ///< True = use ASCII box chars, False = use Unicode
  int            _width       = 80;      ///< Current terminal width in columns
  int            _height      = 24;      ///< Current terminal height in rows
  struct termios _orig_termios;          ///< Original terminal settings (restored on shutdown)
  bool           _termios_saved = false; ///< True if _orig_termios has valid saved state
};

} // namespace traffic_top
