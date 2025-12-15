/** @file

    Display class implementation for traffic_top using direct ANSI output.

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

#include "Display.h"

#include <algorithm>
#include <clocale>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>

namespace traffic_top
{

// ANSI escape sequences
namespace
{
  // Move cursor to row, col (1-based for ANSI)
  void
  moveTo(int row, int col)
  {
    printf("\033[%d;%dH", row + 1, col + 1);
  }

  // Set foreground color
  void
  setColor(short colorIdx)
  {
    switch (colorIdx) {
    case ColorPair::Red:
      printf("\033[31m");
      break;
    case ColorPair::Green:
      printf("\033[32m");
      break;
    case ColorPair::Yellow:
      printf("\033[33m");
      break;
    case ColorPair::Blue:
      printf("\033[34m");
      break;
    case ColorPair::Magenta:
    case ColorPair::Border3:
      printf("\033[35m");
      break;
    case ColorPair::Cyan:
    case ColorPair::Border:
      printf("\033[36m");
      break;
    case ColorPair::Grey:
    case ColorPair::Dim:
      printf("\033[90m");
      break;
    case ColorPair::Border2:
      printf("\033[34m");
      break;
    case ColorPair::Border4: // Bright blue
      printf("\033[94m");
      break;
    case ColorPair::Border5: // Bright yellow
      printf("\033[93m");
      break;
    case ColorPair::Border6: // Bright red
      printf("\033[91m");
      break;
    case ColorPair::Border7: // Bright green
      printf("\033[92m");
      break;
    default:
      printf("\033[0m");
      break;
    }
  }

  void
  resetColor()
  {
    printf("\033[0m");
  }

  void
  setBold()
  {
    printf("\033[1m");
  }

  void
  clearScreen()
  {
    printf("\033[2J\033[H");
  }

  void
  hideCursor()
  {
    printf("\033[?25l");
  }

  void
  showCursor()
  {
    printf("\033[?25h");
  }

} // anonymous namespace

// Layout breakpoints for common terminal sizes:
//   80x24  - Classic VT100/xterm default (2 columns)
//   120x40 - Common larger terminal (3 columns)
//   160x50 - Wide terminal (4 columns)
//   300x75 - Extra large/tiled display (4 columns, wider boxes)
constexpr int WIDTH_MEDIUM = 120; // Larger terminal (minimum for 3-column layout)
constexpr int WIDTH_LARGE  = 160; // Wide terminal (minimum for 4-column layout)

constexpr int LABEL_WIDTH_SM = 12; // Small label width (80-col terminals)
constexpr int LABEL_WIDTH_MD = 14; // Medium label width (120-col terminals)
constexpr int LABEL_WIDTH_LG = 18; // Large label width (160+ terminals)

Display::Display() = default;

Display::~Display()
{
  if (_initialized) {
    shutdown();
  }
}

bool
Display::detectUtf8Support()
{
  const char *lang    = getenv("LANG");
  const char *lc_all  = getenv("LC_ALL");
  const char *lc_type = getenv("LC_CTYPE");

  auto has_utf8 = [](const char *s) {
    if (!s) {
      return false;
    }
    // Check for UTF-8 or UTF8 (case-insensitive)
    for (const char *p = s; *p; ++p) {
      if ((*p == 'U' || *p == 'u') && (*(p + 1) == 'T' || *(p + 1) == 't') && (*(p + 2) == 'F' || *(p + 2) == 'f')) {
        if (*(p + 3) == '-' && *(p + 4) == '8') {
          return true;
        }
        if (*(p + 3) == '8') {
          return true;
        }
      }
    }
    return false;
  };

  return has_utf8(lc_all) || has_utf8(lc_type) || has_utf8(lang);
}

bool
Display::initialize()
{
  if (_initialized) {
    return true;
  }

  // Enable UTF-8 locale
  setlocale(LC_ALL, "");

  // Auto-detect UTF-8 support from environment
  _ascii_mode = !detectUtf8Support();

  // Save original terminal settings and configure raw mode
  if (tcgetattr(STDIN_FILENO, &_orig_termios) == 0) {
    _termios_saved = true;

    struct termios raw = _orig_termios;
    // Disable canonical mode (line buffering) and echo
    raw.c_lflag &= ~(ICANON | ECHO);
    // Set minimum characters for read to 0 (non-blocking)
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  }

  // Get terminal size
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
    _width  = ws.ws_col;
    _height = ws.ws_row;
  } else {
    _width  = 80;
    _height = 24;
  }

  // Setup terminal for direct output
  hideCursor();
  printf("\033[?1049h"); // Switch to alternate screen buffer
  fflush(stdout);

  _initialized = true;
  return true;
}

void
Display::shutdown()
{
  if (_initialized) {
    showCursor();
    printf("\033[?1049l"); // Switch back to normal screen buffer
    resetColor();
    fflush(stdout);

    // Restore original terminal settings
    if (_termios_saved) {
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &_orig_termios);
    }

    _initialized = false;
  }
}

int
Display::getInput(int timeout_ms)
{
  // Use select() for timeout-based input
  fd_set          readfds;
  struct timeval  tv;
  struct timeval *tv_ptr = nullptr;

  FD_ZERO(&readfds);
  FD_SET(STDIN_FILENO, &readfds);

  if (timeout_ms >= 0) {
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    tv_ptr     = &tv;
  }

  int result = select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, tv_ptr);
  if (result <= 0) {
    return KEY_NONE; // Timeout or error
  }

  // Read the character
  unsigned char c;
  if (read(STDIN_FILENO, &c, 1) != 1) {
    return KEY_NONE;
  }

  // Check for escape sequence (arrow keys, etc.)
  if (c == 0x1B) { // ESC
    // Check if more characters are available (escape sequence)
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    tv.tv_sec  = 0;
    tv.tv_usec = 50000; // 50ms timeout to detect escape sequences

    if (select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &tv) > 0) {
      unsigned char seq[2];
      if (read(STDIN_FILENO, &seq[0], 1) == 1 && seq[0] == '[') {
        if (read(STDIN_FILENO, &seq[1], 1) == 1) {
          switch (seq[1]) {
          case 'A':
            return KEY_UP;
          case 'B':
            return KEY_DOWN;
          case 'C':
            return KEY_RIGHT;
          case 'D':
            return KEY_LEFT;
          }
        }
      }
    }
    // Just ESC key pressed (no sequence)
    return 0x1B;
  }

  return c;
}

void
Display::getTerminalSize(int &width, int &height) const
{
  width  = _width;
  height = _height;
}

void
Display::render(Stats &stats, Page page, [[maybe_unused]] bool absolute)
{
  // Update terminal size
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
    _width  = ws.ws_col;
    _height = ws.ws_row;
  }

  clearScreen();

  switch (page) {
  case Page::Main:
    renderMainPage(stats);
    break;
  case Page::Response:
    renderResponsePage(stats);
    break;
  case Page::Connection:
    renderConnectionPage(stats);
    break;
  case Page::Cache:
    renderCachePage(stats);
    break;
  case Page::SSL:
    renderSSLPage(stats);
    break;
  case Page::Errors:
    renderErrorsPage(stats);
    break;
  case Page::Performance:
    renderPerformancePage(stats);
    break;
  case Page::Graphs:
    renderGraphsPage(stats);
    break;
  case Page::Help: {
    std::string version;
    stats.getStat("version", version);
    renderHelpPage(stats.getHost(), version);
    break;
  }
  default:
    break;
  }

  fflush(stdout);
}

void
Display::drawBox(int x, int y, int width, int height, const std::string &title, short colorIdx)
{
  setColor(colorIdx);

  // Top border with rounded corners
  moveTo(y, x);
  printf("%s", boxChar(BoxChars::TopLeft, BoxChars::AsciiTopLeft));
  for (int i = 1; i < width - 1; ++i) {
    printf("%s", boxChar(BoxChars::Horizontal, BoxChars::AsciiHorizontal));
  }
  printf("%s", boxChar(BoxChars::TopRight, BoxChars::AsciiTopRight));

  // Title centered in top border
  if (!title.empty() && static_cast<int>(title.length()) < width - 4) {
    int title_x = x + (width - static_cast<int>(title.length()) - 2) / 2;
    moveTo(y, title_x);
    setBold();
    printf(" %s ", title.c_str());
    resetColor();
    setColor(colorIdx);
  }

  // Sides
  for (int i = 1; i < height - 1; ++i) {
    moveTo(y + i, x);
    printf("%s", boxChar(BoxChars::Vertical, BoxChars::AsciiVertical));
    moveTo(y + i, x + width - 1);
    printf("%s", boxChar(BoxChars::Vertical, BoxChars::AsciiVertical));
  }

  // Bottom border with rounded corners
  moveTo(y + height - 1, x);
  printf("%s", boxChar(BoxChars::BottomLeft, BoxChars::AsciiBottomLeft));
  for (int i = 1; i < width - 1; ++i) {
    printf("%s", boxChar(BoxChars::Horizontal, BoxChars::AsciiHorizontal));
  }
  printf("%s", boxChar(BoxChars::BottomRight, BoxChars::AsciiBottomRight));

  resetColor();
}

void
Display::drawSectionHeader(int y, int x1, int x2, const std::string &title)
{
  setColor(ColorPair::Border);

  // Draw top border line
  moveTo(y, x1);
  printf("%s", boxChar(BoxChars::TopLeft, BoxChars::AsciiTopLeft));
  for (int x = x1 + 1; x < x2 - 1; ++x) {
    printf("%s", boxChar(BoxChars::Horizontal, BoxChars::AsciiHorizontal));
  }
  if (x2 < _width) {
    printf("%s", boxChar(BoxChars::TopRight, BoxChars::AsciiTopRight));
  }

  // Center the title
  int title_len = static_cast<int>(title.length());
  int title_x   = x1 + (x2 - x1 - title_len - 2) / 2;
  moveTo(y, title_x);
  setBold();
  printf(" %s ", title.c_str());
  resetColor();
}

void
Display::drawStatTable(int x, int y, const std::vector<std::string> &items, Stats &stats, int labelWidth)
{
  int row = y;
  for (const auto &key : items) {
    if (row >= _height - 2) {
      break; // Don't overflow into status bar
    }

    std::string prettyName;
    double      value = 0;
    StatType    type;

    stats.getStat(key, value, prettyName, type);

    // Truncate label if needed
    if (static_cast<int>(prettyName.length()) > labelWidth) {
      prettyName = prettyName.substr(0, labelWidth - 1);
    }

    // Draw label with cyan color for visual hierarchy
    moveTo(row, x);
    setColor(ColorPair::Cyan);
    printf("%-*s", labelWidth, prettyName.c_str());
    resetColor();

    printStatValue(x + labelWidth, row, value, type);
    ++row;
  }
}

void
Display::drawStatGrid(int x, int y, int boxWidth, const std::vector<std::string> &items, Stats &stats, int cols)
{
  // Calculate column width based on box width and number of columns
  // Each stat needs: label (8 chars) + value (6 chars) + space (1 char) = 15 chars minimum
  int colWidth   = (boxWidth - 2) / cols; // -2 for box borders
  int labelWidth = 8;

  int row = y;
  int col = 0;

  for (const auto &key : items) {
    if (row >= _height - 2) {
      break;
    }

    std::string prettyName;
    double      value = 0;
    StatType    type;

    stats.getStat(key, value, prettyName, type);

    // Truncate label if needed
    if (static_cast<int>(prettyName.length()) > labelWidth) {
      prettyName = prettyName.substr(0, labelWidth);
    }

    int statX = x + (col * colWidth);

    // Draw label with trailing space
    moveTo(row, statX);
    setColor(ColorPair::Cyan);
    printf("%-*s ", labelWidth, prettyName.c_str()); // Note the space after %s
    resetColor();

    // Draw value (compact format for grid)
    char   buffer[16];
    char   suffix  = ' ';
    double display = value;
    short  color   = ColorPair::Green;

    if (isPercentage(type)) {
      if (value < 0.01) {
        color = ColorPair::Grey;
      }
      snprintf(buffer, sizeof(buffer), "%3.0f%%", display);
    } else {
      if (value > 1000000000.0) {
        display = value / 1000000000.0;
        suffix  = 'G';
        color   = ColorPair::Red;
      } else if (value > 1000000.0) {
        display = value / 1000000.0;
        suffix  = 'M';
        color   = ColorPair::Yellow;
      } else if (value > 1000.0) {
        display = value / 1000.0;
        suffix  = 'K';
        color   = ColorPair::Cyan;
      } else if (value < 0.01) {
        color = ColorPair::Grey;
      }
      snprintf(buffer, sizeof(buffer), "%5.0f%c", display, suffix);
    }

    setColor(color);
    setBold();
    printf("%s", buffer);
    resetColor();

    ++col;
    if (col >= cols) {
      col = 0;
      ++row;
    }
  }
}

void
Display::printStatValue(int x, int y, double value, StatType type)
{
  char   buffer[32];
  char   suffix   = ' ';
  double display  = value;
  short  color    = ColorPair::Green;
  bool   show_pct = isPercentage(type);

  if (!show_pct) {
    // Format large numbers with SI prefixes
    if (value > 1000000000000.0) {
      display = value / 1000000000000.0;
      suffix  = 'T';
      color   = ColorPair::Red;
    } else if (value > 1000000000.0) {
      display = value / 1000000000.0;
      suffix  = 'G';
      color   = ColorPair::Red;
    } else if (value > 1000000.0) {
      display = value / 1000000.0;
      suffix  = 'M';
      color   = ColorPair::Yellow;
    } else if (value > 1000.0) {
      display = value / 1000.0;
      suffix  = 'K';
      color   = ColorPair::Cyan;
    } else if (value < 0.01) {
      color = ColorPair::Grey;
    }
    snprintf(buffer, sizeof(buffer), "%7.1f%c", display, suffix);
  } else {
    // Percentage display with color coding based on context
    if (value > 90) {
      color = ColorPair::Green;
    } else if (value > 70) {
      color = ColorPair::Cyan;
    } else if (value > 50) {
      color = ColorPair::Yellow;
    } else if (value > 20) {
      color = ColorPair::Yellow;
    } else if (value < 0.01) {
      color = ColorPair::Grey;
    } else {
      color = ColorPair::Green;
    }
    snprintf(buffer, sizeof(buffer), "%6.1f%%", display);
  }

  moveTo(y, x);
  setColor(color);
  setBold();
  printf("%s", buffer);
  resetColor();
}

void
Display::drawProgressBar(int x, int y, double percent, int width)
{
  // Clamp percentage
  if (percent < 0)
    percent = 0;
  if (percent > 100)
    percent = 100;

  int filled = static_cast<int>((percent / 100.0) * width);

  // Choose color based on percentage
  short color;
  if (percent > 90) {
    color = ColorPair::Red;
  } else if (percent > 70) {
    color = ColorPair::Yellow;
  } else if (percent > 50) {
    color = ColorPair::Cyan;
  } else if (percent < 0.01) {
    color = ColorPair::Grey;
  } else {
    color = ColorPair::Green;
  }

  moveTo(y, x);
  setColor(color);
  for (int i = 0; i < filled; ++i) {
    printf("#");
  }

  // Draw empty portion
  setColor(ColorPair::Grey);
  for (int i = filled; i < width; ++i) {
    printf("-");
  }
  resetColor();
}

void
Display::drawGraphLine(int x, int y, const std::vector<double> &data, int width, bool colored)
{
  moveTo(y, x);

  // Take the last 'width' data points, or pad with zeros at the start
  size_t start = 0;
  if (data.size() > static_cast<size_t>(width)) {
    start = data.size() - width;
  }

  int drawn = 0;

  // Pad with empty blocks if data is shorter than width
  int padding = width - static_cast<int>(data.size() - start);
  for (int i = 0; i < padding; ++i) {
    if (_ascii_mode) {
      printf("%c", GraphChars::AsciiBlocks[0]);
    } else {
      printf("%s", GraphChars::Blocks[0]);
    }
    ++drawn;
  }

  // Draw the actual data
  for (size_t i = start; i < data.size() && drawn < width; ++i) {
    double val = data[i];
    if (val < 0.0)
      val = 0.0;
    if (val > 1.0)
      val = 1.0;

    // Map value to block index (0-8)
    int blockIdx = static_cast<int>(val * 8.0);
    if (blockIdx > 8)
      blockIdx = 8;

    // Color based on value (gradient: blue -> cyan -> green -> yellow -> red)
    if (colored) {
      if (val < 0.2) {
        setColor(ColorPair::Blue);
      } else if (val < 0.4) {
        setColor(ColorPair::Cyan);
      } else if (val < 0.6) {
        setColor(ColorPair::Green);
      } else if (val < 0.8) {
        setColor(ColorPair::Yellow);
      } else {
        setColor(ColorPair::Red);
      }
    }

    if (_ascii_mode) {
      printf("%c", GraphChars::AsciiBlocks[blockIdx]);
    } else {
      printf("%s", GraphChars::Blocks[blockIdx]);
    }
    ++drawn;
  }

  if (colored) {
    resetColor();
  }
}

void
Display::drawMultiGraphBox(int x, int y, int width,
                           const std::vector<std::tuple<std::string, std::vector<double>, std::string>> &graphs,
                           const std::string                                                            &title)
{
  int height = static_cast<int>(graphs.size()) + 2; // +2 for top/bottom borders

  // Draw box
  if (title.empty()) {
    // Simple separator
    moveTo(y, x);
    setColor(ColorPair::Border);
    printf("%s", boxChar(BoxChars::TopLeft, BoxChars::AsciiTopLeft));
    for (int i = 1; i < width - 1; ++i) {
      printf("%s", boxChar(BoxChars::Horizontal, BoxChars::AsciiHorizontal));
    }
    printf("%s", boxChar(BoxChars::TopRight, BoxChars::AsciiTopRight));
    resetColor();
  } else {
    drawBox(x, y, width, height, title, ColorPair::Border);
  }

  // Draw each graph row
  int contentWidth = width - 4;                                  // -2 for borders, -2 for padding
  int labelWidth   = 12;                                         // Fixed label width
  int valueWidth   = 10;                                         // Fixed value width
  int graphWidth   = contentWidth - labelWidth - valueWidth - 1; // -1 for space after label

  int row = y + 1;
  for (const auto &[label, data, value] : graphs) {
    if (row >= y + height - 1) {
      break;
    }

    // Position and draw border
    moveTo(row, x);
    setColor(ColorPair::Border);
    printf("%s", boxChar(BoxChars::Vertical, BoxChars::AsciiVertical));
    resetColor();

    // Draw label (cyan)
    printf(" ");
    setColor(ColorPair::Cyan);
    std::string truncLabel = label.substr(0, labelWidth);
    printf("%-*s", labelWidth, truncLabel.c_str());
    resetColor();

    // Draw graph
    printf(" ");
    drawGraphLine(x + 2 + labelWidth + 1, row, data, graphWidth, true);

    // Draw value (right-aligned)
    moveTo(row, x + width - valueWidth - 2);
    setColor(ColorPair::Green);
    setBold();
    printf("%*s", valueWidth, value.c_str());
    resetColor();

    // Right border
    moveTo(row, x + width - 1);
    setColor(ColorPair::Border);
    printf("%s", boxChar(BoxChars::Vertical, BoxChars::AsciiVertical));
    resetColor();

    ++row;
  }

  // Bottom border (if no title, we need to draw it)
  if (title.empty()) {
    moveTo(y + height - 1, x);
    setColor(ColorPair::Border);
    printf("%s", boxChar(BoxChars::BottomLeft, BoxChars::AsciiBottomLeft));
    for (int i = 1; i < width - 1; ++i) {
      printf("%s", boxChar(BoxChars::Horizontal, BoxChars::AsciiHorizontal));
    }
    printf("%s", boxChar(BoxChars::BottomRight, BoxChars::AsciiBottomRight));
    resetColor();
  }
}

void
Display::drawStatusBar(const std::string &host, Page page, bool absolute, bool connected)
{
  int status_y = _height - 1;

  // Fill status bar with blue background
  moveTo(status_y, 0);
  printf("\033[44m\033[97m"); // Blue background, bright white text
  for (int x = 0; x < _width; ++x) {
    printf(" ");
  }

  // Time with icon - cyan colored
  time_t    now = time(nullptr);
  struct tm nowtm;
  char      timeBuf[32];
  localtime_r(&now, &nowtm);
  strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &nowtm);

  moveTo(status_y, 1);
  printf("\033[96m"); // Bright cyan
  if (!_ascii_mode) {
    printf("⏱ %s", timeBuf);
  } else {
    printf("%s", timeBuf);
  }

  // Host with connection status indicator
  std::string hostDisplay;
  moveTo(status_y, 12);
  if (connected) {
    if (!_ascii_mode) {
      hostDisplay = "● " + host;
    } else {
      hostDisplay = "[OK] " + host;
    }
    printf("\033[92m"); // Bright green
  } else {
    if (!_ascii_mode) {
      hostDisplay = "○ connecting...";
    } else {
      hostDisplay = "[..] connecting...";
    }
    printf("\033[93m"); // Bright yellow
  }
  if (hostDisplay.length() > 25) {
    hostDisplay = hostDisplay.substr(0, 22) + "...";
  }
  printf("%-25s", hostDisplay.c_str());

  // Page indicator - bright white
  printf("\033[97m"); // Bright white
  int pageNum = static_cast<int>(page) + 1;
  int total   = getPageCount();
  moveTo(status_y, 40);
  printf("[%d/%d] ", pageNum, total);
  printf("\033[93m%s", getPageName(page)); // Yellow page name

  // Mode indicator - show ABS or RATE clearly
  moveTo(status_y, 60);
  if (absolute) {
    printf("\033[30m\033[43m ABS \033[0m\033[44m"); // Black on yellow background
  } else {
    printf("\033[30m\033[42m RATE \033[0m\033[44m"); // Black on green background
  }

  // Key hints (right-aligned) - dimmer color
  printf("\033[37m"); // Normal white (dimmer)
  std::string hints;
  if (_width > 110) {
    hints = absolute ? "q:Quit h:Help 1-8:Pages a:Rate" : "q:Quit h:Help 1-8:Pages a:Abs";
  } else if (_width > 80) {
    hints = "q h 1-8 a";
  } else {
    hints = "q h a";
  }
  int hints_x = _width - static_cast<int>(hints.length()) - 2;
  if (hints_x > 68) {
    moveTo(status_y, hints_x);
    printf("%s", hints.c_str());
  }

  printf("\033[0m"); // Reset
}

const char *
Display::getPageName(Page page)
{
  switch (page) {
  case Page::Main:
    return "Overview";
  case Page::Response:
    return "Responses";
  case Page::Connection:
    return "Connections";
  case Page::Cache:
    return "Cache";
  case Page::SSL:
    return "SSL/TLS";
  case Page::Errors:
    return "Errors";
  case Page::Performance:
    return "Performance";
  case Page::Graphs:
    return "Graphs";
  case Page::Help:
    return "Help";
  default:
    return "Unknown";
  }
}

void
Display::renderMainPage(Stats &stats)
{
  // Layout based on LAYOUT.md specifications:
  //   80x24   - 2x2 grid of 40-char boxes (2 stat columns per box)
  //   120x40  - 3 boxes per row x 5-6 rows
  //   160x40  - 4 boxes per row x multiple rows

  if (_width >= WIDTH_LARGE) {
    // 160x40: 4 boxes per row (40 chars each)
    render160Layout(stats);
  } else if (_width >= WIDTH_MEDIUM) {
    // 120x40: 3 boxes per row (40 chars each)
    render120Layout(stats);
  } else {
    // 80x24: 2 boxes per row (40 chars each)
    render80Layout(stats);
  }
}

namespace
{
  // Format a stat value to a string with suffix (right-aligned number, suffix attached)
  std::string
  formatStatValue(double value, StatType type, int width = 5)
  {
    char   buffer[32];
    char   suffix  = ' ';
    double display = value;

    if (isPercentage(type)) {
      // Format percentage
      snprintf(buffer, sizeof(buffer), "%*d%%", width - 1, static_cast<int>(display));
    } else {
      // Format with SI suffix
      if (value >= 1000000000000.0) {
        display = value / 1000000000000.0;
        suffix  = 'T';
      } else if (value >= 1000000000.0) {
        display = value / 1000000000.0;
        suffix  = 'G';
      } else if (value >= 1000000.0) {
        display = value / 1000000.0;
        suffix  = 'M';
      } else if (value >= 1000.0) {
        display = value / 1000.0;
        suffix  = 'K';
      }

      if (suffix != ' ') {
        snprintf(buffer, sizeof(buffer), "%*d%c", width - 1, static_cast<int>(display), suffix);
      } else {
        snprintf(buffer, sizeof(buffer), "%*d ", width - 1, static_cast<int>(display));
      }
    }

    return buffer;
  }

  // Get color for a stat value
  short
  getStatColor(double value, StatType type)
  {
    if (value < 0.01) {
      return ColorPair::Grey;
    }

    if (isPercentage(type)) {
      if (value > 90)
        return ColorPair::Green;
      if (value > 70)
        return ColorPair::Cyan;
      if (value > 50)
        return ColorPair::Yellow;
      return ColorPair::Green;
    }

    // Color by magnitude
    if (value >= 1000000000.0)
      return ColorPair::Red;
    if (value >= 1000000.0)
      return ColorPair::Yellow;
    if (value >= 1000.0)
      return ColorPair::Cyan;
    return ColorPair::Green;
  }
} // anonymous namespace

void
Display::drawStatPairRow(int x, int y, const std::string &key1, const std::string &key2, Stats &stats, short borderColor)
{
  // Format per LAYOUT.md:
  // | Label1       Value1   Label2      Value2 |
  // Total: 40 chars including borders
  // Content: 38 chars = 1 space + stat1(17) + gap(3) + stat2(16) + 1 space

  constexpr int GAP_WIDTH = 3;
  constexpr int LABEL1_W  = 12;
  constexpr int LABEL2_W  = 11;
  constexpr int VALUE_W   = 5;

  moveTo(y, x);
  setColor(borderColor);
  printf("%s", boxChar(BoxChars::Vertical, BoxChars::AsciiVertical));
  resetColor();
  printf(" ");

  // First stat
  std::string prettyName1;
  double      value1 = 0;
  StatType    type1;
  stats.getStat(key1, value1, prettyName1, type1);

  // Truncate label if needed
  if (prettyName1.length() > static_cast<size_t>(LABEL1_W)) {
    prettyName1 = prettyName1.substr(0, LABEL1_W);
  }

  setColor(ColorPair::Cyan);
  printf("%-*s", LABEL1_W, prettyName1.c_str());
  resetColor();

  std::string valStr1 = formatStatValue(value1, type1, VALUE_W);
  setColor(getStatColor(value1, type1));
  setBold();
  printf("%s", valStr1.c_str());
  resetColor();

  // Gap
  printf("%*s", GAP_WIDTH, "");

  // Second stat
  std::string prettyName2;
  double      value2 = 0;
  StatType    type2;
  stats.getStat(key2, value2, prettyName2, type2);

  if (prettyName2.length() > static_cast<size_t>(LABEL2_W)) {
    prettyName2 = prettyName2.substr(0, LABEL2_W);
  }

  setColor(ColorPair::Cyan);
  printf("%-*s", LABEL2_W, prettyName2.c_str());
  resetColor();

  std::string valStr2 = formatStatValue(value2, type2, VALUE_W);
  setColor(getStatColor(value2, type2));
  setBold();
  printf("%s", valStr2.c_str());
  resetColor();

  printf(" ");
  setColor(borderColor);
  printf("%s", boxChar(BoxChars::Vertical, BoxChars::AsciiVertical));
  resetColor();
}

void
Display::render80Layout(Stats &stats)
{
  // 80x24 Layout:
  // 2x2 grid of 40-char boxes
  // Top row: CLIENT | ORIGIN (9 content rows each)
  // Bottom row: CACHE | REQS/RESPONSES (9 content rows each)

  constexpr int BOX_WIDTH  = 40;
  constexpr int TOP_HEIGHT = 11; // 9 content rows + 2 borders
  constexpr int BOT_HEIGHT = 11;
  int           y2         = TOP_HEIGHT; // Start of second row (after first row ends)

  // Draw all four boxes
  drawBox(0, 0, BOX_WIDTH, TOP_HEIGHT, "CLIENT", ColorPair::Border);
  drawBox(BOX_WIDTH, 0, BOX_WIDTH, TOP_HEIGHT, "ORIGIN", ColorPair::Border4);
  drawBox(0, y2, BOX_WIDTH, BOT_HEIGHT, "CACHE", ColorPair::Border7);
  drawBox(BOX_WIDTH, y2, BOX_WIDTH, BOT_HEIGHT, "REQS/RESPONSES", ColorPair::Border5);

  // CLIENT box content (top left) - cyan border
  drawStatPairRow(0, 1, "client_req", "client_conn", stats, ColorPair::Border);
  drawStatPairRow(0, 2, "client_curr_conn", "client_actv_conn", stats, ColorPair::Border);
  drawStatPairRow(0, 3, "client_req_conn", "client_dyn_ka", stats, ColorPair::Border);
  drawStatPairRow(0, 4, "client_avg_size", "client_net", stats, ColorPair::Border);
  drawStatPairRow(0, 5, "client_req_time", "client_head", stats, ColorPair::Border);
  drawStatPairRow(0, 6, "client_body", "client_conn_h1", stats, ColorPair::Border);
  drawStatPairRow(0, 7, "client_conn_h2", "ssl_curr_sessions", stats, ColorPair::Border);
  drawStatPairRow(0, 8, "ssl_handshake_success", "ssl_error_ssl", stats, ColorPair::Border);
  drawStatPairRow(0, 9, "fresh_time", "cold_time", stats, ColorPair::Border);

  // ORIGIN box content (top right) - bright blue border
  drawStatPairRow(BOX_WIDTH, 1, "server_req", "server_conn", stats, ColorPair::Border4);
  drawStatPairRow(BOX_WIDTH, 2, "server_curr_conn", "server_req_conn", stats, ColorPair::Border4);
  drawStatPairRow(BOX_WIDTH, 3, "conn_fail", "abort", stats, ColorPair::Border4);
  drawStatPairRow(BOX_WIDTH, 4, "server_avg_size", "server_net", stats, ColorPair::Border4);
  drawStatPairRow(BOX_WIDTH, 5, "ka_total", "ka_count", stats, ColorPair::Border4);
  drawStatPairRow(BOX_WIDTH, 6, "server_head", "server_body", stats, ColorPair::Border4);
  drawStatPairRow(BOX_WIDTH, 7, "dns_lookups", "dns_hits", stats, ColorPair::Border4);
  drawStatPairRow(BOX_WIDTH, 8, "dns_ratio", "dns_entry", stats, ColorPair::Border4);
  drawStatPairRow(BOX_WIDTH, 9, "other_err", "t_conn_fail", stats, ColorPair::Border4);

  // CACHE box content (bottom left) - bright green border
  drawStatPairRow(0, y2 + 1, "disk_used", "ram_used", stats, ColorPair::Border7);
  drawStatPairRow(0, y2 + 2, "disk_total", "ram_total", stats, ColorPair::Border7);
  drawStatPairRow(0, y2 + 3, "ram_ratio", "fresh", stats, ColorPair::Border7);
  drawStatPairRow(0, y2 + 4, "reval", "cold", stats, ColorPair::Border7);
  drawStatPairRow(0, y2 + 5, "changed", "not", stats, ColorPair::Border7);
  drawStatPairRow(0, y2 + 6, "no", "entries", stats, ColorPair::Border7);
  drawStatPairRow(0, y2 + 7, "lookups", "cache_writes", stats, ColorPair::Border7);
  drawStatPairRow(0, y2 + 8, "read_active", "write_active", stats, ColorPair::Border7);
  drawStatPairRow(0, y2 + 9, "cache_updates", "cache_deletes", stats, ColorPair::Border7);

  // REQS/RESPONSES box content (bottom right) - bright yellow border
  drawStatPairRow(BOX_WIDTH, y2 + 1, "get", "post", stats, ColorPair::Border5);
  drawStatPairRow(BOX_WIDTH, y2 + 2, "head", "put", stats, ColorPair::Border5);
  drawStatPairRow(BOX_WIDTH, y2 + 3, "delete", "options", stats, ColorPair::Border5);
  drawStatPairRow(BOX_WIDTH, y2 + 4, "200", "206", stats, ColorPair::Border5);
  drawStatPairRow(BOX_WIDTH, y2 + 5, "301", "304", stats, ColorPair::Border5);
  drawStatPairRow(BOX_WIDTH, y2 + 6, "404", "502", stats, ColorPair::Border5);
  drawStatPairRow(BOX_WIDTH, y2 + 7, "2xx", "3xx", stats, ColorPair::Border5);
  drawStatPairRow(BOX_WIDTH, y2 + 8, "4xx", "5xx", stats, ColorPair::Border5);
  drawStatPairRow(BOX_WIDTH, y2 + 9, "503", "504", stats, ColorPair::Border5);
}

void
Display::render120Layout(Stats &stats)
{
  // 120x40 Layout: 3 boxes per row (40 chars each)
  // For 40 lines: 39 available (1 status bar)
  // 4 rows of boxes that don't share borders

  constexpr int BOX_WIDTH = 40;
  int           available = _height - 1; // Leave room for status bar

  // Calculate box heights: divide available space among 4 rows
  // For 40 lines: 39 / 4 = 9 with 3 left over
  int base_height = available / 4;
  int extra       = available % 4;
  int row1_height = base_height + (extra > 0 ? 1 : 0);
  int row2_height = base_height + (extra > 1 ? 1 : 0);
  int row3_height = base_height + (extra > 2 ? 1 : 0);
  int row4_height = base_height;

  int row = 0;

  // Row 1: CACHE | REQUESTS | CONNECTIONS
  // Consistent colors: CACHE=Green, REQUESTS=Yellow, CONNECTIONS=Blue
  drawBox(0, row, BOX_WIDTH, row1_height, "CACHE", ColorPair::Border7);
  drawBox(BOX_WIDTH, row, BOX_WIDTH, row1_height, "REQUESTS", ColorPair::Border5);
  drawBox(BOX_WIDTH * 2, row, BOX_WIDTH, row1_height, "CONNECTIONS", ColorPair::Border2);

  drawStatPairRow(0, row + 1, "disk_used", "disk_total", stats, ColorPair::Border7);
  drawStatPairRow(0, row + 2, "ram_used", "ram_total", stats, ColorPair::Border7);
  drawStatPairRow(0, row + 3, "entries", "avg_size", stats, ColorPair::Border7);
  drawStatPairRow(0, row + 4, "lookups", "cache_writes", stats, ColorPair::Border7);
  drawStatPairRow(0, row + 5, "read_active", "write_active", stats, ColorPair::Border7);
  if (row1_height > 7)
    drawStatPairRow(0, row + 6, "cache_updates", "cache_deletes", stats, ColorPair::Border7);

  drawStatPairRow(BOX_WIDTH, row + 1, "client_req", "server_req", stats, ColorPair::Border5);
  drawStatPairRow(BOX_WIDTH, row + 2, "get", "post", stats, ColorPair::Border5);
  drawStatPairRow(BOX_WIDTH, row + 3, "head", "put", stats, ColorPair::Border5);
  drawStatPairRow(BOX_WIDTH, row + 4, "delete", "options", stats, ColorPair::Border5);
  drawStatPairRow(BOX_WIDTH, row + 5, "100", "101", stats, ColorPair::Border5);
  if (row1_height > 7)
    drawStatPairRow(BOX_WIDTH, row + 6, "201", "204", stats, ColorPair::Border5);

  drawStatPairRow(BOX_WIDTH * 2, row + 1, "client_conn", "client_curr_conn", stats, ColorPair::Border2);
  drawStatPairRow(BOX_WIDTH * 2, row + 2, "client_actv_conn", "server_conn", stats, ColorPair::Border2);
  drawStatPairRow(BOX_WIDTH * 2, row + 3, "server_curr_conn", "server_req_conn", stats, ColorPair::Border2);
  drawStatPairRow(BOX_WIDTH * 2, row + 4, "client_conn_h1", "client_conn_h2", stats, ColorPair::Border2);
  drawStatPairRow(BOX_WIDTH * 2, row + 5, "h2_streams_total", "h2_streams_current", stats, ColorPair::Border2);
  if (row1_height > 7)
    drawStatPairRow(BOX_WIDTH * 2, row + 6, "net_open_conn", "net_throttled", stats, ColorPair::Border2);

  row += row1_height;

  // Row 2: HIT RATES | RESPONSES | BANDWIDTH
  // Consistent colors: HIT RATES=Red, RESPONSES=Yellow, BANDWIDTH=Magenta
  drawBox(0, row, BOX_WIDTH, row2_height, "HIT RATES", ColorPair::Border6);
  drawBox(BOX_WIDTH, row, BOX_WIDTH, row2_height, "RESPONSES", ColorPair::Border5);
  drawBox(BOX_WIDTH * 2, row, BOX_WIDTH, row2_height, "BANDWIDTH", ColorPair::Border3);

  drawStatPairRow(0, row + 1, "ram_ratio", "fresh", stats, ColorPair::Border6);
  drawStatPairRow(0, row + 2, "reval", "cold", stats, ColorPair::Border6);
  drawStatPairRow(0, row + 3, "changed", "not", stats, ColorPair::Border6);
  drawStatPairRow(0, row + 4, "no", "ram_hit", stats, ColorPair::Border6);
  drawStatPairRow(0, row + 5, "ram_miss", "fresh_time", stats, ColorPair::Border6);
  if (row2_height > 7)
    drawStatPairRow(0, row + 6, "reval_time", "cold_time", stats, ColorPair::Border6);

  drawStatPairRow(BOX_WIDTH, row + 1, "200", "206", stats, ColorPair::Border5);
  drawStatPairRow(BOX_WIDTH, row + 2, "301", "304", stats, ColorPair::Border5);
  drawStatPairRow(BOX_WIDTH, row + 3, "404", "502", stats, ColorPair::Border5);
  drawStatPairRow(BOX_WIDTH, row + 4, "503", "504", stats, ColorPair::Border5);
  drawStatPairRow(BOX_WIDTH, row + 5, "2xx", "3xx", stats, ColorPair::Border5);
  if (row2_height > 7)
    drawStatPairRow(BOX_WIDTH, row + 6, "4xx", "5xx", stats, ColorPair::Border5);

  drawStatPairRow(BOX_WIDTH * 2, row + 1, "client_head", "client_body", stats, ColorPair::Border3);
  drawStatPairRow(BOX_WIDTH * 2, row + 2, "server_head", "server_body", stats, ColorPair::Border3);
  drawStatPairRow(BOX_WIDTH * 2, row + 3, "client_avg_size", "server_avg_size", stats, ColorPair::Border3);
  drawStatPairRow(BOX_WIDTH * 2, row + 4, "client_net", "server_net", stats, ColorPair::Border3);
  drawStatPairRow(BOX_WIDTH * 2, row + 5, "client_size", "server_size", stats, ColorPair::Border3);
  if (row2_height > 7)
    drawStatPairRow(BOX_WIDTH * 2, row + 6, "client_req_time", "total_time", stats, ColorPair::Border3);

  row += row2_height;

  // Row 3: SSL/TLS | DNS | ERRORS
  // Consistent colors: SSL/TLS=Magenta, DNS=Cyan, ERRORS=Red
  drawBox(0, row, BOX_WIDTH, row3_height, "SSL/TLS", ColorPair::Border3);
  drawBox(BOX_WIDTH, row, BOX_WIDTH, row3_height, "DNS", ColorPair::Border);
  drawBox(BOX_WIDTH * 2, row, BOX_WIDTH, row3_height, "ERRORS", ColorPair::Border6);

  drawStatPairRow(0, row + 1, "ssl_success_in", "ssl_success_out", stats, ColorPair::Border3);
  drawStatPairRow(0, row + 2, "ssl_session_hit", "ssl_session_miss", stats, ColorPair::Border3);
  drawStatPairRow(0, row + 3, "tls_v12", "tls_v13", stats, ColorPair::Border3);
  drawStatPairRow(0, row + 4, "ssl_client_bad_cert", "ssl_origin_bad_cert", stats, ColorPair::Border3);
  drawStatPairRow(0, row + 5, "ssl_error_ssl", "ssl_error_syscall", stats, ColorPair::Border3);
  if (row3_height > 7)
    drawStatPairRow(0, row + 6, "ssl_attempts_in", "ssl_attempts_out", stats, ColorPair::Border3);

  drawStatPairRow(BOX_WIDTH, row + 1, "dns_lookups", "dns_hits", stats, ColorPair::Border);
  drawStatPairRow(BOX_WIDTH, row + 2, "dns_ratio", "dns_entry", stats, ColorPair::Border);
  drawStatPairRow(BOX_WIDTH, row + 3, "dns_serve_stale", "dns_in_flight", stats, ColorPair::Border);
  drawStatPairRow(BOX_WIDTH, row + 4, "dns_success", "dns_fail", stats, ColorPair::Border);
  drawStatPairRow(BOX_WIDTH, row + 5, "dns_lookup_time", "dns_success_time", stats, ColorPair::Border);
  if (row3_height > 7)
    drawStatPairRow(BOX_WIDTH, row + 6, "dns_total", "dns_retries", stats, ColorPair::Border);

  drawStatPairRow(BOX_WIDTH * 2, row + 1, "conn_fail", "abort", stats, ColorPair::Border6);
  drawStatPairRow(BOX_WIDTH * 2, row + 2, "client_abort", "other_err", stats, ColorPair::Border6);
  drawStatPairRow(BOX_WIDTH * 2, row + 3, "cache_read_errors", "cache_write_errors", stats, ColorPair::Border6);
  drawStatPairRow(BOX_WIDTH * 2, row + 4, "txn_aborts", "txn_other_errors", stats, ColorPair::Border6);
  drawStatPairRow(BOX_WIDTH * 2, row + 5, "h2_stream_errors", "h2_conn_errors", stats, ColorPair::Border6);
  if (row3_height > 7)
    drawStatPairRow(BOX_WIDTH * 2, row + 6, "err_client_read", "cache_lookup_fail", stats, ColorPair::Border6);

  row += row3_height;

  // Row 4: HTTP METHODS | RESPONSE TIMES | HTTP CODES
  // These boxes show different stats from rows 1-3
  if (row + row4_height <= _height - 1) {
    drawBox(0, row, BOX_WIDTH, row4_height, "HTTP METHODS", ColorPair::Border);
    drawBox(BOX_WIDTH, row, BOX_WIDTH, row4_height, "RESPONSE TIMES", ColorPair::Border4);
    drawBox(BOX_WIDTH * 2, row, BOX_WIDTH, row4_height, "HTTP CODES", ColorPair::Border2);

    // HTTP Methods breakdown
    drawStatPairRow(0, row + 1, "get", "post", stats, ColorPair::Border);
    drawStatPairRow(0, row + 2, "head", "put", stats, ColorPair::Border);
    drawStatPairRow(0, row + 3, "delete", "options", stats, ColorPair::Border);
    drawStatPairRow(0, row + 4, "client_req", "server_req", stats, ColorPair::Border);
    drawStatPairRow(0, row + 5, "client_req_conn", "server_req_conn", stats, ColorPair::Border);
    if (row4_height > 7)
      drawStatPairRow(0, row + 6, "client_dyn_ka", "client_req_time", stats, ColorPair::Border);

    // Response times for different cache states
    drawStatPairRow(BOX_WIDTH, row + 1, "fresh_time", "reval_time", stats, ColorPair::Border4);
    drawStatPairRow(BOX_WIDTH, row + 2, "cold_time", "changed_time", stats, ColorPair::Border4);
    drawStatPairRow(BOX_WIDTH, row + 3, "not_time", "no_time", stats, ColorPair::Border4);
    drawStatPairRow(BOX_WIDTH, row + 4, "total_time", "client_req_time", stats, ColorPair::Border4);
    drawStatPairRow(BOX_WIDTH, row + 5, "ssl_handshake_time", "ka_total", stats, ColorPair::Border4);
    if (row4_height > 7)
      drawStatPairRow(BOX_WIDTH, row + 6, "ka_count", "ssl_origin_reused", stats, ColorPair::Border4);

    // Additional HTTP codes not shown elsewhere
    drawStatPairRow(BOX_WIDTH * 2, row + 1, "100", "101", stats, ColorPair::Border2);
    drawStatPairRow(BOX_WIDTH * 2, row + 2, "201", "204", stats, ColorPair::Border2);
    drawStatPairRow(BOX_WIDTH * 2, row + 3, "302", "307", stats, ColorPair::Border2);
    drawStatPairRow(BOX_WIDTH * 2, row + 4, "400", "401", stats, ColorPair::Border2);
    drawStatPairRow(BOX_WIDTH * 2, row + 5, "403", "500", stats, ColorPair::Border2);
    if (row4_height > 7)
      drawStatPairRow(BOX_WIDTH * 2, row + 6, "501", "505", stats, ColorPair::Border2);
  }
}

void
Display::render160Layout(Stats &stats)
{
  // 160x40 Layout: 4 boxes per row (40 chars each)
  // For 40 lines: 39 available (1 status bar)
  // 4 rows of boxes that don't share borders

  constexpr int BOX_WIDTH = 40;
  int           available = _height - 1; // Leave room for status bar

  // Calculate box heights: divide available space among 4 rows
  // For 40 lines: 39 / 4 = 9 with 3 left over
  int base_height = available / 4;
  int extra       = available % 4;
  int row1_height = base_height + (extra > 0 ? 1 : 0);
  int row2_height = base_height + (extra > 1 ? 1 : 0);
  int row3_height = base_height + (extra > 2 ? 1 : 0);
  int row4_height = base_height;

  int row = 0;

  // Row 1: CACHE | CLIENT | ORIGIN | REQUESTS
  // Consistent colors: CACHE=Green, CLIENT=Cyan, ORIGIN=Bright Blue, REQUESTS=Yellow
  drawBox(0, row, BOX_WIDTH, row1_height, "CACHE", ColorPair::Border7);
  drawBox(BOX_WIDTH, row, BOX_WIDTH, row1_height, "CLIENT", ColorPair::Border);
  drawBox(BOX_WIDTH * 2, row, BOX_WIDTH, row1_height, "ORIGIN", ColorPair::Border4);
  drawBox(BOX_WIDTH * 3, row, BOX_WIDTH, row1_height, "REQUESTS", ColorPair::Border5);

  drawStatPairRow(0, row + 1, "disk_used", "disk_total", stats, ColorPair::Border7);
  drawStatPairRow(0, row + 2, "ram_used", "ram_total", stats, ColorPair::Border7);
  drawStatPairRow(0, row + 3, "entries", "avg_size", stats, ColorPair::Border7);
  drawStatPairRow(0, row + 4, "lookups", "cache_writes", stats, ColorPair::Border7);
  drawStatPairRow(0, row + 5, "read_active", "write_active", stats, ColorPair::Border7);
  if (row1_height > 7)
    drawStatPairRow(0, row + 6, "cache_updates", "cache_deletes", stats, ColorPair::Border7);

  drawStatPairRow(BOX_WIDTH, row + 1, "client_req", "client_conn", stats, ColorPair::Border);
  drawStatPairRow(BOX_WIDTH, row + 2, "client_curr_conn", "client_actv_conn", stats, ColorPair::Border);
  drawStatPairRow(BOX_WIDTH, row + 3, "client_req_conn", "client_dyn_ka", stats, ColorPair::Border);
  drawStatPairRow(BOX_WIDTH, row + 4, "client_avg_size", "client_net", stats, ColorPair::Border);
  drawStatPairRow(BOX_WIDTH, row + 5, "client_req_time", "client_head", stats, ColorPair::Border);
  if (row1_height > 7)
    drawStatPairRow(BOX_WIDTH, row + 6, "client_body", "conn_fail", stats, ColorPair::Border);

  drawStatPairRow(BOX_WIDTH * 2, row + 1, "server_req", "server_conn", stats, ColorPair::Border4);
  drawStatPairRow(BOX_WIDTH * 2, row + 2, "server_curr_conn", "server_req_conn", stats, ColorPair::Border4);
  drawStatPairRow(BOX_WIDTH * 2, row + 3, "conn_fail", "abort", stats, ColorPair::Border4);
  drawStatPairRow(BOX_WIDTH * 2, row + 4, "server_avg_size", "server_net", stats, ColorPair::Border4);
  drawStatPairRow(BOX_WIDTH * 2, row + 5, "ka_total", "ka_count", stats, ColorPair::Border4);
  if (row1_height > 7)
    drawStatPairRow(BOX_WIDTH * 2, row + 6, "server_head", "server_body", stats, ColorPair::Border4);

  drawStatPairRow(BOX_WIDTH * 3, row + 1, "get", "post", stats, ColorPair::Border5);
  drawStatPairRow(BOX_WIDTH * 3, row + 2, "head", "put", stats, ColorPair::Border5);
  drawStatPairRow(BOX_WIDTH * 3, row + 3, "delete", "options", stats, ColorPair::Border5);
  drawStatPairRow(BOX_WIDTH * 3, row + 4, "1xx", "2xx", stats, ColorPair::Border5);
  drawStatPairRow(BOX_WIDTH * 3, row + 5, "3xx", "4xx", stats, ColorPair::Border5);
  if (row1_height > 7)
    drawStatPairRow(BOX_WIDTH * 3, row + 6, "5xx", "client_req_conn", stats, ColorPair::Border5);

  row += row1_height;

  // Row 2: HIT RATES | CONNECTIONS | SSL/TLS | RESPONSES
  // Consistent colors: HIT RATES=Red, CONNECTIONS=Blue, SSL/TLS=Magenta, RESPONSES=Yellow
  drawBox(0, row, BOX_WIDTH, row2_height, "HIT RATES", ColorPair::Border6);
  drawBox(BOX_WIDTH, row, BOX_WIDTH, row2_height, "CONNECTIONS", ColorPair::Border2);
  drawBox(BOX_WIDTH * 2, row, BOX_WIDTH, row2_height, "SSL/TLS", ColorPair::Border3);
  drawBox(BOX_WIDTH * 3, row, BOX_WIDTH, row2_height, "RESPONSES", ColorPair::Border5);

  drawStatPairRow(0, row + 1, "ram_ratio", "fresh", stats, ColorPair::Border6);
  drawStatPairRow(0, row + 2, "reval", "cold", stats, ColorPair::Border6);
  drawStatPairRow(0, row + 3, "changed", "not", stats, ColorPair::Border6);
  drawStatPairRow(0, row + 4, "no", "ram_hit", stats, ColorPair::Border6);
  drawStatPairRow(0, row + 5, "ram_miss", "fresh_time", stats, ColorPair::Border6);
  if (row2_height > 7)
    drawStatPairRow(0, row + 6, "reval_time", "cold_time", stats, ColorPair::Border6);

  drawStatPairRow(BOX_WIDTH, row + 1, "client_conn_h1", "client_curr_conn_h1", stats, ColorPair::Border2);
  drawStatPairRow(BOX_WIDTH, row + 2, "client_conn_h2", "client_curr_conn_h2", stats, ColorPair::Border2);
  drawStatPairRow(BOX_WIDTH, row + 3, "h2_streams_total", "h2_streams_current", stats, ColorPair::Border2);
  drawStatPairRow(BOX_WIDTH, row + 4, "client_actv_conn_h1", "client_actv_conn_h2", stats, ColorPair::Border2);
  drawStatPairRow(BOX_WIDTH, row + 5, "net_throttled", "net_open_conn", stats, ColorPair::Border2);
  if (row2_height > 7)
    drawStatPairRow(BOX_WIDTH, row + 6, "client_dyn_ka", "ssl_curr_sessions", stats, ColorPair::Border2);

  drawStatPairRow(BOX_WIDTH * 2, row + 1, "ssl_success_in", "ssl_success_out", stats, ColorPair::Border3);
  drawStatPairRow(BOX_WIDTH * 2, row + 2, "ssl_session_hit", "ssl_session_miss", stats, ColorPair::Border3);
  drawStatPairRow(BOX_WIDTH * 2, row + 3, "tls_v12", "tls_v13", stats, ColorPair::Border3);
  drawStatPairRow(BOX_WIDTH * 2, row + 4, "ssl_client_bad_cert", "ssl_origin_bad_cert", stats, ColorPair::Border3);
  drawStatPairRow(BOX_WIDTH * 2, row + 5, "ssl_error_ssl", "ssl_error_syscall", stats, ColorPair::Border3);
  if (row2_height > 7)
    drawStatPairRow(BOX_WIDTH * 2, row + 6, "ssl_attempts_in", "ssl_attempts_out", stats, ColorPair::Border3);

  drawStatPairRow(BOX_WIDTH * 3, row + 1, "200", "201", stats, ColorPair::Border5);
  drawStatPairRow(BOX_WIDTH * 3, row + 2, "204", "206", stats, ColorPair::Border5);
  drawStatPairRow(BOX_WIDTH * 3, row + 3, "301", "302", stats, ColorPair::Border5);
  drawStatPairRow(BOX_WIDTH * 3, row + 4, "304", "307", stats, ColorPair::Border5);
  drawStatPairRow(BOX_WIDTH * 3, row + 5, "400", "404", stats, ColorPair::Border5);
  if (row2_height > 7)
    drawStatPairRow(BOX_WIDTH * 3, row + 6, "500", "502", stats, ColorPair::Border5);

  row += row2_height;

  // Row 3: BANDWIDTH | DNS | ERRORS | TOTALS
  // Consistent colors: BANDWIDTH=Magenta, DNS=Cyan, ERRORS=Red, TOTALS=Blue
  drawBox(0, row, BOX_WIDTH, row3_height, "BANDWIDTH", ColorPair::Border3);
  drawBox(BOX_WIDTH, row, BOX_WIDTH, row3_height, "DNS", ColorPair::Border);
  drawBox(BOX_WIDTH * 2, row, BOX_WIDTH, row3_height, "ERRORS", ColorPair::Border6);
  drawBox(BOX_WIDTH * 3, row, BOX_WIDTH, row3_height, "TOTALS", ColorPair::Border2);

  drawStatPairRow(0, row + 1, "client_head", "client_body", stats, ColorPair::Border3);
  drawStatPairRow(0, row + 2, "server_head", "server_body", stats, ColorPair::Border3);
  drawStatPairRow(0, row + 3, "client_avg_size", "server_avg_size", stats, ColorPair::Border3);
  drawStatPairRow(0, row + 4, "client_net", "server_net", stats, ColorPair::Border3);
  drawStatPairRow(0, row + 5, "client_size", "server_size", stats, ColorPair::Border3);
  if (row3_height > 7)
    drawStatPairRow(0, row + 6, "client_req_time", "total_time", stats, ColorPair::Border3);

  drawStatPairRow(BOX_WIDTH, row + 1, "dns_lookups", "dns_hits", stats, ColorPair::Border);
  drawStatPairRow(BOX_WIDTH, row + 2, "dns_ratio", "dns_entry", stats, ColorPair::Border);
  drawStatPairRow(BOX_WIDTH, row + 3, "dns_serve_stale", "dns_in_flight", stats, ColorPair::Border);
  drawStatPairRow(BOX_WIDTH, row + 4, "dns_success", "dns_fail", stats, ColorPair::Border);
  drawStatPairRow(BOX_WIDTH, row + 5, "dns_lookup_time", "dns_success_time", stats, ColorPair::Border);
  if (row3_height > 7)
    drawStatPairRow(BOX_WIDTH, row + 6, "dns_total", "dns_retries", stats, ColorPair::Border);

  drawStatPairRow(BOX_WIDTH * 2, row + 1, "conn_fail", "abort", stats, ColorPair::Border6);
  drawStatPairRow(BOX_WIDTH * 2, row + 2, "client_abort", "other_err", stats, ColorPair::Border6);
  drawStatPairRow(BOX_WIDTH * 2, row + 3, "cache_read_errors", "cache_write_errors", stats, ColorPair::Border6);
  drawStatPairRow(BOX_WIDTH * 2, row + 4, "txn_aborts", "txn_other_errors", stats, ColorPair::Border6);
  drawStatPairRow(BOX_WIDTH * 2, row + 5, "h2_stream_errors", "h2_conn_errors", stats, ColorPair::Border6);
  if (row3_height > 7)
    drawStatPairRow(BOX_WIDTH * 2, row + 6, "err_client_read", "cache_lookup_fail", stats, ColorPair::Border6);

  drawStatPairRow(BOX_WIDTH * 3, row + 1, "client_req", "server_req", stats, ColorPair::Border2);
  drawStatPairRow(BOX_WIDTH * 3, row + 2, "client_conn", "server_conn", stats, ColorPair::Border2);
  drawStatPairRow(BOX_WIDTH * 3, row + 3, "2xx", "3xx", stats, ColorPair::Border2);
  drawStatPairRow(BOX_WIDTH * 3, row + 4, "4xx", "5xx", stats, ColorPair::Border2);
  drawStatPairRow(BOX_WIDTH * 3, row + 5, "abort", "conn_fail", stats, ColorPair::Border2);
  if (row3_height > 7)
    drawStatPairRow(BOX_WIDTH * 3, row + 6, "other_err", "t_conn_fail", stats, ColorPair::Border2);

  row += row3_height;

  // Row 4: HTTP CODES | CACHE DETAIL | ORIGIN DETAIL | MISC STATS
  // Consistent colors: HTTP CODES=Yellow, CACHE DETAIL=Green, ORIGIN DETAIL=Bright Blue, MISC=Cyan
  if (row + row4_height <= _height - 1) {
    drawBox(0, row, BOX_WIDTH, row4_height, "HTTP CODES", ColorPair::Border5);
    drawBox(BOX_WIDTH, row, BOX_WIDTH, row4_height, "CACHE DETAIL", ColorPair::Border7);
    drawBox(BOX_WIDTH * 2, row, BOX_WIDTH, row4_height, "ORIGIN DETAIL", ColorPair::Border4);
    drawBox(BOX_WIDTH * 3, row, BOX_WIDTH, row4_height, "MISC STATS", ColorPair::Border);

    drawStatPairRow(0, row + 1, "100", "101", stats, ColorPair::Border5);
    drawStatPairRow(0, row + 2, "200", "201", stats, ColorPair::Border5);
    drawStatPairRow(0, row + 3, "204", "206", stats, ColorPair::Border5);
    drawStatPairRow(0, row + 4, "301", "302", stats, ColorPair::Border5);
    drawStatPairRow(0, row + 5, "304", "307", stats, ColorPair::Border5);
    if (row4_height > 7)
      drawStatPairRow(0, row + 6, "400", "401", stats, ColorPair::Border5);

    drawStatPairRow(BOX_WIDTH, row + 1, "ram_hit", "ram_miss", stats, ColorPair::Border7);
    drawStatPairRow(BOX_WIDTH, row + 2, "update_active", "cache_updates", stats, ColorPair::Border7);
    drawStatPairRow(BOX_WIDTH, row + 3, "cache_deletes", "avg_size", stats, ColorPair::Border7);
    drawStatPairRow(BOX_WIDTH, row + 4, "fresh", "reval", stats, ColorPair::Border7);
    drawStatPairRow(BOX_WIDTH, row + 5, "cold", "changed", stats, ColorPair::Border7);
    if (row4_height > 7)
      drawStatPairRow(BOX_WIDTH, row + 6, "not", "no", stats, ColorPair::Border7);

    drawStatPairRow(BOX_WIDTH * 2, row + 1, "ssl_origin_reused", "ssl_origin_bad_cert", stats, ColorPair::Border4);
    drawStatPairRow(BOX_WIDTH * 2, row + 2, "ssl_origin_expired", "ssl_origin_revoked", stats, ColorPair::Border4);
    drawStatPairRow(BOX_WIDTH * 2, row + 3, "ssl_origin_unknown_ca", "ssl_origin_verify_fail", stats, ColorPair::Border4);
    drawStatPairRow(BOX_WIDTH * 2, row + 4, "ssl_origin_decrypt_fail", "ssl_origin_wrong_ver", stats, ColorPair::Border4);
    drawStatPairRow(BOX_WIDTH * 2, row + 5, "ssl_origin_other", "ssl_handshake_time", stats, ColorPair::Border4);
    if (row4_height > 7)
      drawStatPairRow(BOX_WIDTH * 2, row + 6, "tls_v10", "tls_v11", stats, ColorPair::Border4);

    drawStatPairRow(BOX_WIDTH * 3, row + 1, "txn_aborts", "txn_possible_aborts", stats, ColorPair::Border);
    drawStatPairRow(BOX_WIDTH * 3, row + 2, "txn_other_errors", "h2_session_die_error", stats, ColorPair::Border);
    drawStatPairRow(BOX_WIDTH * 3, row + 3, "h2_session_die_high_error", "err_conn_fail", stats, ColorPair::Border);
    drawStatPairRow(BOX_WIDTH * 3, row + 4, "err_client_abort", "err_client_read", stats, ColorPair::Border);
    drawStatPairRow(BOX_WIDTH * 3, row + 5, "changed_time", "not_time", stats, ColorPair::Border);
    if (row4_height > 7)
      drawStatPairRow(BOX_WIDTH * 3, row + 6, "no_time", "client_dyn_ka", stats, ColorPair::Border);
  }
}

void
Display::renderResponsePage(Stats &stats)
{
  // Layout: 80x24 -> 2 cols, 120x40 -> 3 cols, 160+ -> 5 cols
  int box_height = std::min(10, _height - 4);

  if (_width >= WIDTH_LARGE) {
    // Wide terminal: 5 columns for each response class
    int w = _width / 5;

    drawBox(0, 0, w, box_height, "1xx", ColorPair::Border);
    drawBox(w, 0, w, box_height, "2xx", ColorPair::Border2);
    drawBox(w * 2, 0, w, box_height, "3xx", ColorPair::Border3);
    drawBox(w * 3, 0, w, box_height, "4xx", ColorPair::Border);
    drawBox(w * 4, 0, _width - w * 4, box_height, "5xx", ColorPair::Border2);

    std::vector<std::string> r1 = {"100", "101", "1xx"};
    drawStatTable(2, 1, r1, stats, 6);

    std::vector<std::string> r2 = {"200", "201", "204", "206", "2xx"};
    drawStatTable(w + 2, 1, r2, stats, 6);

    std::vector<std::string> r3 = {"301", "302", "304", "307", "3xx"};
    drawStatTable(w * 2 + 2, 1, r3, stats, 6);

    std::vector<std::string> r4 = {"400", "401", "403", "404", "408", "4xx"};
    drawStatTable(w * 3 + 2, 1, r4, stats, 6);

    std::vector<std::string> r5 = {"500", "502", "503", "504", "5xx"};
    drawStatTable(w * 4 + 2, 1, r5, stats, 6);

    // Extended codes if height allows
    if (_height > box_height + 8) {
      int y2 = box_height + 1;
      int h2 = std::min(_height - box_height - 3, 8);

      drawBox(0, y2, _width / 2, h2, "4xx EXTENDED", ColorPair::Border3);
      drawBox(_width / 2, y2, _width - _width / 2, h2, "METHODS", ColorPair::Border);

      std::vector<std::string> r4ext = {"405", "406", "409", "410", "413", "414", "416"};
      drawStatTable(2, y2 + 1, r4ext, stats, 6);

      std::vector<std::string> methods = {"get", "head", "post", "put", "delete"};
      drawStatTable(_width / 2 + 2, y2 + 1, methods, stats, 8);
    }

  } else if (_width >= WIDTH_MEDIUM) {
    // Medium terminal: 3 columns
    int w = _width / 3;

    drawBox(0, 0, w, box_height, "1xx/2xx", ColorPair::Border);
    drawBox(w, 0, w, box_height, "3xx/4xx", ColorPair::Border2);
    drawBox(w * 2, 0, _width - w * 2, box_height, "5xx/ERR", ColorPair::Border3);

    std::vector<std::string> r12 = {"1xx", "200", "201", "206", "2xx"};
    drawStatTable(2, 1, r12, stats, 6);

    std::vector<std::string> r34 = {"301", "302", "304", "3xx", "404", "4xx"};
    drawStatTable(w + 2, 1, r34, stats, 6);

    std::vector<std::string> r5e = {"500", "502", "503", "5xx", "conn_fail"};
    drawStatTable(w * 2 + 2, 1, r5e, stats, 8);

  } else {
    // Classic 80x24: 3x2 grid layout for response codes and methods
    // For 24 lines: 23 usable (1 status bar), need 3 rows of boxes
    int w         = _width / 2;
    int available = _height - 1; // Leave room for status bar

    // Each box needs: 2 border rows + content rows
    // 1xx/2xx: 5 stats max -> 7 rows
    // 3xx/4xx: 5 stats max -> 7 rows
    // 5xx/Methods: 5 stats max -> remaining
    int row1_height = 7;
    int row2_height = 7;
    int row3_height = available - row1_height - row2_height;

    // Top row: 1xx and 2xx
    drawBox(0, 0, w, row1_height, "1xx", ColorPair::Border);
    drawBox(w, 0, _width - w, row1_height, "2xx", ColorPair::Border2);

    std::vector<std::string> r1 = {"100", "101", "1xx"};
    drawStatTable(2, 1, r1, stats, 6);

    std::vector<std::string> r2 = {"200", "201", "204", "206", "2xx"};
    drawStatTable(w + 2, 1, r2, stats, 6);

    // Middle row: 3xx and 4xx
    int y2 = row1_height;
    drawBox(0, y2, w, row2_height, "3xx", ColorPair::Border3);
    drawBox(w, y2, _width - w, row2_height, "4xx", ColorPair::Border);

    std::vector<std::string> r3 = {"301", "302", "304", "307", "3xx"};
    drawStatTable(2, y2 + 1, r3, stats, 6);

    std::vector<std::string> r4 = {"400", "401", "403", "404", "4xx"};
    drawStatTable(w + 2, y2 + 1, r4, stats, 6);

    // Bottom row: 5xx and Methods
    int y3 = y2 + row2_height;
    if (row3_height > 2) {
      drawBox(0, y3, w, row3_height, "5xx", ColorPair::Border2);
      drawBox(w, y3, _width - w, row3_height, "METHODS", ColorPair::Border3);

      std::vector<std::string> r5 = {"500", "502", "503", "504", "5xx"};
      drawStatTable(2, y3 + 1, r5, stats, 6);

      std::vector<std::string> methods = {"get", "head", "post", "put", "delete"};
      drawStatTable(w + 2, y3 + 1, methods, stats, 8);
    }
  }
}

void
Display::renderConnectionPage(Stats &stats)
{
  // Layout with protocol, client, origin, bandwidth, and network stats
  // For 80x24: 3 rows of boxes, each with enough height for their stats
  int w           = _width / 2;
  int label_width = (_width >= WIDTH_MEDIUM) ? LABEL_WIDTH_MD : LABEL_WIDTH_SM;

  // Calculate box heights based on available space (leave 1 row for status bar)
  int available   = _height - 1;                           // Leave room for status bar
  int row1_height = 7;                                     // HTTP/1.x (3 stats) and HTTP/2 (5 stats)
  int row2_height = 7;                                     // CLIENT (5 stats) and ORIGIN (4 stats)
  int row3_height = available - row1_height - row2_height; // BANDWIDTH and NETWORK

  // Adjust if terminal is too small
  if (available < 20) {
    row1_height = 5;
    row2_height = 5;
    row3_height = available - row1_height - row2_height;
  }

  // Top row: HTTP/1.x and HTTP/2
  drawBox(0, 0, w, row1_height, "HTTP/1.x", ColorPair::Border);
  drawBox(w, 0, _width - w, row1_height, "HTTP/2", ColorPair::Border2);

  std::vector<std::string> h1 = {"client_conn_h1", "client_curr_conn_h1", "client_actv_conn_h1"};
  drawStatTable(2, 1, h1, stats, label_width);

  std::vector<std::string> h2 = {"client_conn_h2", "client_curr_conn_h2", "client_actv_conn_h2", "h2_streams_total",
                                 "h2_streams_current"};
  drawStatTable(w + 2, 1, h2, stats, label_width);

  // Middle row: Client and Origin
  int y2 = row1_height;
  drawBox(0, y2, w, row2_height, "CLIENT", ColorPair::Border3);
  drawBox(w, y2, _width - w, row2_height, "ORIGIN", ColorPair::Border);

  std::vector<std::string> client = {"client_req", "client_conn", "client_curr_conn", "client_actv_conn", "client_req_conn"};
  drawStatTable(2, y2 + 1, client, stats, label_width);

  std::vector<std::string> origin = {"server_req", "server_conn", "server_curr_conn", "server_req_conn"};
  drawStatTable(w + 2, y2 + 1, origin, stats, label_width);

  // Bottom row: Bandwidth and Network
  int y3 = y2 + row2_height;
  if (row3_height > 2) {
    drawBox(0, y3, w, row3_height, "BANDWIDTH", ColorPair::Border2);
    drawBox(w, y3, _width - w, row3_height, "NETWORK", ColorPair::Border3);

    std::vector<std::string> bw = {"client_head", "client_body", "client_net", "client_avg_size"};
    drawStatTable(2, y3 + 1, bw, stats, label_width);

    std::vector<std::string> net = {"server_head", "server_body", "server_net", "server_avg_size"};
    drawStatTable(w + 2, y3 + 1, net, stats, label_width);
  }
}

void
Display::renderCachePage(Stats &stats)
{
  // Layout: 80x24 -> 2 cols, 120x40 -> 3 cols, 160+ -> 4 cols
  int box_height = std::min(10, _height / 2);

  if (_width >= WIDTH_LARGE) {
    // Wide terminal: 4 columns
    int w           = _width / 4;
    int label_width = LABEL_WIDTH_MD;

    drawBox(0, 0, w, box_height, "STORAGE", ColorPair::Border);
    drawBox(w, 0, w, box_height, "OPERATIONS", ColorPair::Border2);
    drawBox(w * 2, 0, w, box_height, "HIT/MISS", ColorPair::Border3);
    drawBox(w * 3, 0, _width - w * 3, box_height, "LATENCY", ColorPair::Border);

    std::vector<std::string> storage = {"disk_used", "disk_total", "ram_used", "ram_total", "entries", "avg_size"};
    drawStatTable(2, 1, storage, stats, label_width);

    std::vector<std::string> ops = {"lookups", "cache_writes", "cache_updates", "cache_deletes", "read_active", "write_active"};
    drawStatTable(w + 2, 1, ops, stats, label_width);

    std::vector<std::string> hits = {"ram_ratio", "ram_hit", "ram_miss", "fresh", "reval", "cold"};
    drawStatTable(w * 2 + 2, 1, hits, stats, label_width);

    std::vector<std::string> times = {"fresh_time", "reval_time", "cold_time", "changed_time"};
    drawStatTable(w * 3 + 2, 1, times, stats, label_width);

    // DNS section
    if (_height > box_height + 8) {
      int y2 = box_height + 1;
      int h2 = std::min(_height - box_height - 3, 6);

      drawBox(0, y2, _width, h2, "DNS CACHE", ColorPair::Border2);

      std::vector<std::string> dns = {"dns_lookups", "dns_hits", "dns_ratio", "dns_entry"};
      drawStatTable(2, y2 + 1, dns, stats, label_width);
    }

  } else {
    // Classic/Medium terminal: 2x3 grid layout
    int w           = _width / 2;
    int label_width = (_width >= WIDTH_MEDIUM) ? LABEL_WIDTH_MD : LABEL_WIDTH_SM;
    int available   = _height - 1; // Leave room for status bar

    // Storage/Operations: 6 stats -> 8 rows
    // Hit Rates/Latency: 7 stats / 6 stats -> 9 rows
    // DNS: 4 stats -> remaining
    int row1_height = 8;
    int row2_height = 9;
    int row3_height = available - row1_height - row2_height;

    // Adjust for smaller terminals
    if (available < 22) {
      row1_height = 7;
      row2_height = 7;
      row3_height = available - row1_height - row2_height;
    }

    // Top row: Storage and Operations
    drawBox(0, 0, w, row1_height, "STORAGE", ColorPair::Border);
    drawBox(w, 0, _width - w, row1_height, "OPERATIONS", ColorPair::Border2);

    std::vector<std::string> storage = {"disk_used", "disk_total", "ram_used", "ram_total", "entries", "avg_size"};
    drawStatTable(2, 1, storage, stats, label_width);

    std::vector<std::string> ops = {"lookups", "cache_writes", "cache_updates", "cache_deletes", "read_active", "write_active"};
    drawStatTable(w + 2, 1, ops, stats, label_width);

    // Middle row: Hit Rates and Latency
    int y2 = row1_height;
    drawBox(0, y2, w, row2_height, "HIT RATES", ColorPair::Border3);
    drawBox(w, y2, _width - w, row2_height, "LATENCY (ms)", ColorPair::Border);

    std::vector<std::string> hits = {"ram_ratio", "fresh", "reval", "cold", "changed", "not", "no"};
    drawStatTable(2, y2 + 1, hits, stats, label_width);

    std::vector<std::string> latency = {"fresh_time", "reval_time", "cold_time", "changed_time", "not_time", "no_time"};
    drawStatTable(w + 2, y2 + 1, latency, stats, label_width);

    // Bottom row: DNS
    int y3 = y2 + row2_height;
    if (row3_height > 2) {
      drawBox(0, y3, _width, row3_height, "DNS", ColorPair::Border2);

      std::vector<std::string> dns = {"dns_lookups", "dns_hits", "dns_ratio", "dns_entry"};
      drawStatTable(2, y3 + 1, dns, stats, label_width);
    }
  }
}

void
Display::renderSSLPage(Stats &stats)
{
  // SSL page with comprehensive SSL/TLS metrics
  int w           = _width / 2;
  int label_width = (_width >= WIDTH_MEDIUM) ? LABEL_WIDTH_LG : LABEL_WIDTH_MD;
  int available   = _height - 1; // Leave room for status bar

  // Handshakes/Sessions: 5 stats -> 7 rows
  // Origin Errors/TLS: 5/4 stats -> 7 rows
  // Client/General Errors: remaining
  int row1_height = 7;
  int row2_height = 7;
  int row3_height = available - row1_height - row2_height;

  // Adjust for smaller terminals
  if (available < 20) {
    row1_height = 6;
    row2_height = 6;
    row3_height = available - row1_height - row2_height;
  }

  // Top row: Handshakes and Sessions
  drawBox(0, 0, w, row1_height, "HANDSHAKES", ColorPair::Border);
  drawBox(w, 0, _width - w, row1_height, "SESSIONS", ColorPair::Border2);

  std::vector<std::string> handshake = {"ssl_attempts_in", "ssl_success_in", "ssl_attempts_out", "ssl_success_out",
                                        "ssl_handshake_time"};
  drawStatTable(2, 1, handshake, stats, label_width);

  std::vector<std::string> session = {"ssl_session_hit", "ssl_session_miss", "ssl_sess_new", "ssl_sess_evict", "ssl_origin_reused"};
  drawStatTable(w + 2, 1, session, stats, label_width);

  // Middle row: Origin Errors and TLS Versions
  int y2 = row1_height;
  drawBox(0, y2, w, row2_height, "ORIGIN ERRORS", ColorPair::Border3);
  drawBox(w, y2, _width - w, row2_height, "TLS VERSIONS", ColorPair::Border);

  std::vector<std::string> origin_err = {"ssl_origin_bad_cert", "ssl_origin_expired", "ssl_origin_revoked", "ssl_origin_unknown_ca",
                                         "ssl_origin_verify_fail"};
  drawStatTable(2, y2 + 1, origin_err, stats, label_width);

  std::vector<std::string> tls_ver = {"tls_v10", "tls_v11", "tls_v12", "tls_v13"};
  drawStatTable(w + 2, y2 + 1, tls_ver, stats, label_width);

  // Bottom row: Client Errors and General Errors
  int y3 = y2 + row2_height;
  if (row3_height > 2) {
    drawBox(0, y3, w, row3_height, "CLIENT ERRORS", ColorPair::Border2);
    drawBox(w, y3, _width - w, row3_height, "GENERAL ERRORS", ColorPair::Border3);

    std::vector<std::string> client_err = {"ssl_client_bad_cert"};
    drawStatTable(2, y3 + 1, client_err, stats, label_width);

    std::vector<std::string> general_err = {"ssl_error_ssl", "ssl_error_syscall", "ssl_error_async"};
    drawStatTable(w + 2, y3 + 1, general_err, stats, label_width);
  }
}

void
Display::renderErrorsPage(Stats &stats)
{
  // Comprehensive error page with all error categories
  int w           = _width / 2;
  int label_width = (_width >= WIDTH_MEDIUM) ? LABEL_WIDTH_MD : LABEL_WIDTH_SM;
  int available   = _height - 1; // Leave room for status bar

  // Connection/Transaction: 3 stats -> 5 rows
  // Cache/Origin: 3 stats -> 5 rows
  // HTTP/2/HTTP: 4/6 stats -> remaining
  int row1_height = 5;
  int row2_height = 5;
  int row3_height = available - row1_height - row2_height;

  // Top row: Connection and Transaction errors
  drawBox(0, 0, w, row1_height, "CONNECTION", ColorPair::Border);
  drawBox(w, 0, _width - w, row1_height, "TRANSACTION", ColorPair::Border2);

  std::vector<std::string> conn = {"err_conn_fail", "err_client_abort", "err_client_read"};
  drawStatTable(2, 1, conn, stats, label_width);

  std::vector<std::string> tx = {"txn_aborts", "txn_possible_aborts", "txn_other_errors"};
  drawStatTable(w + 2, 1, tx, stats, label_width);

  // Middle row: Cache and Origin errors
  int y2 = row1_height;
  drawBox(0, y2, w, row2_height, "CACHE", ColorPair::Border3);
  drawBox(w, y2, _width - w, row2_height, "ORIGIN", ColorPair::Border);

  std::vector<std::string> cache_err = {"cache_read_errors", "cache_write_errors", "cache_lookup_fail"};
  drawStatTable(2, y2 + 1, cache_err, stats, label_width);

  std::vector<std::string> origin_err = {"conn_fail", "abort", "other_err"};
  drawStatTable(w + 2, y2 + 1, origin_err, stats, label_width);

  // Bottom row: HTTP/2 and HTTP response errors
  int y3 = y2 + row2_height;
  if (row3_height > 2) {
    drawBox(0, y3, w, row3_height, "HTTP/2", ColorPair::Border2);
    drawBox(w, y3, _width - w, row3_height, "HTTP", ColorPair::Border3);

    std::vector<std::string> h2_err = {"h2_stream_errors", "h2_conn_errors", "h2_session_die_error", "h2_session_die_high_error"};
    drawStatTable(2, y3 + 1, h2_err, stats, label_width);

    std::vector<std::string> http_err = {"400", "404", "4xx", "500", "502", "5xx"};
    drawStatTable(w + 2, y3 + 1, http_err, stats, 6);
  }
}

void
Display::renderPerformancePage(Stats &stats)
{
  // Performance page showing HTTP milestone timing data in chronological order
  // Milestones are cumulative nanoseconds, displayed as ms/s
  int label_width = (_width >= WIDTH_MEDIUM) ? LABEL_WIDTH_MD : LABEL_WIDTH_SM;
  int available   = _height - 1; // Leave room for status bar

  // All milestones in chronological order of when they occur during a request
  // clang-format off
  std::vector<std::string> milestones = {
    "ms_sm_start",            // 1. State machine starts
    "ms_ua_begin",            // 2. Client connection begins
    "ms_ua_first_read",       // 3. First read from client
    "ms_ua_read_header",      // 4. Client headers fully read
    "ms_cache_read_begin",    // 5. Start checking cache
    "ms_cache_read_end",      // 6. Done checking cache
    "ms_dns_begin",           // 7. DNS lookup starts (if cache miss)
    "ms_dns_end",             // 8. DNS lookup ends
    "ms_server_connect",      // 9. Start connecting to origin
    "ms_server_first_connect", // 10. First connection to origin
    "ms_server_connect_end",  // 11. Connection established
    "ms_server_begin_write",  // 12. Start writing to origin
    "ms_server_first_read",   // 13. First read from origin
    "ms_server_read_header",  // 14. Origin headers received
    "ms_cache_write_begin",   // 15. Start writing to cache
    "ms_cache_write_end",     // 16. Done writing to cache
    "ms_ua_begin_write",      // 17. Start writing to client
    "ms_server_close",        // 18. Origin connection closed
    "ms_ua_close",            // 19. Client connection closed
    "ms_sm_finish"            // 20. State machine finished
  };
  // clang-format on

  // For wider terminals, use two columns
  if (_width >= WIDTH_MEDIUM) {
    // Two-column layout
    int col_width     = _width / 2;
    int box_height    = available;
    int stats_per_col = static_cast<int>(milestones.size() + 1) / 2;

    drawBox(0, 0, col_width, box_height, "MILESTONES (ms/s)", ColorPair::Border);
    drawBox(col_width, 0, _width - col_width, box_height, "MILESTONES (cont)", ColorPair::Border);

    // Left column - first half of milestones
    int                      max_left = std::min(stats_per_col, box_height - 2);
    std::vector<std::string> left_stats(milestones.begin(), milestones.begin() + max_left);
    drawStatTable(2, 1, left_stats, stats, label_width);

    // Right column - second half of milestones
    int max_right = std::min(static_cast<int>(milestones.size()) - stats_per_col, box_height - 2);
    if (max_right > 0) {
      std::vector<std::string> right_stats(milestones.begin() + stats_per_col, milestones.begin() + stats_per_col + max_right);
      drawStatTable(col_width + 2, 1, right_stats, stats, label_width);
    }
  } else {
    // Single column for narrow terminals
    drawBox(0, 0, _width, available, "MILESTONES (ms/s)", ColorPair::Border);

    int max_stats = std::min(static_cast<int>(milestones.size()), available - 2);
    milestones.resize(max_stats);
    drawStatTable(2, 1, milestones, stats, label_width);
  }
}

void
Display::renderGraphsPage(Stats &stats)
{
  // Layout graphs based on terminal width
  // 80x24: Two 40-char boxes side by side, then 80-char multi-graph box
  // 120x40: Three 40-char boxes, then 120-char wide graphs
  // 160+: Four 40-char boxes

  // Helper lambda to format value with suffix
  auto formatValue = [](double value, const char *suffix = "") -> std::string {
    char buffer[32];
    if (value >= 1000000000.0) {
      snprintf(buffer, sizeof(buffer), "%.0fG%s", value / 1000000000.0, suffix);
    } else if (value >= 1000000.0) {
      snprintf(buffer, sizeof(buffer), "%.0fM%s", value / 1000000.0, suffix);
    } else if (value >= 1000.0) {
      snprintf(buffer, sizeof(buffer), "%.0fK%s", value / 1000.0, suffix);
    } else {
      snprintf(buffer, sizeof(buffer), "%.0f%s", value, suffix);
    }
    return buffer;
  };

  // Get current values
  double clientReq = 0, clientNet = 0, serverNet = 0, ramRatio = 0;
  double clientConn = 0, serverConn = 0, lookups = 0, cacheWrites = 0;
  stats.getStat("client_req", clientReq);
  stats.getStat("client_net", clientNet);
  stats.getStat("server_net", serverNet);
  stats.getStat("ram_ratio", ramRatio);
  stats.getStat("client_curr_conn", clientConn);
  stats.getStat("server_curr_conn", serverConn);
  stats.getStat("lookups", lookups);
  stats.getStat("cache_writes", cacheWrites);

  // Build graph data
  std::vector<std::tuple<std::string, std::vector<double>, std::string>> networkGraphs = {
    {"Net In",  stats.getHistory("client_net"), formatValue(clientNet * 8, " b/s")},
    {"Net Out", stats.getHistory("server_net"), formatValue(serverNet * 8, " b/s")},
  };

  std::vector<std::tuple<std::string, std::vector<double>, std::string>> cacheGraphs = {
    {"Hit Rate", stats.getHistory("ram_ratio", 100.0), formatValue(ramRatio, "%")},
    {"Lookups", stats.getHistory("lookups"), formatValue(lookups, "/s")},
    {"Writes", stats.getHistory("cache_writes"), formatValue(cacheWrites, "/s")},
  };

  std::vector<std::tuple<std::string, std::vector<double>, std::string>> connGraphs = {
    {"Client", stats.getHistory("client_curr_conn"), formatValue(clientConn)},
    {"Origin", stats.getHistory("server_curr_conn"), formatValue(serverConn)},
  };

  std::vector<std::tuple<std::string, std::vector<double>, std::string>> requestGraphs = {
    {"Requests", stats.getHistory("client_req"), formatValue(clientReq, "/s")},
  };

  if (_width >= WIDTH_LARGE) {
    // Wide terminal (160+): 4 columns of 40-char boxes
    int w = 40;

    drawMultiGraphBox(0, 0, w, networkGraphs, "NETWORK");
    drawMultiGraphBox(w, 0, w, cacheGraphs, "CACHE");
    drawMultiGraphBox(w * 2, 0, w, connGraphs, "CONNECTIONS");
    drawMultiGraphBox(w * 3, 0, _width - w * 3, requestGraphs, "REQUESTS");

    // Second row: Wide bandwidth history if height allows
    if (_height > 10) {
      std::vector<std::tuple<std::string, std::vector<double>, std::string>> allGraphs = {
        {"Client In", stats.getHistory("client_net"), formatValue(clientNet * 8, " b/s")},
        {"Origin Out", stats.getHistory("server_net"), formatValue(serverNet * 8, " b/s")},
        {"Requests", stats.getHistory("client_req"), formatValue(clientReq, "/s")},
        {"Hit Rate", stats.getHistory("ram_ratio", 100.0), formatValue(ramRatio, "%")},
      };
      drawMultiGraphBox(0, 6, _width, allGraphs, "TRAFFIC OVERVIEW");
    }

  } else if (_width >= WIDTH_MEDIUM) {
    // Medium terminal (120): 3 columns of 40-char boxes
    int w = 40;

    drawMultiGraphBox(0, 0, w, networkGraphs, "NETWORK");
    drawMultiGraphBox(w, 0, w, cacheGraphs, "CACHE");
    drawMultiGraphBox(w * 2, 0, _width - w * 2, connGraphs, "CONNECTIONS");

    // Second row: requests graph spanning full width
    if (_height > 8) {
      std::vector<std::tuple<std::string, std::vector<double>, std::string>> overviewGraphs = {
        {"Requests", stats.getHistory("client_req"), formatValue(clientReq, "/s")},
        {"Hit Rate", stats.getHistory("ram_ratio", 100.0), formatValue(ramRatio, "%")},
        {"Client", stats.getHistory("client_curr_conn"), formatValue(clientConn)},
      };
      drawMultiGraphBox(0, 6, _width, overviewGraphs, "OVERVIEW");
    }

  } else {
    // Classic terminal (80): 2 columns of 40-char boxes + 80-char overview
    int w = _width / 2;

    // Combine network graphs for smaller box
    std::vector<std::tuple<std::string, std::vector<double>, std::string>> leftGraphs = {
      {"Net In",  stats.getHistory("client_net"), formatValue(clientNet * 8, " b/s")},
      {"Net Out", stats.getHistory("server_net"), formatValue(serverNet * 8, " b/s")},
    };

    std::vector<std::tuple<std::string, std::vector<double>, std::string>> rightGraphs = {
      {"Hit Rate", stats.getHistory("ram_ratio", 100.0), formatValue(ramRatio, "%")},
      {"Requests", stats.getHistory("client_req"), formatValue(clientReq, "/s")},
    };

    drawMultiGraphBox(0, 0, w, leftGraphs, "NETWORK");
    drawMultiGraphBox(w, 0, _width - w, rightGraphs, "CACHE");

    // Second row: full-width overview
    if (_height > 8) {
      std::vector<std::tuple<std::string, std::vector<double>, std::string>> overviewGraphs = {
        {"Bandwidth", stats.getHistory("client_net"), formatValue(clientNet * 8, " b/s")},
        {"Hit Rate", stats.getHistory("ram_ratio", 100.0), formatValue(ramRatio, "%")},
        {"Requests", stats.getHistory("client_req"), formatValue(clientReq, "/s")},
        {"Connections", stats.getHistory("client_curr_conn"), formatValue(clientConn)},
      };
      drawMultiGraphBox(0, 5, _width, overviewGraphs, "TRAFFIC OVERVIEW");
    }
  }
}

void
Display::renderHelpPage(const std::string &host, const std::string &version)
{
  int box_width = std::min(80, _width - 4);
  int box_x     = (_width - box_width) / 2;

  drawBox(box_x, 0, box_width, _height - 2, "HELP", ColorPair::Border);

  int y    = 2;
  int x    = box_x + 2;
  int col2 = box_x + box_width / 2;

  moveTo(y++, x);
  setBold();
  setColor(ColorPair::Cyan);
  printf("TRAFFIC_TOP - ATS Real-time Monitor");
  resetColor();
  y++;

  moveTo(y++, x);
  setBold();
  printf("Navigation");
  resetColor();

  moveTo(y++, x);
  printf("  1-8          Switch to page N");
  moveTo(y++, x);
  printf("  Left/m       Previous page");
  moveTo(y++, x);
  printf("  Right/r      Next page");
  moveTo(y++, x);
  printf("  h or ?       Show this help");
  moveTo(y++, x);
  printf("  a            Toggle absolute/rate mode");
  moveTo(y++, x);
  printf("  b/ESC        Back (from help)");
  moveTo(y++, x);
  printf("  q            Quit");
  y++;

  moveTo(y++, x);
  setBold();
  printf("Pages");
  resetColor();

  moveTo(y++, x);
  printf("  1    Overview     Cache, requests, connections");
  moveTo(y++, x);
  printf("  2    Responses    HTTP response code breakdown");
  moveTo(y++, x);
  printf("  3    Connections  HTTP/1.x vs HTTP/2 details");
  moveTo(y++, x);
  printf("  4    Cache        Storage, operations, hit rates");
  moveTo(y++, x);
  printf("  5    SSL/TLS      Handshake and session stats");
  moveTo(y++, x);
  printf("  6    Errors       Connection and HTTP errors");
  moveTo(y++, x);
  printf("  7/p  Performance  HTTP milestones timing (ms/s)");
  moveTo(y++, x);
  printf("  8/g  Graphs       Real-time graphs");
  y++;

  // Right column - Cache definitions
  int y2 = 4;
  moveTo(y2++, col2);
  setBold();
  printf("Cache States");
  resetColor();

  moveTo(y2, col2);
  setColor(ColorPair::Green);
  printf("  Fresh");
  resetColor();
  moveTo(y2++, col2 + 12);
  printf("Served from cache");

  moveTo(y2, col2);
  setColor(ColorPair::Cyan);
  printf("  Reval");
  resetColor();
  moveTo(y2++, col2 + 12);
  printf("Revalidated with origin");

  moveTo(y2, col2);
  setColor(ColorPair::Yellow);
  printf("  Cold");
  resetColor();
  moveTo(y2++, col2 + 12);
  printf("Cache miss");

  moveTo(y2, col2);
  setColor(ColorPair::Yellow);
  printf("  Changed");
  resetColor();
  moveTo(y2++, col2 + 12);
  printf("Cache entry updated");

  // Connection info
  y2 += 2;
  moveTo(y2++, col2);
  setBold();
  printf("Connection");
  resetColor();

  moveTo(y2++, col2);
  printf("  Host: %s", host.c_str());
  moveTo(y2++, col2);
  printf("  ATS:  %s", version.empty() ? "unknown" : version.c_str());

  // Footer
  moveTo(_height - 3, x);
  setColor(ColorPair::Cyan);
  printf("Press any key to return...");
  resetColor();
}

} // namespace traffic_top
