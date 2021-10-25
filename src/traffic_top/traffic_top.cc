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

#include "tscore/ink_config.h"
#include <map>
#include <list>
#include <string>
#include <cstring>
#include <iostream>
#include <cassert>
#include <cstdlib>
#include <unistd.h>
#include <getopt.h>

// At least on solaris, the default ncurses defines macros such as
// clear() that break stdlibc++.
#define NOMACROS 1
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

#include "stats.h"

#include "tscore/I_Layout.h"
#include "tscore/ink_args.h"
#include "records/I_RecProcess.h"
#include "RecordsConfig.h"
#include "tscore/runroot.h"

using namespace std;

#if HAS_CURL
char curl_error[CURL_ERROR_SIZE];
#endif
string response;

namespace colorPair
{
const short red    = 1;
const short yellow = 2;
const short green  = 3;
const short blue   = 4;
//  const short black = 5;
const short grey   = 6;
const short cyan   = 7;
const short border = 8;
}; // namespace colorPair

//----------------------------------------------------------------------------
static void
prettyPrint(const int x, const int y, const double number, const int type)
{
  char buffer[32];
  char exp         = ' ';
  double my_number = number;
  short color;
  if (number > 1000000000000LL) {
    my_number = number / 1000000000000LL;
    exp       = 'T';
    color     = colorPair::red;
  } else if (number > 1000000000) {
    my_number = number / 1000000000;
    exp       = 'G';
    color     = colorPair::red;
  } else if (number > 1000000) {
    my_number = number / 1000000;
    exp       = 'M';
    color     = colorPair::yellow;
  } else if (number > 1000) {
    my_number = number / 1000;
    exp       = 'K';
    color     = colorPair::cyan;
  } else if (my_number <= .09) {
    color = colorPair::grey;
  } else {
    color = colorPair::green;
  }

  if (type == 4 || type == 5) {
    if (number > 90) {
      color = colorPair::red;
    } else if (number > 80) {
      color = colorPair::yellow;
    } else if (number > 50) {
      color = colorPair::blue;
    } else if (my_number <= .09) {
      color = colorPair::grey;
    } else {
      color = colorPair::green;
    }
    snprintf(buffer, sizeof(buffer), "%6.1f%%%%", my_number);
  } else {
    snprintf(buffer, sizeof(buffer), "%6.1f%c", my_number, exp);
  }
  attron(COLOR_PAIR(color));
  attron(A_BOLD);
  mvprintw(y, x, "%s", buffer);
  attroff(COLOR_PAIR(color));
  attroff(A_BOLD);
}

//----------------------------------------------------------------------------
static void
makeTable(const int x, const int y, const list<string> &items, Stats &stats)
{
  int my_y = y;

  for (const auto &item : items) {
    string prettyName;
    double value = 0;
    int type;

    stats.getStat(item, value, prettyName, type);
    mvprintw(my_y, x, "%s", prettyName.c_str());
    prettyPrint(x + 10, my_y++, value, type);
  }
}

//----------------------------------------------------------------------------
size_t
write_data(void *ptr, size_t size, size_t nmemb, void * /* stream */)
{
  response.append(static_cast<char *>(ptr), size * nmemb);
  return size * nmemb;
}

//----------------------------------------------------------------------------
static void
response_code_page(Stats &stats)
{
  attron(COLOR_PAIR(colorPair::border));
  attron(A_BOLD);
  mvprintw(0, 0, "                              RESPONSE CODES                                   ");
  attroff(COLOR_PAIR(colorPair::border));
  attroff(A_BOLD);

  list<string> response1;
  response1.push_back("100");
  response1.push_back("101");
  response1.push_back("1xx");
  response1.push_back("200");
  response1.push_back("201");
  response1.push_back("202");
  response1.push_back("203");
  response1.push_back("204");
  response1.push_back("205");
  response1.push_back("206");
  response1.push_back("2xx");
  response1.push_back("300");
  response1.push_back("301");
  response1.push_back("302");
  response1.push_back("303");
  response1.push_back("304");
  response1.push_back("305");
  response1.push_back("307");
  response1.push_back("3xx");
  makeTable(0, 1, response1, stats);

  list<string> response2;
  response2.push_back("400");
  response2.push_back("401");
  response2.push_back("402");
  response2.push_back("403");
  response2.push_back("404");
  response2.push_back("405");
  response2.push_back("406");
  response2.push_back("407");
  response2.push_back("408");
  response2.push_back("409");
  response2.push_back("410");
  response2.push_back("411");
  response2.push_back("412");
  response2.push_back("413");
  response2.push_back("414");
  response2.push_back("415");
  response2.push_back("416");
  response2.push_back("4xx");
  makeTable(21, 1, response2, stats);

  list<string> response3;
  response3.push_back("500");
  response3.push_back("501");
  response3.push_back("502");
  response3.push_back("503");
  response3.push_back("504");
  response3.push_back("505");
  response3.push_back("5xx");
  makeTable(42, 1, response3, stats);
}

//----------------------------------------------------------------------------
static void
help(const string &host, const string &version)
{
  timeout(1000);

  while (true) {
    clear();
    time_t now = time(nullptr);
    struct tm nowtm;
    char timeBuf[32];
    localtime_r(&now, &nowtm);
    strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &nowtm);

    // clear();
    attron(A_BOLD);
    mvprintw(0, 0, "Overview:");
    attroff(A_BOLD);
    mvprintw(
      1, 0,
      "traffic_top is a top like program for Apache Traffic Server (ATS). "
      "There is a lot of statistical information gathered by ATS. "
      "This program tries to show some of the more important stats and gives a good overview of what the proxy server is doing. "
      "Hopefully this can be used as a tool for diagnosing the proxy server if there are problems.");

    attron(A_BOLD);
    mvprintw(7, 0, "Definitions:");
    attroff(A_BOLD);
    mvprintw(8, 0, "Fresh      => Requests that were served by fresh entries in cache");
    mvprintw(9, 0, "Revalidate => Requests that contacted the origin to verify if still valid");
    mvprintw(10, 0, "Cold       => Requests that were not in cache at all");
    mvprintw(11, 0, "Changed    => Requests that required entries in cache to be updated");
    mvprintw(12, 0, "Changed    => Requests that can't be cached for some reason");
    mvprintw(12, 0, "No Cache   => Requests that the client sent Cache-Control: no-cache header");

    attron(COLOR_PAIR(colorPair::border));
    attron(A_BOLD);
    mvprintw(23, 0, "%s - %.12s - %.12s      (b)ack                            ", timeBuf, version.c_str(), host.c_str());
    attroff(COLOR_PAIR(colorPair::border));
    attroff(A_BOLD);
    refresh();
    int x = getch();
    if (x == 'b') {
      break;
    }
  }
}

//----------------------------------------------------------------------------
void
main_stats_page(Stats &stats)
{
  attron(COLOR_PAIR(colorPair::border));
  attron(A_BOLD);
  mvprintw(0, 0, "         CACHE INFORMATION             ");
  mvprintw(0, 40, "       CLIENT REQUEST & RESPONSE        ");
  mvprintw(16, 0, "             CLIENT                    ");
  mvprintw(16, 40, "           ORIGIN SERVER                ");

  for (int i = 0; i <= 22; ++i) {
    mvprintw(i, 39, " ");
  }
  attroff(COLOR_PAIR(colorPair::border));
  attroff(A_BOLD);

  list<string> cache1;
  cache1.push_back("disk_used");
  cache1.push_back("disk_total");
  cache1.push_back("ram_used");
  cache1.push_back("ram_total");
  cache1.push_back("lookups");
  cache1.push_back("cache_writes");
  cache1.push_back("cache_updates");
  cache1.push_back("cache_deletes");
  cache1.push_back("read_active");
  cache1.push_back("write_active");
  cache1.push_back("update_active");
  cache1.push_back("entries");
  cache1.push_back("avg_size");
  cache1.push_back("dns_lookups");
  cache1.push_back("dns_hits");
  makeTable(0, 1, cache1, stats);

  list<string> cache2;
  cache2.push_back("ram_ratio");
  cache2.push_back("fresh");
  cache2.push_back("reval");
  cache2.push_back("cold");
  cache2.push_back("changed");
  cache2.push_back("not");
  cache2.push_back("no");
  cache2.push_back("fresh_time");
  cache2.push_back("reval_time");
  cache2.push_back("cold_time");
  cache2.push_back("changed_time");
  cache2.push_back("not_time");
  cache2.push_back("no_time");
  cache2.push_back("dns_ratio");
  cache2.push_back("dns_entry");
  makeTable(21, 1, cache2, stats);

  list<string> response1;
  response1.push_back("get");
  response1.push_back("head");
  response1.push_back("post");
  response1.push_back("2xx");
  response1.push_back("3xx");
  response1.push_back("4xx");
  response1.push_back("5xx");
  response1.push_back("conn_fail");
  response1.push_back("other_err");
  response1.push_back("abort");
  makeTable(41, 1, response1, stats);

  list<string> response2;
  response2.push_back("200");
  response2.push_back("206");
  response2.push_back("301");
  response2.push_back("302");
  response2.push_back("304");
  response2.push_back("404");
  response2.push_back("502");
  response2.push_back("s_100");
  response2.push_back("s_1k");
  response2.push_back("s_3k");
  response2.push_back("s_5k");
  response2.push_back("s_10k");
  response2.push_back("s_1m");
  response2.push_back("s_>1m");
  makeTable(62, 1, response2, stats);

  list<string> client1;
  client1.push_back("client_req");
  client1.push_back("client_req_conn");
  client1.push_back("client_conn");
  client1.push_back("client_curr_conn");
  client1.push_back("client_actv_conn");
  client1.push_back("client_dyn_ka");
  makeTable(0, 17, client1, stats);

  list<string> client2;
  client2.push_back("client_head");
  client2.push_back("client_body");
  client2.push_back("client_avg_size");
  client2.push_back("client_net");
  client2.push_back("client_req_time");
  makeTable(21, 17, client2, stats);

  list<string> server1;
  server1.push_back("server_req");
  server1.push_back("server_req_conn");
  server1.push_back("server_conn");
  server1.push_back("server_curr_conn");
  makeTable(41, 17, server1, stats);

  list<string> server2;
  server2.push_back("server_head");
  server2.push_back("server_body");
  server2.push_back("server_avg_size");
  server2.push_back("server_net");
  makeTable(62, 17, server2, stats);
}

//----------------------------------------------------------------------------
int
main(int argc, const char **argv)
{
#if HAS_CURL
  static const char USAGE[] = "Usage: traffic_top [-s seconds] [URL|hostname|hostname:port]";
#else
  static const char USAGE[] = "Usage: traffic_top [-s seconds]";
#endif

  int sleep_time = 6; // In seconds
  bool absolute  = false;
  string url;

  AppVersionInfo version;
  version.setup(PACKAGE_NAME, "traffic_top", PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");

  const ArgumentDescription argument_descriptions[] = {
    {"sleep", 's', "Sets the delay between updates (in seconds)", "I", &sleep_time, nullptr, nullptr},
    HELP_ARGUMENT_DESCRIPTION(),
    VERSION_ARGUMENT_DESCRIPTION(),
    RUNROOT_ARGUMENT_DESCRIPTION(),
  };

  process_args(&version, argument_descriptions, countof(argument_descriptions), argv, USAGE);

  runroot_handler(argv);
  Layout::create();
  RecProcessInit(RECM_STAND_ALONE, nullptr /* diags */);
  LibRecordsConfigInit();

  switch (n_file_arguments) {
  case 0: {
    ats_scoped_str rundir(RecConfigReadRuntimeDir());

    TSMgmtError err = TSInit(rundir, static_cast<TSInitOptionT>(TS_MGMT_OPT_NO_EVENTS | TS_MGMT_OPT_NO_SOCK_TESTS));
    if (err != TS_ERR_OKAY) {
      fprintf(stderr, "Error: connecting to local manager: %s\n", TSGetErrorMessage(err));
      exit(1);
    }
    break;
  }

  case 1:
#if HAS_CURL
    url = file_arguments[0];
#else
    usage(argument_descriptions, countof(argument_descriptions), USAGE);
#endif
    break;

  default:
    usage(argument_descriptions, countof(argument_descriptions), USAGE);
  }

  Stats stats(url);
  stats.getStats();
  const string &host = stats.getHost();

  initscr();
  curs_set(0);

  start_color(); /* Start color functionality	*/

  init_pair(colorPair::red, COLOR_RED, COLOR_BLACK);
  init_pair(colorPair::yellow, COLOR_YELLOW, COLOR_BLACK);
  init_pair(colorPair::grey, COLOR_BLACK, COLOR_BLACK);
  init_pair(colorPair::green, COLOR_GREEN, COLOR_BLACK);
  init_pair(colorPair::blue, COLOR_BLUE, COLOR_BLACK);
  init_pair(colorPair::cyan, COLOR_CYAN, COLOR_BLACK);
  init_pair(colorPair::border, COLOR_WHITE, COLOR_BLUE);
  //  mvchgat(0, 0, -1, A_BLINK, 1, nullptr);

  enum Page {
    MAIN_PAGE,
    RESPONSE_PAGE,
  };
  Page page       = MAIN_PAGE;
  string page_alt = "(r)esponse";

  while (true) {
    attron(COLOR_PAIR(colorPair::border));
    attron(A_BOLD);

    string version;
    time_t now = time(nullptr);
    struct tm nowtm;
    char timeBuf[32];
    localtime_r(&now, &nowtm);
    strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &nowtm);
    stats.getStat("version", version);

    mvprintw(23, 0, "%-20.20s   %30s (q)uit (h)elp (%c)bsolute  ", host.c_str(), page_alt.c_str(), absolute ? 'A' : 'a');
    attroff(COLOR_PAIR(colorPair::border));
    attroff(A_BOLD);

    if (page == MAIN_PAGE) {
      main_stats_page(stats);
    } else if (page == RESPONSE_PAGE) {
      response_code_page(stats);
    }

    curs_set(0);
    refresh();
    timeout(sleep_time * 1000);

    int x = getch();
    switch (x) {
    case 'h':
      help(host, version);
      break;
    case 'q':
      goto quit;
    case 'm':
      page     = MAIN_PAGE;
      page_alt = "(r)esponse";
      break;
    case 'r':
      page     = RESPONSE_PAGE;
      page_alt = "(m)ain";
      break;
    case 'a':
      absolute = stats.toggleAbsolute();
    }
    stats.getStats();
    clear();
  }

quit:
  endwin();

  return 0;
}
