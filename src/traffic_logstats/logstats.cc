/** @file

  This is a command line tool that reads an ATS log in the squid
  binary log format, and produces meaningful metrics per property.

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

#include "tscore/ink_platform.h"
#include "tscore/ink_file.h"
#include "tscore/I_Layout.h"
#include "tscore/I_Version.h"
#include "tscore/HashFNV.h"
#include "tscore/ink_args.h"
#include "tscore/MatcherUtils.h"
#include "tscore/runroot.h"

// Includes and namespaces etc.
#include "LogStandalone.cc"

#include "LogObject.h"
#include "hdrs/HTTP.h"

#include <sys/utsname.h>
#if defined(solaris)
#include <sys/types.h>
#include <unistd.h>
#endif

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include <algorithm>
#include <vector>
#include <list>
#include <cmath>
#include <functional>
#include <fcntl.h>
#include <unordered_map>
#include <unordered_set>
#include <string_view>

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

using namespace std;

// Constants, please update the VERSION number when you make a new build!!!
#define PROGRAM_NAME "traffic_logstats"

const int MAX_LOGBUFFER_SIZE = 65536;
const int DEFAULT_LINE_LEN   = 78;
const double LOG10_1024      = 3.0102999566398116;
const int MAX_ORIG_STRING    = 4096;

// Optimizations for "strcmp()", treat some fixed length (3 or 4 bytes) strings
// as integers.
const int GET_AS_INT  = 5522759;
const int PUT_AS_INT  = 5526864;
const int HEAD_AS_INT = 1145128264;
const int POST_AS_INT = 1414745936;

const int TEXT_AS_INT = 1954047348;

const int JPEG_AS_INT = 1734701162;
const int JPG_AS_INT  = 6778986;
const int GIF_AS_INT  = 6711655;
const int PNG_AS_INT  = 6778480;
const int BMP_AS_INT  = 7368034;
const int CSS_AS_INT  = 7566179;
const int XML_AS_INT  = 7105912;
const int HTML_AS_INT = 1819112552;
const int ZIP_AS_INT  = 7367034;

const int JAVA_AS_INT = 1635148138; // For "javascript"
const int X_JA_AS_INT = 1634348408; // For "x-javascript"
const int RSSp_AS_INT = 728986482;  // For "RSS+"
const int PLAI_AS_INT = 1767992432; // For "plain"
const int IMAG_AS_INT = 1734438249; // For "image"
const int HTTP_AS_INT = 1886680168; // For "http" followed by "s://" or "://"

// Store our "state" (position in log file etc.)
struct LastState {
  off_t offset;
  ino_t st_ino;
};
static LastState last_state;

// Store the collected counters and stats, per Origin Server, URL or total
struct StatsCounter {
  int64_t count;
  int64_t bytes;
};

struct ElapsedStats {
  int min;
  int max;
  float avg;
  float stddev;
};

struct OriginStats {
  const char *server;
  StatsCounter total;

  struct {
    struct {
      ElapsedStats hit;
      ElapsedStats hit_ram;
      ElapsedStats ims;
      ElapsedStats refresh;
      ElapsedStats other;
      ElapsedStats total;
    } hits;
    struct {
      ElapsedStats miss;
      ElapsedStats ims;
      ElapsedStats refresh;
      ElapsedStats other;
      ElapsedStats total;
    } misses;
  } elapsed;

  struct {
    struct {
      StatsCounter hit;
      StatsCounter hit_ram;
      StatsCounter ims;
      StatsCounter refresh;
      StatsCounter other;
      StatsCounter total;
    } hits;
    struct {
      StatsCounter miss;
      StatsCounter ims;
      StatsCounter refresh;
      StatsCounter other;
      StatsCounter total;
    } misses;
    struct {
      StatsCounter client_abort;
      StatsCounter client_read_error;
      StatsCounter connect_fail;
      StatsCounter invalid_req;
      StatsCounter unknown;
      StatsCounter other;
      StatsCounter total;
    } errors;
    StatsCounter other;
  } results;

  struct {
    StatsCounter c_000; // Bad
    StatsCounter c_100;
    StatsCounter c_200;
    StatsCounter c_201;
    StatsCounter c_202;
    StatsCounter c_203;
    StatsCounter c_204;
    StatsCounter c_205;
    StatsCounter c_206;
    StatsCounter c_2xx;
    StatsCounter c_300;
    StatsCounter c_301;
    StatsCounter c_302;
    StatsCounter c_303;
    StatsCounter c_304;
    StatsCounter c_305;
    StatsCounter c_307;
    StatsCounter c_3xx;
    StatsCounter c_400;
    StatsCounter c_401;
    StatsCounter c_402;
    StatsCounter c_403;
    StatsCounter c_404;
    StatsCounter c_405;
    StatsCounter c_406;
    StatsCounter c_407;
    StatsCounter c_408;
    StatsCounter c_409;
    StatsCounter c_410;
    StatsCounter c_411;
    StatsCounter c_412;
    StatsCounter c_413;
    StatsCounter c_414;
    StatsCounter c_415;
    StatsCounter c_416;
    StatsCounter c_417;
    StatsCounter c_4xx;
    StatsCounter c_500;
    StatsCounter c_501;
    StatsCounter c_502;
    StatsCounter c_503;
    StatsCounter c_504;
    StatsCounter c_505;
    StatsCounter c_5xx;
  } codes;

  struct {
    StatsCounter direct;
    StatsCounter none;
    StatsCounter sibling;
    StatsCounter parent;
    StatsCounter empty;
    StatsCounter invalid;
    StatsCounter other;
  } hierarchies;

  struct {
    StatsCounter http;
    StatsCounter https;
    StatsCounter none;
    StatsCounter other;
  } schemes;

  struct {
    StatsCounter ipv4;
    StatsCounter ipv6;
  } protocols;

  struct {
    StatsCounter options;
    StatsCounter get;
    StatsCounter head;
    StatsCounter post;
    StatsCounter put;
    StatsCounter del;
    StatsCounter trace;
    StatsCounter connect;
    StatsCounter purge;
    StatsCounter none;
    StatsCounter other;
  } methods;

  struct {
    struct {
      StatsCounter plain;
      StatsCounter xml;
      StatsCounter html;
      StatsCounter css;
      StatsCounter javascript;
      StatsCounter other;
      StatsCounter total;
    } text;
    struct {
      StatsCounter jpeg;
      StatsCounter gif;
      StatsCounter png;
      StatsCounter bmp;
      StatsCounter other;
      StatsCounter total;
    } image;
    struct {
      StatsCounter shockwave_flash;
      StatsCounter quicktime;
      StatsCounter javascript;
      StatsCounter zip;
      StatsCounter other;
      StatsCounter rss_xml;
      StatsCounter rss_atom;
      StatsCounter rss_other;
      StatsCounter total;
    } application;
    struct {
      StatsCounter wav;
      StatsCounter mpeg;
      StatsCounter other;
      StatsCounter total;
    } audio;
    StatsCounter none;
    StatsCounter other;
  } content;
};

struct UrlStats {
  bool
  operator<(const UrlStats &rhs) const
  {
    return req.count > rhs.req.count;
  } // Reverse order

  const char *url;
  StatsCounter req;
  ElapsedStats time;
  int64_t c_000;
  int64_t c_2xx;
  int64_t c_3xx;
  int64_t c_4xx;
  int64_t c_5xx;
  int64_t hits;
  int64_t misses;
  int64_t errors;
};

///////////////////////////////////////////////////////////////////////////////
// Equal operator for char* (for the hash_map)
struct eqstr {
  inline bool
  operator()(const char *s1, const char *s2) const
  {
    return 0 == strcmp(s1, s2);
  }
};

struct hash_fnv32 {
  inline uint32_t
  operator()(const char *s) const
  {
    ATSHash32FNV1a fnv;

    if (s) {
      fnv.update(s, strlen(s));
    }

    fnv.final();
    return fnv.get();
  }
};

typedef std::list<UrlStats> LruStack;
typedef std::unordered_map<const char *, OriginStats *, hash_fnv32, eqstr> OriginStorage;
typedef std::unordered_set<const char *, hash_fnv32, eqstr> OriginSet;
typedef std::unordered_map<const char *, LruStack::iterator, hash_fnv32, eqstr> LruHash;

// Resize a hash-based container.
template <class T, class N>
void
rehash(T &container, N size)
{
  container.rehash(size);
}

// LRU class for the URL data
void update_elapsed(ElapsedStats &stat, const int elapsed, const StatsCounter &counter);

class UrlLru
{
public:
  UrlLru(int size = 1000000, int show_urls = 0) : _size(size)
  {
    _show_urls = size > 0 ? (show_urls >= size ? size - 1 : show_urls) : show_urls;
    _init();
    _reset(false);
    _cur = _stack.begin();
  }

  void
  resize(int size = 0)
  {
    if (0 != size) {
      _size = size;
    }

    _init();
    _reset(true);
    _cur = _stack.begin();
  }

  void
  dump(int as_object = 0)
  {
    int show = _stack.size();

    if (_show_urls > 0 && _show_urls < show) {
      show = _show_urls;
    }

    _stack.sort();
    for (LruStack::iterator u = _stack.begin(); nullptr != u->url && --show >= 0; ++u) {
      _dump_url(u, as_object);
    }
    if (as_object) {
      std::cout << "  \"_timestamp\" : \"" << static_cast<int>(ink_time_wall_seconds()) << "\"" << std::endl;
    } else {
      std::cout << "  { \"_timestamp\" : \"" << static_cast<int>(ink_time_wall_seconds()) << "\" }" << std::endl;
    }
  }

  void
  add_stat(const char *url, int64_t bytes, int time, int result, int http_code, int as_object = 0)
  {
    LruHash::iterator h = _hash.find(url);

    if (h != _hash.end()) {
      LruStack::iterator &l = h->second;

      ++(l->req.count);
      l->req.bytes += bytes;

      if ((http_code >= 600) || (http_code < 200)) {
        ++(l->c_000);
      } else if (http_code >= 500) {
        ++(l->c_5xx);
      } else if (http_code >= 400) {
        ++(l->c_4xx);
      } else if (http_code >= 300) {
        ++(l->c_3xx);
      } else { // http_code >= 200
        ++(l->c_2xx);
      }

      switch (result) {
      case SQUID_LOG_TCP_HIT:
      case SQUID_LOG_TCP_IMS_HIT:
      case SQUID_LOG_TCP_REFRESH_HIT:
      case SQUID_LOG_TCP_DISK_HIT:
      case SQUID_LOG_TCP_MEM_HIT:
      case SQUID_LOG_TCP_REF_FAIL_HIT:
      case SQUID_LOG_UDP_HIT:
      case SQUID_LOG_UDP_WEAK_HIT:
      case SQUID_LOG_UDP_HIT_OBJ:
        ++(l->hits);
        break;
      case SQUID_LOG_TCP_MISS:
      case SQUID_LOG_TCP_IMS_MISS:
      case SQUID_LOG_TCP_REFRESH_MISS:
      case SQUID_LOG_TCP_EXPIRED_MISS:
      case SQUID_LOG_TCP_WEBFETCH_MISS:
      case SQUID_LOG_UDP_MISS:
        ++(l->misses);
        break;
      case SQUID_LOG_ERR_CLIENT_ABORT:
      case SQUID_LOG_ERR_CLIENT_READ_ERROR:
      case SQUID_LOG_ERR_CONNECT_FAIL:
      case SQUID_LOG_ERR_INVALID_REQ:
      case SQUID_LOG_ERR_UNKNOWN:
      case SQUID_LOG_ERR_READ_TIMEOUT:
        ++(l->errors);
        break;
      }

      update_elapsed(l->time, time, l->req);
      // Move this entry to the top of the stack (hence, LRU)
      if (_size > 0) {
        _stack.splice(_stack.begin(), _stack, l);
      }
    } else {                                  // "new" URL
      const char *u        = ats_strdup(url); // We own it.
      LruStack::iterator l = _stack.end();

      if (_size > 0) {
        if (_cur == l) { // LRU is full, take the last one
          --l;
          h = _hash.find(l->url);
          if (h != _hash.end()) {
            _hash.erase(h);
          }
          if (0 == _show_urls) {
            _dump_url(l, as_object);
          }
        } else {
          l = _cur++;
        }
        ats_free(const_cast<char *>(l->url)); // We no longer own this string.
      } else {
        l = _stack.insert(l, UrlStats()); // This seems faster than having a static "template" ...
      }

      // Setup this URL stat
      l->url       = u;
      l->req.bytes = bytes;
      l->req.count = 1;

      if ((http_code >= 600) || (http_code < 200)) {
        l->c_000 = 1;
      } else if (http_code >= 500) {
        l->c_5xx = 1;
      } else if (http_code >= 400) {
        l->c_4xx = 1;
      } else if (http_code >= 300) {
        l->c_3xx = 1;
      } else { // http_code >= 200
        l->c_2xx = 1;
      }

      switch (result) {
      case SQUID_LOG_TCP_HIT:
      case SQUID_LOG_TCP_IMS_HIT:
      case SQUID_LOG_TCP_REFRESH_HIT:
      case SQUID_LOG_TCP_DISK_HIT:
      case SQUID_LOG_TCP_MEM_HIT:
      case SQUID_LOG_TCP_REF_FAIL_HIT:
      case SQUID_LOG_UDP_HIT:
      case SQUID_LOG_UDP_WEAK_HIT:
      case SQUID_LOG_UDP_HIT_OBJ:
        l->hits = 1;
        break;
      case SQUID_LOG_TCP_MISS:
      case SQUID_LOG_TCP_IMS_MISS:
      case SQUID_LOG_TCP_REFRESH_MISS:
      case SQUID_LOG_TCP_EXPIRED_MISS:
      case SQUID_LOG_TCP_WEBFETCH_MISS:
      case SQUID_LOG_UDP_MISS:
        l->misses = 1;
        break;
      case SQUID_LOG_ERR_CLIENT_ABORT:
      case SQUID_LOG_ERR_CLIENT_READ_ERROR:
      case SQUID_LOG_ERR_CONNECT_FAIL:
      case SQUID_LOG_ERR_INVALID_REQ:
      case SQUID_LOG_ERR_UNKNOWN:
      case SQUID_LOG_ERR_READ_TIMEOUT:
        l->errors = 1;
        break;
      }

      l->time.min = -1;
      l->time.max = -1;
      update_elapsed(l->time, time, l->req);
      _hash[u] = l;

      // We running a real LRU or not?
      if (_size > 0) {
        _stack.splice(_stack.begin(), _stack, l); // Move this to the top of the stack
      }
    }
  }

private:
  void
  _init()
  {
    if (_size > 0) {
      _stack.resize(_size);
      rehash(_hash, _size);
    }
  }

  void
  _reset(bool free = false)
  {
    for (LruStack::iterator l = _stack.begin(); l != _stack.end(); ++l) {
      if (free && l->url) {
        ats_free(const_cast<char *>(l->url));
      }
      memset(&(*l), 0, sizeof(UrlStats));
    }
  }

  void
  _dump_url(LruStack::iterator &u, int as_object)
  {
    if (as_object) {
      std::cout << "  \"" << u->url << "\" : { ";
    } else {
      std::cout << "  { \"" << u->url << "\" : { ";
      // Requests
    }
    std::cout << "\"req\" : { \"total\" : \"" << u->req.count << "\", \"hits\" : \"" << u->hits << "\", \"misses\" : \""
              << u->misses << "\", \"errors\" : \"" << u->errors << "\", \"000\" : \"" << u->c_000 << "\", \"2xx\" : \"" << u->c_2xx
              << "\", \"3xx\" : \"" << u->c_3xx << "\", \"4xx\" : \"" << u->c_4xx << "\", \"5xx\" : \"" << u->c_5xx << "\" }, ";
    std::cout << "\"bytes\" : \"" << u->req.bytes << "\", ";
    // Service times
    std::cout << "\"svc_t\" : { \"min\" : \"" << u->time.min << "\", \"max\" : \"" << u->time.max << "\", \"avg\" : \""
              << std::setiosflags(ios::fixed) << std::setprecision(2) << u->time.avg << "\", \"dev\" : \""
              << std::setiosflags(ios::fixed) << std::setprecision(2) << u->time.stddev;

    if (as_object) {
      std::cout << "\" } }," << std::endl;
    } else {
      std::cout << "\" } } }," << std::endl;
    }
  }

  LruHash _hash;
  LruStack _stack;
  int _size, _show_urls;
  LruStack::iterator _cur;
};

///////////////////////////////////////////////////////////////////////////////
// Globals, holding the accumulated stats (ok, I'm lazy ...)
static OriginStats totals;
static OriginStorage origins;
static OriginSet *origin_set;
static UrlLru *urls;
static int parse_errors;

// Command line arguments (parsing)
struct CommandLineArgs {
  char log_file[1024];
  char origin_file[1024];
  char origin_list[MAX_ORIG_STRING];
  int max_origins;
  char state_tag[1024];
  int64_t min_hits;
  int max_age;
  int line_len;
  int incremental;     // Do an incremental run
  int tail;            // Tail the log file
  int summary;         // Summary only
  int json;            // JSON output
  int cgi;             // CGI output (typically with json)
  int urls;            // Produce JSON output of URL stats, arg is LRU size
  int show_urls;       // Max URLs to show
  int as_object;       // Show the URL stats as a single JSON object (not array)
  int concise;         // Eliminate metrics that can be inferred by other values
  int report_per_user; // A flag to aggregate and report stats per user instead of per host if 'true' (default 'false')
  int no_format_check; // A flag to skip the log format check if any of the fields is not a standard squid log format field.

  CommandLineArgs()
    : max_origins(0),
      min_hits(0),
      max_age(0),
      line_len(DEFAULT_LINE_LEN),
      incremental(0),
      tail(0),
      summary(0),
      json(0),
      cgi(0),
      urls(0),
      show_urls(0),
      as_object(0),
      concise(0),
      report_per_user(0),
      no_format_check(0)
  {
    log_file[0]    = '\0';
    origin_file[0] = '\0';
    origin_list[0] = '\0';
    state_tag[0]   = '\0';
  }

  void parse_arguments(const char **argv);
};

static CommandLineArgs cl;

static ArgumentDescription argument_descriptions[] = {
  {"log_file", 'f', "Specific logfile to parse", "S1023", cl.log_file, nullptr, nullptr},
  {"origin_list", 'o', "Only show stats for listed Origins", "S4095", cl.origin_list, nullptr, nullptr},
  {"origin_file", 'O', "File listing Origins to show", "S1023", cl.origin_file, nullptr, nullptr},
  {"max_orgins", 'M', "Max number of Origins to show", "I", &cl.max_origins, nullptr, nullptr},
  {"urls", 'u', "Produce JSON stats for URLs, argument is LRU size", "I", &cl.urls, nullptr, nullptr},
  {"show_urls", 'U', "Only show max this number of URLs", "I", &cl.show_urls, nullptr, nullptr},
  {"as_object", 'A', "Produce URL stats as a JSON object instead of array", "T", &cl.as_object, nullptr, nullptr},
  {"concise", 'C', "Eliminate metrics that can be inferred from other values", "T", &cl.concise, nullptr, nullptr},
  {"incremental", 'i', "Incremental log parsing", "T", &cl.incremental, nullptr, nullptr},
  {"statetag", 'S', "Name of the state file to use", "S1023", cl.state_tag, nullptr, nullptr},
  {"tail", 't', "Parse the last <sec> seconds of log", "I", &cl.tail, nullptr, nullptr},
  {"summary", 's', "Only produce the summary", "T", &cl.summary, nullptr, nullptr},
  {"json", 'j', "Produce JSON formatted output", "T", &cl.json, nullptr, nullptr},
  {"cgi", 'c', "Produce HTTP headers suitable as a CGI", "T", &cl.cgi, nullptr, nullptr},
  {"min_hits", 'm', "Minimum total hits for an Origin", "L", &cl.min_hits, nullptr, nullptr},
  {"max_age", 'a', "Max age for log entries to be considered", "I", &cl.max_age, nullptr, nullptr},
  {"line_len", 'l', "Output line length", "I", &cl.line_len, nullptr, nullptr},
  {"debug_tags", 'T', "Colon-Separated Debug Tags", "S1023", &error_tags, nullptr, nullptr},
  {"report_per_user", 'r', "Report stats per user instead of host", "T", &cl.report_per_user, nullptr, nullptr},
  {"no_format_check", 'n', "Don't validate the log format field names", "T", &cl.no_format_check, nullptr, nullptr},
  HELP_ARGUMENT_DESCRIPTION(),
  VERSION_ARGUMENT_DESCRIPTION(),
  RUNROOT_ARGUMENT_DESCRIPTION()};

static const char *USAGE_LINE = "Usage: " PROGRAM_NAME " [-f logfile] [-o origin[,...]] [-O originfile] [-m minhits] [-binshv]";

void
CommandLineArgs::parse_arguments(const char **argv)
{
  // process command-line arguments
  process_args(&appVersionInfo, argument_descriptions, countof(argument_descriptions), argv, USAGE_LINE);

  // Process as "CGI" ?
  if (strstr(argv[0], ".cgi") || cgi) {
    char *query;

    json = 1;
    cgi  = 1;

    if (nullptr != (query = getenv("QUERY_STRING"))) {
      char buffer[MAX_ORIG_STRING];
      char *tok, *sep_ptr, *val;

      ink_strlcpy(buffer, query, sizeof(buffer));
      unescapifyStr(buffer);

      for (tok = strtok_r(buffer, "&", &sep_ptr); tok != nullptr;) {
        val = strchr(tok, '=');
        if (val) {
          *(val++) = '\0';
          if (0 == strncmp(tok, "origin_list", 11)) {
            ink_strlcpy(origin_list, val, sizeof(origin_list));
          } else if (0 == strncmp(tok, "state_tag", 9)) {
            ink_strlcpy(state_tag, val, sizeof(state_tag));
          } else if (0 == strncmp(tok, "max_origins", 11)) {
            max_origins = strtol(val, nullptr, 10);
          } else if (0 == strncmp(tok, "urls", 4)) {
            urls = strtol(val, nullptr, 10);
          } else if (0 == strncmp(tok, "show_urls", 9)) {
            show_urls = strtol(val, nullptr, 10);
          } else if (0 == strncmp(tok, "as_object", 9)) {
            as_object = strtol(val, nullptr, 10);
          } else if (0 == strncmp(tok, "min_hits", 8)) {
            min_hits = strtol(val, nullptr, 10);
          } else if (0 == strncmp(tok, "incremental", 11)) {
            incremental = strtol(val, nullptr, 10);
          } else {
            // Unknown query arg.
          }
        }

        tok = strtok_r(nullptr, "&", &sep_ptr);
      }
    }
  }
}

// Enum for return code levels.
enum ExitLevel {
  EXIT_OK       = 0,
  EXIT_WARNING  = 1,
  EXIT_CRITICAL = 2,
  EXIT_UNKNOWN  = 3,
};

struct ExitStatus {
  ExitLevel level;
  char notice[1024];

  ExitStatus() : level(EXIT_OK) { memset(notice, 0, sizeof(notice)); }
  void
  set(ExitLevel l, const char *n = nullptr)
  {
    if (l > level) {
      level = l;
    }
    if (n) {
      ink_strlcat(notice, n, sizeof(notice));
    }
  }

  void
  append(const char *n)
  {
    ink_strlcat(notice, n, sizeof(notice));
  }

  void
  append(const std::string s)
  {
    ink_strlcat(notice, s.c_str(), sizeof(notice));
  }
};

// Enum for parsing a log line
enum ParseStates {
  P_STATE_ELAPSED,
  P_STATE_IP,
  P_STATE_RESULT,
  P_STATE_CODE,
  P_STATE_SIZE,
  P_STATE_METHOD,
  P_STATE_URL,
  P_STATE_RFC931,
  P_STATE_HIERARCHY,
  P_STATE_PEER,
  P_STATE_TYPE,
  P_STATE_END
};

// Enum for HTTP methods
enum HTTPMethod {
  METHOD_OPTIONS,
  METHOD_GET,
  METHOD_HEAD,
  METHOD_POST,
  METHOD_PUT,
  METHOD_DELETE,
  METHOD_TRACE,
  METHOD_CONNECT,
  METHOD_PURGE,
  METHOD_NONE,
  METHOD_OTHER
};

// Enum for URL schemes
enum URLScheme {
  SCHEME_HTTP,
  SCHEME_HTTPS,
  SCHEME_NONE,
  SCHEME_OTHER,
};

///////////////////////////////////////////////////////////////////////////////
// Initialize the elapsed field
inline void
init_elapsed(OriginStats *stats)
{
  stats->elapsed.hits.hit.min       = -1;
  stats->elapsed.hits.hit_ram.min   = -1;
  stats->elapsed.hits.ims.min       = -1;
  stats->elapsed.hits.refresh.min   = -1;
  stats->elapsed.hits.other.min     = -1;
  stats->elapsed.hits.total.min     = -1;
  stats->elapsed.misses.miss.min    = -1;
  stats->elapsed.misses.ims.min     = -1;
  stats->elapsed.misses.refresh.min = -1;
  stats->elapsed.misses.other.min   = -1;
  stats->elapsed.misses.total.min   = -1;
}

// Update the counters for one StatsCounter
inline void
update_counter(StatsCounter &counter, int size)
{
  counter.count++;
  counter.bytes += size;
}

inline void
update_elapsed(ElapsedStats &stat, const int elapsed, const StatsCounter &counter)
{
  int newcount, oldcount;
  float oldavg, newavg, sum_of_squares;

  // Skip all the "0" values.
  if (0 == elapsed) {
    return;
  }
  if (-1 == stat.min) {
    stat.min = elapsed;
  } else if (stat.min > elapsed) {
    stat.min = elapsed;
  }

  if (stat.max < elapsed) {
    stat.max = elapsed;
  }

  // update_counter should have been called on counter.count before calling
  // update_elapsed.
  newcount = counter.count;
  // New count should never be zero, else there was a programming error.
  ink_release_assert(newcount);
  oldcount = counter.count - 1;
  oldavg   = stat.avg;
  newavg   = (oldavg * oldcount + elapsed) / newcount;
  // Now find the new standard deviation from the old one

  if (oldcount != 0) {
    sum_of_squares = (stat.stddev * stat.stddev * oldcount);
  } else {
    sum_of_squares = 0;
  }

  // Find the old sum of squares.
  sum_of_squares = sum_of_squares + 2 * oldavg * oldcount * (oldavg - newavg) + oldcount * (newavg * newavg - oldavg * oldavg);

  // Now, find the new sum of squares.
  sum_of_squares = sum_of_squares + (elapsed - newavg) * (elapsed - newavg);

  stat.stddev = sqrt(sum_of_squares / newcount);
  stat.avg    = newavg;
}

///////////////////////////////////////////////////////////////////////////////
// Update the "result" and "elapsed" stats for a particular record
inline void
update_results_elapsed(OriginStats *stat, int result, int elapsed, int size)
{
  switch (result) {
  case SQUID_LOG_TCP_HIT:
    update_counter(stat->results.hits.hit, size);
    update_counter(stat->results.hits.total, size);
    update_elapsed(stat->elapsed.hits.hit, elapsed, stat->results.hits.hit);
    update_elapsed(stat->elapsed.hits.total, elapsed, stat->results.hits.total);
    break;
  case SQUID_LOG_TCP_MEM_HIT:
    update_counter(stat->results.hits.hit_ram, size);
    update_counter(stat->results.hits.total, size);
    update_elapsed(stat->elapsed.hits.hit_ram, elapsed, stat->results.hits.hit_ram);
    update_elapsed(stat->elapsed.hits.total, elapsed, stat->results.hits.total);
    break;
  case SQUID_LOG_TCP_MISS:
    update_counter(stat->results.misses.miss, size);
    update_counter(stat->results.misses.total, size);
    update_elapsed(stat->elapsed.misses.miss, elapsed, stat->results.misses.miss);
    update_elapsed(stat->elapsed.misses.total, elapsed, stat->results.misses.total);
    break;
  case SQUID_LOG_TCP_IMS_HIT:
    update_counter(stat->results.hits.ims, size);
    update_counter(stat->results.hits.total, size);
    update_elapsed(stat->elapsed.hits.ims, elapsed, stat->results.hits.ims);
    update_elapsed(stat->elapsed.hits.total, elapsed, stat->results.hits.total);
    break;
  case SQUID_LOG_TCP_IMS_MISS:
    update_counter(stat->results.misses.ims, size);
    update_counter(stat->results.misses.total, size);
    update_elapsed(stat->elapsed.misses.ims, elapsed, stat->results.misses.ims);
    update_elapsed(stat->elapsed.misses.total, elapsed, stat->results.misses.total);
    break;
  case SQUID_LOG_TCP_REFRESH_HIT:
    update_counter(stat->results.hits.refresh, size);
    update_counter(stat->results.hits.total, size);
    update_elapsed(stat->elapsed.hits.refresh, elapsed, stat->results.hits.refresh);
    update_elapsed(stat->elapsed.hits.total, elapsed, stat->results.hits.total);
    break;
  case SQUID_LOG_TCP_REFRESH_MISS:
    update_counter(stat->results.misses.refresh, size);
    update_counter(stat->results.misses.total, size);
    update_elapsed(stat->elapsed.misses.refresh, elapsed, stat->results.misses.refresh);
    update_elapsed(stat->elapsed.misses.total, elapsed, stat->results.misses.total);
    break;
  case SQUID_LOG_TCP_DISK_HIT:
  case SQUID_LOG_TCP_REF_FAIL_HIT:
  case SQUID_LOG_UDP_HIT:
  case SQUID_LOG_UDP_WEAK_HIT:
  case SQUID_LOG_UDP_HIT_OBJ:
    update_counter(stat->results.hits.other, size);
    update_counter(stat->results.hits.total, size);
    update_elapsed(stat->elapsed.hits.other, elapsed, stat->results.hits.other);
    update_elapsed(stat->elapsed.hits.total, elapsed, stat->results.hits.total);
    break;
  case SQUID_LOG_TCP_EXPIRED_MISS:
  case SQUID_LOG_TCP_WEBFETCH_MISS:
  case SQUID_LOG_UDP_MISS:
    update_counter(stat->results.misses.other, size);
    update_counter(stat->results.misses.total, size);
    update_elapsed(stat->elapsed.misses.other, elapsed, stat->results.misses.other);
    update_elapsed(stat->elapsed.misses.total, elapsed, stat->results.misses.total);
    break;
  case SQUID_LOG_ERR_CLIENT_ABORT:
    update_counter(stat->results.errors.client_abort, size);
    update_counter(stat->results.errors.total, size);
    break;
  case SQUID_LOG_ERR_CLIENT_READ_ERROR:
    update_counter(stat->results.errors.client_read_error, size);
    update_counter(stat->results.errors.total, size);
    break;
  case SQUID_LOG_ERR_CONNECT_FAIL:
    update_counter(stat->results.errors.connect_fail, size);
    update_counter(stat->results.errors.total, size);
    break;
  case SQUID_LOG_ERR_INVALID_REQ:
    update_counter(stat->results.errors.invalid_req, size);
    update_counter(stat->results.errors.total, size);
    break;
  case SQUID_LOG_ERR_UNKNOWN:
    update_counter(stat->results.errors.unknown, size);
    update_counter(stat->results.errors.total, size);
    break;
  default:
    // This depends on all errors being at the end of the enum ... Which is the case right now.
    if (result < SQUID_LOG_ERR_READ_TIMEOUT) {
      update_counter(stat->results.other, size);
    } else {
      update_counter(stat->results.errors.other, size);
      update_counter(stat->results.errors.total, size);
    }
    break;
  }
}

///////////////////////////////////////////////////////////////////////////////
// Update the "codes" stats for a particular record
inline void
update_codes(OriginStats *stat, int code, int size)
{
  switch (code) {
  case 100:
    update_counter(stat->codes.c_100, size);
    break;

  // 200's
  case 200:
    update_counter(stat->codes.c_200, size);
    break;
  case 201:
    update_counter(stat->codes.c_201, size);
    break;
  case 202:
    update_counter(stat->codes.c_202, size);
    break;
  case 203:
    update_counter(stat->codes.c_203, size);
    break;
  case 204:
    update_counter(stat->codes.c_204, size);
    break;
  case 205:
    update_counter(stat->codes.c_205, size);
    break;
  case 206:
    update_counter(stat->codes.c_206, size);
    break;

  // 300's
  case 300:
    update_counter(stat->codes.c_300, size);
    break;
  case 301:
    update_counter(stat->codes.c_301, size);
    break;
  case 302:
    update_counter(stat->codes.c_302, size);
    break;
  case 303:
    update_counter(stat->codes.c_303, size);
    break;
  case 304:
    update_counter(stat->codes.c_304, size);
    break;
  case 305:
    update_counter(stat->codes.c_305, size);
    break;
  case 307:
    update_counter(stat->codes.c_307, size);
    break;

  // 400's
  case 400:
    update_counter(stat->codes.c_400, size);
    break;
  case 401:
    update_counter(stat->codes.c_401, size);
    break;
  case 402:
    update_counter(stat->codes.c_402, size);
    break;
  case 403:
    update_counter(stat->codes.c_403, size);
    break;
  case 404:
    update_counter(stat->codes.c_404, size);
    break;
  case 405:
    update_counter(stat->codes.c_405, size);
    break;
  case 406:
    update_counter(stat->codes.c_406, size);
    break;
  case 407:
    update_counter(stat->codes.c_407, size);
    break;
  case 408:
    update_counter(stat->codes.c_408, size);
    break;
  case 409:
    update_counter(stat->codes.c_409, size);
    break;
  case 410:
    update_counter(stat->codes.c_410, size);
    break;
  case 411:
    update_counter(stat->codes.c_411, size);
    break;
  case 412:
    update_counter(stat->codes.c_412, size);
    break;
  case 413:
    update_counter(stat->codes.c_413, size);
    break;
  case 414:
    update_counter(stat->codes.c_414, size);
    break;
  case 415:
    update_counter(stat->codes.c_415, size);
    break;
  case 416:
    update_counter(stat->codes.c_416, size);
    break;
  case 417:
    update_counter(stat->codes.c_417, size);
    break;

  // 500's
  case 500:
    update_counter(stat->codes.c_500, size);
    break;
  case 501:
    update_counter(stat->codes.c_501, size);
    break;
  case 502:
    update_counter(stat->codes.c_502, size);
    break;
  case 503:
    update_counter(stat->codes.c_503, size);
    break;
  case 504:
    update_counter(stat->codes.c_504, size);
    break;
  case 505:
    update_counter(stat->codes.c_505, size);
    break;
  default:
    break;
  }

  if ((code >= 600) || (code < 200)) {
    update_counter(stat->codes.c_000, size);
  } else if (code >= 500) {
    update_counter(stat->codes.c_5xx, size);
  } else if (code >= 400) {
    update_counter(stat->codes.c_4xx, size);
  } else if (code >= 300) {
    update_counter(stat->codes.c_3xx, size);
  } else if (code >= 200) {
    update_counter(stat->codes.c_2xx, size);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Update the "methods" stats for a particular record
inline void
update_methods(OriginStats *stat, int method, int size)
{
  // We're so loppsided on GETs, so makes most sense to test 'out of order'.
  switch (method) {
  case METHOD_GET:
    update_counter(stat->methods.get, size);
    break;

  case METHOD_OPTIONS:
    update_counter(stat->methods.options, size);
    break;

  case METHOD_HEAD:
    update_counter(stat->methods.head, size);
    break;

  case METHOD_POST:
    update_counter(stat->methods.post, size);
    break;

  case METHOD_PUT:
    update_counter(stat->methods.put, size);
    break;

  case METHOD_DELETE:
    update_counter(stat->methods.del, size);
    break;

  case METHOD_TRACE:
    update_counter(stat->methods.trace, size);
    break;

  case METHOD_CONNECT:
    update_counter(stat->methods.connect, size);
    break;

  case METHOD_PURGE:
    update_counter(stat->methods.purge, size);
    break;

  case METHOD_NONE:
    update_counter(stat->methods.none, size);
    break;

  default:
    update_counter(stat->methods.other, size);
    break;
  }
}

///////////////////////////////////////////////////////////////////////////////
// Update the "schemes" stats for a particular record
inline void
update_schemes(OriginStats *stat, int scheme, int size)
{
  if (SCHEME_HTTP == scheme) {
    update_counter(stat->schemes.http, size);
  } else if (SCHEME_HTTPS == scheme) {
    update_counter(stat->schemes.https, size);
  } else if (SCHEME_NONE == scheme) {
    update_counter(stat->schemes.none, size);
  } else {
    update_counter(stat->schemes.other, size);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Update the "protocols" stats for a particular record
inline void
update_protocols(OriginStats *stat, bool ipv6, int size)
{
  if (ipv6) {
    update_counter(stat->protocols.ipv6, size);
  } else {
    update_counter(stat->protocols.ipv4, size);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Finds or creates a stats structures if missing
OriginStats *
find_or_create_stats(const char *key)
{
  OriginStats *o_stats = nullptr;
  OriginStorage::iterator o_iter;
  char *o_server = nullptr;

  // TODO: If we save state (struct) for a run, we probably need to always
  // update the origin data, no matter what the origin_set is.
  if (origin_set->empty() || (origin_set->find(key) != origin_set->end())) {
    o_iter = origins.find(key);
    if (origins.end() == o_iter) {
      o_stats = (OriginStats *)ats_malloc(sizeof(OriginStats));
      memset(o_stats, 0, sizeof(OriginStats));
      init_elapsed(o_stats);
      o_server = ats_strdup(key);
      if (o_server) {
        o_stats->server   = o_server;
        origins[o_server] = o_stats;
      }
    } else {
      o_stats = o_iter->second;
    }
  }
  return o_stats;
}

///////////////////////////////////////////////////////////////////////////////
// Update the stats
void
update_stats(OriginStats *o_stats, const HTTPMethod method, URLScheme scheme, int http_code, int size, int result, int hier,
             int elapsed, bool ipv6)
{
  update_results_elapsed(&totals, result, elapsed, size);
  update_codes(&totals, http_code, size);
  update_methods(&totals, method, size);
  update_schemes(&totals, scheme, size);
  update_protocols(&totals, ipv6, size);
  update_counter(totals.total, size);
  if (nullptr != o_stats) {
    update_results_elapsed(o_stats, result, elapsed, size);
    update_codes(o_stats, http_code, size);
    update_methods(o_stats, method, size);
    update_schemes(o_stats, scheme, size);
    update_protocols(o_stats, ipv6, size);
    update_counter(o_stats->total, size);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Parse a log buffer
int
parse_log_buff(LogBufferHeader *buf_header, bool summary = false, bool aggregate_per_userid = false)
{
  static LogFieldList *fieldlist = nullptr;

  LogEntryHeader *entry;
  LogBufferIterator buf_iter(buf_header);
  LogField *field = nullptr;
  ParseStates state;

  char *read_from;
  char *tok;
  char *ptr;
  int tok_len;
  int flag = 0; // Flag used in state machine to carry "state" forward

  // Parsed results
  int http_code = 0, size = 0, result = 0, hier = 0, elapsed = 0;
  bool ipv6 = false;
  OriginStats *o_stats;
  HTTPMethod method;
  URLScheme scheme;

  if (!fieldlist) {
    fieldlist = new LogFieldList;
    ink_assert(fieldlist != nullptr);
    bool agg = false;
    LogFormat::parse_symbol_string(buf_header->fmt_fieldlist(), fieldlist, &agg);
  }

  if (!cl.no_format_check) {
    // Validate the fieldlist
    field                                = fieldlist->first();
    const std::string_view test_fields[] = {"cqtq", "ttms", "chi", "crc", "pssc", "psql", "cqhm", "cquc", "caun", "phr", "shn"};
    for (auto i : test_fields) {
      if (i != field->symbol()) {
        cerr << "Error parsing log file - expected field: " << i << ", but read field: " << field->symbol() << endl;
        return 1;
      }
      field = fieldlist->next(field);
    }
  }

  // Loop over all entries
  while ((entry = buf_iter.next())) {
    read_from = (char *)entry + sizeof(LogEntryHeader);
    // We read and skip over the first field, which is the timestamp.
    if ((field = fieldlist->first())) {
      read_from += INK_MIN_ALIGN;
    } else { // This shouldn't happen, buffer must be messed up.
      break;
    }

    state   = P_STATE_ELAPSED;
    o_stats = nullptr;
    method  = METHOD_OTHER;
    scheme  = SCHEME_OTHER;

    while ((field = fieldlist->next(field))) {
      switch (state) {
      case P_STATE_ELAPSED:
        state   = P_STATE_IP;
        elapsed = *((int64_t *)(read_from));
        read_from += INK_MIN_ALIGN;
        break;

      case P_STATE_IP:
        state = P_STATE_RESULT;
        // Just skip the IP, we no longer assume it's always the same.
        {
          LogFieldIp *ip = reinterpret_cast<LogFieldIp *>(read_from);
          int len        = sizeof(LogFieldIp);
          if (AF_INET == ip->_family) {
            ipv6 = false;
            len  = sizeof(LogFieldIp4);
          } else if (AF_INET6 == ip->_family) {
            ipv6 = true;
            len  = sizeof(LogFieldIp6);
          }
          read_from += INK_ALIGN_DEFAULT(len);
        }
        break;

      case P_STATE_RESULT:
        state  = P_STATE_CODE;
        result = *((int64_t *)(read_from));
        read_from += INK_MIN_ALIGN;
        if ((result < 32) || (result > 255)) {
          flag  = 1;
          state = P_STATE_END;
        }
        break;

      case P_STATE_CODE:
        state     = P_STATE_SIZE;
        http_code = *((int64_t *)(read_from));
        read_from += INK_MIN_ALIGN;
        if ((http_code < 0) || (http_code > 999)) {
          flag  = 1;
          state = P_STATE_END;
        }
        break;

      case P_STATE_SIZE:
        // Warning: This is not 64-bit safe, when converting the log format,
        // this needs to be fixed as well.
        state = P_STATE_METHOD;
        size  = *((int64_t *)(read_from));
        read_from += INK_MIN_ALIGN;
        break;

      case P_STATE_METHOD:
        state = P_STATE_URL;
        flag  = 0;

        // Small optimization for common (3-4 char) cases
        switch (*reinterpret_cast<int *>(read_from)) {
        case GET_AS_INT:
          method = METHOD_GET;
          read_from += LogAccess::round_strlen(3 + 1);
          break;
        case PUT_AS_INT:
          method = METHOD_PUT;
          read_from += LogAccess::round_strlen(3 + 1);
          break;
        case HEAD_AS_INT:
          method = METHOD_HEAD;
          read_from += LogAccess::round_strlen(4 + 1);
          break;
        case POST_AS_INT:
          method = METHOD_POST;
          read_from += LogAccess::round_strlen(4 + 1);
          break;
        default:
          tok_len = strlen(read_from);
          if ((5 == tok_len) && (0 == strncmp(read_from, "PURGE", 5))) {
            method = METHOD_PURGE;
          } else if ((6 == tok_len) && (0 == strncmp(read_from, "DELETE", 6))) {
            method = METHOD_DELETE;
          } else if ((7 == tok_len) && (0 == strncmp(read_from, "OPTIONS", 7))) {
            method = METHOD_OPTIONS;
          } else if ((1 == tok_len) && ('-' == *read_from)) {
            method = METHOD_NONE;
            flag   = 1; // No method, so no need to parse the URL
          } else {
            ptr = read_from;
            while (*ptr && isupper(*ptr)) {
              ++ptr;
            }
            // Skip URL if it doesn't look like an HTTP method
            if (*ptr != '\0') {
              flag = 1;
            }
          }
          read_from += LogAccess::round_strlen(tok_len + 1);
          break;
        }
        break;

      case P_STATE_URL:
        state = P_STATE_RFC931;
        if (urls) {
          urls->add_stat(read_from, size, elapsed, result, http_code, cl.as_object);
        }

        // TODO check for read_from being empty string
        if (0 == flag) {
          tok = read_from;
          if (HTTP_AS_INT == *reinterpret_cast<int *>(tok)) {
            tok += 4;
            if (':' == *tok) {
              scheme = SCHEME_HTTP;
              tok += 3;
              tok_len = strlen(tok) + 7;
            } else if ('s' == *tok) {
              scheme = SCHEME_HTTPS;
              tok += 4;
              tok_len = strlen(tok) + 8;
            } else {
              tok_len = strlen(tok) + 4;
            }
          } else {
            if ('/' == *tok) {
              scheme = SCHEME_NONE;
            }
            tok_len = strlen(tok);
          }
          if ('/' == *tok) { // This is to handle crazy stuff like http:///origin.com
            tok++;
          }
          ptr = strchr(tok, '/');
          if (ptr) {
            *ptr = '\0';
          }
          if (!aggregate_per_userid && !summary) {
            o_stats = find_or_create_stats(tok);
          }
        } else {
          // No method given
          if ('/' == *read_from) {
            scheme = SCHEME_NONE;
          }
          tok_len = strlen(read_from);
        }
        read_from += LogAccess::round_strlen(tok_len + 1);
        if (!aggregate_per_userid) {
          update_stats(o_stats, method, scheme, http_code, size, result, hier, elapsed, ipv6);
        }
        break;

      case P_STATE_RFC931:
        state = P_STATE_HIERARCHY;

        if (aggregate_per_userid) {
          if (!summary) {
            o_stats = find_or_create_stats(read_from);
          }
          update_stats(o_stats, method, scheme, http_code, size, result, hier, elapsed, ipv6);
        }

        if ('-' == *read_from) {
          read_from += LogAccess::round_strlen(1 + 1);
        } else {
          read_from += LogAccess::strlen(read_from);
        }
        break;

      case P_STATE_HIERARCHY:
        state = P_STATE_PEER;
        hier  = *((int64_t *)(read_from));
        switch (hier) {
        case SQUID_HIER_NONE:
          update_counter(totals.hierarchies.none, size);
          if (o_stats != nullptr) {
            update_counter(o_stats->hierarchies.none, size);
          }
          break;
        case SQUID_HIER_DIRECT:
          update_counter(totals.hierarchies.direct, size);
          if (o_stats != nullptr) {
            update_counter(o_stats->hierarchies.direct, size);
          }
          break;
        case SQUID_HIER_SIBLING_HIT:
          update_counter(totals.hierarchies.sibling, size);
          if (o_stats != nullptr) {
            update_counter(o_stats->hierarchies.sibling, size);
          }
          break;
        case SQUID_HIER_PARENT_HIT:
          update_counter(totals.hierarchies.parent, size);
          if (o_stats != nullptr) {
            update_counter(o_stats->hierarchies.direct, size);
          }
          break;
        case SQUID_HIER_EMPTY:
          update_counter(totals.hierarchies.empty, size);
          if (o_stats != nullptr) {
            update_counter(o_stats->hierarchies.empty, size);
          }
          break;
        default:
          if ((hier >= SQUID_HIER_EMPTY) && (hier < SQUID_HIER_INVALID_ASSIGNED_CODE)) {
            update_counter(totals.hierarchies.other, size);
            if (o_stats != nullptr) {
              update_counter(o_stats->hierarchies.other, size);
            }
          } else {
            update_counter(totals.hierarchies.invalid, size);
            if (o_stats != nullptr) {
              update_counter(o_stats->hierarchies.invalid, size);
            }
          }
          break;
        }
        read_from += INK_MIN_ALIGN;
        break;

      case P_STATE_PEER:
        state = P_STATE_TYPE;
        if ('-' == *read_from) {
          read_from += LogAccess::round_strlen(1 + 1);
        } else {
          read_from += LogAccess::strlen(read_from);
        }
        break;

      case P_STATE_TYPE:
        state = P_STATE_END;
        if (IMAG_AS_INT == *reinterpret_cast<int *>(read_from)) {
          update_counter(totals.content.image.total, size);
          if (o_stats != nullptr) {
            update_counter(o_stats->content.image.total, size);
          }
          tok = read_from + 6;
          switch (*reinterpret_cast<int *>(tok)) {
          case JPEG_AS_INT:
            tok_len = 10;
            update_counter(totals.content.image.jpeg, size);
            if (o_stats != nullptr) {
              update_counter(o_stats->content.image.jpeg, size);
            }
            break;
          case JPG_AS_INT:
            tok_len = 9;
            update_counter(totals.content.image.jpeg, size);
            if (o_stats != nullptr) {
              update_counter(o_stats->content.image.jpeg, size);
            }
            break;
          case GIF_AS_INT:
            tok_len = 9;
            update_counter(totals.content.image.gif, size);
            if (o_stats != nullptr) {
              update_counter(o_stats->content.image.gif, size);
            }
            break;
          case PNG_AS_INT:
            tok_len = 9;
            update_counter(totals.content.image.png, size);
            if (o_stats != nullptr) {
              update_counter(o_stats->content.image.png, size);
            }
            break;
          case BMP_AS_INT:
            tok_len = 9;
            update_counter(totals.content.image.bmp, size);
            if (o_stats != nullptr) {
              update_counter(o_stats->content.image.bmp, size);
            }
            break;
          default:
            tok_len = 6 + strlen(tok);
            update_counter(totals.content.image.other, size);
            if (o_stats != nullptr) {
              update_counter(o_stats->content.image.other, size);
            }
            break;
          }
        } else if (TEXT_AS_INT == *reinterpret_cast<int *>(read_from)) {
          tok = read_from + 5;
          update_counter(totals.content.text.total, size);
          if (o_stats != nullptr) {
            update_counter(o_stats->content.text.total, size);
          }
          switch (*reinterpret_cast<int *>(tok)) {
          case JAVA_AS_INT:
            // TODO verify if really "javascript"
            tok_len = 15;
            update_counter(totals.content.text.javascript, size);
            if (o_stats != nullptr) {
              update_counter(o_stats->content.text.javascript, size);
            }
            break;
          case CSS_AS_INT:
            tok_len = 8;
            update_counter(totals.content.text.css, size);
            if (o_stats != nullptr) {
              update_counter(o_stats->content.text.css, size);
            }
            break;
          case XML_AS_INT:
            tok_len = 8;
            update_counter(totals.content.text.xml, size);
            if (o_stats != nullptr) {
              update_counter(o_stats->content.text.xml, size);
            }
            break;
          case HTML_AS_INT:
            tok_len = 9;
            update_counter(totals.content.text.html, size);
            if (o_stats != nullptr) {
              update_counter(o_stats->content.text.html, size);
            }
            break;
          case PLAI_AS_INT:
            tok_len = 10;
            update_counter(totals.content.text.plain, size);
            if (o_stats != nullptr) {
              update_counter(o_stats->content.text.plain, size);
            }
            break;
          default:
            tok_len = 5 + strlen(tok);
            update_counter(totals.content.text.other, size);
            if (o_stats != nullptr) {
              update_counter(o_stats->content.text.other, size);
            }
            break;
          }
        } else if (0 == strncmp(read_from, "application", 11)) {
          tok = read_from + 12;
          update_counter(totals.content.application.total, size);
          if (o_stats != nullptr) {
            update_counter(o_stats->content.application.total, size);
          }
          switch (*reinterpret_cast<int *>(tok)) {
          case ZIP_AS_INT:
            tok_len = 15;
            update_counter(totals.content.application.zip, size);
            if (o_stats != nullptr) {
              update_counter(o_stats->content.application.zip, size);
            }
            break;
          case JAVA_AS_INT:
            tok_len = 22;
            update_counter(totals.content.application.javascript, size);
            if (o_stats != nullptr) {
              update_counter(o_stats->content.application.javascript, size);
            }
            break;
          case X_JA_AS_INT:
            tok_len = 24;
            update_counter(totals.content.application.javascript, size);
            if (o_stats != nullptr) {
              update_counter(o_stats->content.application.javascript, size);
            }
            break;
          case RSSp_AS_INT:
            if (0 == strcmp(tok + 4, "xml")) {
              tok_len = 19;
              update_counter(totals.content.application.rss_xml, size);
              if (o_stats != nullptr) {
                update_counter(o_stats->content.application.rss_xml, size);
              }
            } else if (0 == strcmp(tok + 4, "atom")) {
              tok_len = 20;
              update_counter(totals.content.application.rss_atom, size);
              if (o_stats != nullptr) {
                update_counter(o_stats->content.application.rss_atom, size);
              }
            } else {
              tok_len = 12 + strlen(tok);
              update_counter(totals.content.application.rss_other, size);
              if (o_stats != nullptr) {
                update_counter(o_stats->content.application.rss_other, size);
              }
            }
            break;
          default:
            if (0 == strcmp(tok, "x-shockwave-flash")) {
              tok_len = 29;
              update_counter(totals.content.application.shockwave_flash, size);
              if (o_stats != nullptr) {
                update_counter(o_stats->content.application.shockwave_flash, size);
              }
            } else if (0 == strcmp(tok, "x-quicktimeplayer")) {
              tok_len = 29;
              update_counter(totals.content.application.quicktime, size);
              if (o_stats != nullptr) {
                update_counter(o_stats->content.application.quicktime, size);
              }
            } else {
              tok_len = 12 + strlen(tok);
              update_counter(totals.content.application.other, size);
              if (o_stats != nullptr) {
                update_counter(o_stats->content.application.other, size);
              }
            }
          }
        } else if (0 == strncmp(read_from, "audio", 5)) {
          tok     = read_from + 6;
          tok_len = 6 + strlen(tok);
          update_counter(totals.content.audio.total, size);
          if (o_stats != nullptr) {
            update_counter(o_stats->content.audio.total, size);
          }
          if ((0 == strcmp(tok, "x-wav")) || (0 == strcmp(tok, "wav"))) {
            update_counter(totals.content.audio.wav, size);
            if (o_stats != nullptr) {
              update_counter(o_stats->content.audio.wav, size);
            }
          } else if ((0 == strcmp(tok, "x-mpeg")) || (0 == strcmp(tok, "mpeg"))) {
            update_counter(totals.content.audio.mpeg, size);
            if (o_stats != nullptr) {
              update_counter(o_stats->content.audio.mpeg, size);
            }
          } else {
            update_counter(totals.content.audio.other, size);
            if (o_stats != nullptr) {
              update_counter(o_stats->content.audio.other, size);
            }
          }
        } else if ('-' == *read_from) {
          tok_len = 1;
          update_counter(totals.content.none, size);
          if (o_stats != nullptr) {
            update_counter(o_stats->content.none, size);
          }
        } else {
          tok_len = strlen(read_from);
          update_counter(totals.content.other, size);
          if (o_stats != nullptr) {
            update_counter(o_stats->content.other, size);
          }
        }
        read_from += LogAccess::round_strlen(tok_len + 1);
        flag = 0; // We exited this state without errors
        break;

      case P_STATE_END:
        // Nothing to do really
        if (flag) {
          parse_errors++;
        }
        break;
      }
    }
  }

  return 0;
}

///////////////////////////////////////////////////////////////////////////////
// Process a file (FD)
int
process_file(int in_fd, off_t offset, unsigned max_age)
{
  char buffer[MAX_LOGBUFFER_SIZE];
  int nread, buffer_bytes;

  Debug("logstats", "Processing file [offset=%" PRId64 "].", (int64_t)offset);
  while (true) {
    Debug("logstats", "Reading initial header.");
    buffer[0] = '\0';

    unsigned first_read_size = sizeof(uint32_t) + sizeof(uint32_t);
    LogBufferHeader *header  = (LogBufferHeader *)&buffer[0];

    // Find the next log header, aligning us properly. This is not
    // particularly optimal, but we should only have to do this
    // once, and hopefully we'll be aligned immediately.
    if (offset > 0) {
      Debug("logstats", "Re-aligning file read.");
      while (true) {
        if (lseek(in_fd, offset, SEEK_SET) < 0) {
          Debug("logstats", "Internal seek failed (offset=%" PRId64 ").", (int64_t)offset);
          return 1;
        }

        // read the first 8 bytes of the header, which will give us the
        // cookie and the version number.
        nread = read(in_fd, buffer, first_read_size);
        if (!nread || EOF == nread) {
          return 0;
        }
        // ensure that this is a valid logbuffer header
        if (header->cookie && (LOG_SEGMENT_COOKIE == header->cookie)) {
          offset = 0;
          break;
        }
        offset++;
      }
      if (!header->cookie) {
        return 0;
      }
    } else {
      nread = read(in_fd, buffer, first_read_size);
      if (!nread || EOF == nread || !header->cookie) {
        return 0;
      }

      // ensure that this is a valid logbuffer header
      if (header->cookie != LOG_SEGMENT_COOKIE) {
        Debug("logstats", "Invalid segment cookie (expected %d, got %d)", LOG_SEGMENT_COOKIE, header->cookie);
        return 1;
      }
    }

    Debug("logstats", "LogBuffer version %d, current = %d", header->version, LOG_SEGMENT_VERSION);
    if (header->version != LOG_SEGMENT_VERSION) {
      return 1;
    }

    // read the rest of the header
    unsigned second_read_size = sizeof(LogBufferHeader) - first_read_size;
    nread                     = read(in_fd, &buffer[first_read_size], second_read_size);
    if (!nread || EOF == nread) {
      Debug("logstats", "Second read of header failed (attemped %d bytes at offset %d, got nothing), errno=%d.", second_read_size,
            first_read_size, errno);
      return 1;
    }

    // read the rest of the buffer
    if (header->byte_count > sizeof(buffer)) {
      Debug("logstats", "Header byte count [%d] > expected [%zu]", header->byte_count, sizeof(buffer));
      return 1;
    }

    buffer_bytes = header->byte_count - sizeof(LogBufferHeader);
    if (buffer_bytes <= 0 || (unsigned int)buffer_bytes > (sizeof(buffer) - sizeof(LogBufferHeader))) {
      Debug("logstats", "Buffer payload [%d] is wrong.", buffer_bytes);
      return 1;
    }

    const int MAX_READ_TRIES = 5;
    int total_read           = 0;
    int read_tries_remaining = MAX_READ_TRIES; // since the data will be old anyway, let's only try a few times.
    do {
      nread = read(in_fd, &buffer[sizeof(LogBufferHeader) + total_read], buffer_bytes - total_read);
      if (EOF == nread || !nread) { // just bail on error
        Debug("logstats", "Read failed while reading log buffer, wanted %d bytes, nread=%d, errno=%d", buffer_bytes - total_read,
              nread, errno);
        return 1;
      } else {
        total_read += nread;
      }

      if (total_read < buffer_bytes) {
        if (--read_tries_remaining <= 0) {
          Debug("logstats_failed_retries", "Unable to read after %d tries, total_read=%d, buffer_bytes=%d", MAX_READ_TRIES,
                total_read, buffer_bytes);
          return 1;
        }
        // let's wait until we get more data on this file descriptor
        Debug("logstats_partial_read",
              "Failed to read buffer payload [%d bytes], total_read=%d, buffer_bytes=%d, tries_remaining=%d",
              buffer_bytes - total_read, total_read, buffer_bytes, read_tries_remaining);
        usleep(50 * 1000); // wait 50ms
      }
    } while (total_read < buffer_bytes);

    // Possibly skip too old entries (the entire buffer is skipped)
    if (header->high_timestamp >= max_age) {
      if (parse_log_buff(header, cl.summary != 0, cl.report_per_user != 0) != 0) {
        Debug("logstats", "Failed to parse log buffer.");
        return 1;
      }
    } else {
      Debug("logstats", "Skipping old buffer (age=%d, max=%d)", header->high_timestamp, max_age);
    }
  }

  return 0;
}

///////////////////////////////////////////////////////////////////////////////
// Determine if this "stat" (Origin Server) is worthwhile to produce a
// report for.
inline int
use_origin(const OriginStats *stat)
{
  return cl.report_per_user != 0 ?
           (stat->total.count > cl.min_hits) :
           ((stat->total.count > cl.min_hits) && (nullptr != strchr(stat->server, '.')) && (nullptr == strchr(stat->server, '%')));
}

///////////////////////////////////////////////////////////////////////////////
// Produce a nicely formatted output for a stats collection on a stream
inline void
format_center(const char *str)
{
  std::cout << std::setfill(' ') << std::setw((cl.line_len - strlen(str)) / 2 + strlen(str)) << str << std::endl << std::endl;
}

inline void
format_int(int64_t num)
{
  if (num > 0) {
    int64_t mult = (int64_t)pow((double)10, (int)(log10((double)num) / 3) * 3);
    int64_t div;
    std::stringstream ss;

    ss.fill('0');
    while (mult > 0) {
      div = num / mult;
      ss << div << std::setw(3);
      num -= (div * mult);
      if (mult /= 1000) {
        ss << std::setw(0) << ',' << std::setw(3);
      }
    }
    std::cout << ss.str();
  } else {
    std::cout << '0';
  }
}

void
format_elapsed_header()
{
  std::cout << std::left << std::setw(24) << "Elapsed time stats";
  std::cout << std::right << std::setw(7) << "Min" << std::setw(13) << "Max";
  std::cout << std::right << std::setw(17) << "Avg" << std::setw(17) << "Std Deviation" << std::endl;
  std::cout << std::setw(cl.line_len) << std::setfill('-') << '-' << std::setfill(' ') << std::endl;
}

inline void
format_elapsed_line(const char *desc, const ElapsedStats &stat, bool json, bool concise)
{
  if (json) {
    std::cout << "    " << '"' << desc << "\" : "
              << "{ ";
    std::cout << "\"min\": \"" << stat.min << "\", ";
    std::cout << "\"max\": \"" << stat.max << "\"";
    if (!concise) {
      std::cout << ", \"avg\": \"" << std::setiosflags(ios::fixed) << std::setprecision(2) << stat.avg << "\", ";
      std::cout << "\"dev\": \"" << std::setiosflags(ios::fixed) << std::setprecision(2) << stat.stddev << "\"";
    }
    std::cout << " }," << std::endl;
  } else {
    std::cout << std::left << std::setw(24) << desc;
    std::cout << std::right << std::setw(7);
    format_int(stat.min);
    std::cout << std::right << std::setw(13);
    format_int(stat.max);

    std::cout << std::right << std::setw(17) << std::setiosflags(ios::fixed) << std::setprecision(2) << stat.avg;
    std::cout << std::right << std::setw(17) << std::setiosflags(ios::fixed) << std::setprecision(2) << stat.stddev;
    std::cout << std::endl;
  }
}

void
format_detail_header(const char *desc, bool concise = false)
{
  std::cout << std::left << std::setw(29) << desc;
  std::cout << std::right << std::setw(15) << "Count" << std::setw(11) << "Percent";
  std::cout << std::right << std::setw(12) << "Bytes" << std::setw(11) << "Percent" << std::endl;
  std::cout << std::setw(cl.line_len) << std::setfill('-') << '-' << std::setfill(' ') << std::endl;
}

inline void
format_line(const char *desc, const StatsCounter &stat, const StatsCounter &total, bool json, bool concise)
{
  static char metrics[] = "KKMGTP";
  static char buf[64];
  int ix = (stat.bytes > 1024 ? (int)(log10((double)stat.bytes) / LOG10_1024) : 1);

  if (json) {
    std::cout << "    " << '"' << desc << "\" : "
              << "{ ";
    std::cout << "\"req\": \"" << stat.count << "\", ";
    if (!concise) {
      std::cout << "\"req_pct\": \"" << std::setiosflags(ios::fixed) << std::setprecision(2)
                << (double)stat.count / total.count * 100 << "\", ";
    }
    std::cout << "\"bytes\": \"" << stat.bytes << "\"";

    if (!concise) {
      std::cout << ", \"bytes_pct\": \"" << std::setiosflags(ios::fixed) << std::setprecision(2)
                << (double)stat.bytes / total.bytes * 100 << "\"";
    }
    std::cout << " }," << std::endl;
  } else {
    std::cout << std::left << std::setw(29) << desc;

    std::cout << std::right << std::setw(15);
    format_int(stat.count);

    snprintf(buf, sizeof(buf), "%10.2f%%", ((double)stat.count / total.count * 100));
    std::cout << std::right << buf;

    snprintf(buf, sizeof(buf), "%10.2f%cB", stat.bytes / pow((double)1024, ix), metrics[ix]);
    std::cout << std::right << buf;

    snprintf(buf, sizeof(buf), "%10.2f%%", ((double)stat.bytes / total.bytes * 100));
    std::cout << std::right << buf << std::endl;
  }
}

// Little "helpers" for the vector we use to sort the Origins.
typedef pair<const char *, OriginStats *> OriginPair;
inline bool
operator<(const OriginPair &a, const OriginPair &b)
{
  return a.second->total.count > b.second->total.count;
}

void
print_detail_stats(const OriginStats *stat, bool json, bool concise)
{
  // Cache hit/misses etc.
  if (!json) {
    format_detail_header("Request Result");
  }

  format_line(json ? "hit.direct" : "Cache hit", stat->results.hits.hit, stat->total, json, concise);
  format_line(json ? "hit.ram" : "Cache hit RAM", stat->results.hits.hit_ram, stat->total, json, concise);
  format_line(json ? "hit.ims" : "Cache hit IMS", stat->results.hits.ims, stat->total, json, concise);
  format_line(json ? "hit.refresh" : "Cache hit refresh", stat->results.hits.refresh, stat->total, json, concise);
  format_line(json ? "hit.other" : "Cache hit other", stat->results.hits.other, stat->total, json, concise);
  format_line(json ? "hit.total" : "Cache hit total", stat->results.hits.total, stat->total, json, concise);

  if (!json) {
    std::cout << std::endl;
  }

  format_line(json ? "miss.direct" : "Cache miss", stat->results.misses.miss, stat->total, json, concise);
  format_line(json ? "miss.ims" : "Cache miss IMS", stat->results.misses.ims, stat->total, json, concise);
  format_line(json ? "miss.refresh" : "Cache miss refresh", stat->results.misses.refresh, stat->total, json, concise);
  format_line(json ? "miss.other" : "Cache miss other", stat->results.misses.other, stat->total, json, concise);
  format_line(json ? "miss.total" : "Cache miss total", stat->results.misses.total, stat->total, json, concise);

  if (!json) {
    std::cout << std::endl;
  }

  format_line(json ? "error.client_abort" : "Client aborted", stat->results.errors.client_abort, stat->total, json, concise);
  format_line(json ? "error.client_read_error" : "Client read error", stat->results.errors.client_read_error, stat->total, json,
              concise);
  format_line(json ? "error.connect_failed" : "Connect failed", stat->results.errors.connect_fail, stat->total, json, concise);
  format_line(json ? "error.invalid_request" : "Invalid request", stat->results.errors.invalid_req, stat->total, json, concise);
  format_line(json ? "error.unknown" : "Unknown error(99)", stat->results.errors.unknown, stat->total, json, concise);
  format_line(json ? "error.other" : "Other errors", stat->results.errors.other, stat->total, json, concise);
  format_line(json ? "error.total" : "Errors total", stat->results.errors.total, stat->total, json, concise);

  if (!json) {
    std::cout << std::setw(cl.line_len) << std::setfill('.') << '.' << std::setfill(' ') << std::endl;
    format_line("Total requests", stat->total, stat->total, json, concise);
    std::cout << std::endl << std::endl;

    // HTTP codes
    format_detail_header("HTTP return codes");
  }

  format_line(json ? "status.100" : "100 Continue", stat->codes.c_100, stat->total, json, concise);

  format_line(json ? "status.200" : "200 OK", stat->codes.c_200, stat->total, json, concise);
  format_line(json ? "status.201" : "201 Created", stat->codes.c_201, stat->total, json, concise);
  format_line(json ? "status.202" : "202 Accepted", stat->codes.c_202, stat->total, json, concise);
  format_line(json ? "status.203" : "203 Non-Authoritative Info", stat->codes.c_203, stat->total, json, concise);
  format_line(json ? "status.204" : "204 No content", stat->codes.c_204, stat->total, json, concise);
  format_line(json ? "status.205" : "205 Reset Content", stat->codes.c_205, stat->total, json, concise);
  format_line(json ? "status.206" : "206 Partial content", stat->codes.c_206, stat->total, json, concise);
  format_line(json ? "status.2xx" : "2xx Total", stat->codes.c_2xx, stat->total, json, concise);

  if (!json) {
    std::cout << std::endl;
  }

  format_line(json ? "status.300" : "300 Multiple Choices", stat->codes.c_300, stat->total, json, concise);
  format_line(json ? "status.301" : "301 Moved permanently", stat->codes.c_301, stat->total, json, concise);
  format_line(json ? "status.302" : "302 Found", stat->codes.c_302, stat->total, json, concise);
  format_line(json ? "status.303" : "303 See Other", stat->codes.c_303, stat->total, json, concise);
  format_line(json ? "status.304" : "304 Not modified", stat->codes.c_304, stat->total, json, concise);
  format_line(json ? "status.305" : "305 Use Proxy", stat->codes.c_305, stat->total, json, concise);
  format_line(json ? "status.307" : "307 Temporary Redirect", stat->codes.c_307, stat->total, json, concise);
  format_line(json ? "status.3xx" : "3xx Total", stat->codes.c_3xx, stat->total, json, concise);

  if (!json) {
    std::cout << std::endl;
  }

  format_line(json ? "status.400" : "400 Bad request", stat->codes.c_400, stat->total, json, concise);
  format_line(json ? "status.401" : "401 Unauthorized", stat->codes.c_401, stat->total, json, concise);
  format_line(json ? "status.402" : "402 Payment Required", stat->codes.c_402, stat->total, json, concise);
  format_line(json ? "status.403" : "403 Forbidden", stat->codes.c_403, stat->total, json, concise);
  format_line(json ? "status.404" : "404 Not found", stat->codes.c_404, stat->total, json, concise);
  format_line(json ? "status.405" : "405 Method Not Allowed", stat->codes.c_405, stat->total, json, concise);
  format_line(json ? "status.406" : "406 Not Acceptable", stat->codes.c_406, stat->total, json, concise);
  format_line(json ? "status.407" : "407 Proxy Auth Required", stat->codes.c_407, stat->total, json, concise);
  format_line(json ? "status.408" : "408 Request Timeout", stat->codes.c_408, stat->total, json, concise);
  format_line(json ? "status.409" : "409 Conflict", stat->codes.c_409, stat->total, json, concise);
  format_line(json ? "status.410" : "410 Gone", stat->codes.c_410, stat->total, json, concise);
  format_line(json ? "status.411" : "411 Length Required", stat->codes.c_411, stat->total, json, concise);
  format_line(json ? "status.412" : "412 Precondition Failed", stat->codes.c_412, stat->total, json, concise);
  format_line(json ? "status.413" : "413 Request Entity Too Large", stat->codes.c_413, stat->total, json, concise);
  format_line(json ? "status.414" : "414 Request-URI Too Long", stat->codes.c_414, stat->total, json, concise);
  format_line(json ? "status.415" : "415 Unsupported Media Type", stat->codes.c_415, stat->total, json, concise);
  format_line(json ? "status.416" : "416 Req Range Not Satisfiable", stat->codes.c_416, stat->total, json, concise);
  format_line(json ? "status.417" : "417 Expectation Failed", stat->codes.c_417, stat->total, json, concise);
  format_line(json ? "status.4xx" : "4xx Total", stat->codes.c_4xx, stat->total, json, concise);

  if (!json) {
    std::cout << std::endl;
  }

  format_line(json ? "status.500" : "500 Internal Server Error", stat->codes.c_500, stat->total, json, concise);
  format_line(json ? "status.501" : "501 Not implemented", stat->codes.c_501, stat->total, json, concise);
  format_line(json ? "status.502" : "502 Bad gateway", stat->codes.c_502, stat->total, json, concise);
  format_line(json ? "status.503" : "503 Service unavailable", stat->codes.c_503, stat->total, json, concise);
  format_line(json ? "status.504" : "504 Gateway Timeout", stat->codes.c_504, stat->total, json, concise);
  format_line(json ? "status.505" : "505 HTTP Ver. Not Supported", stat->codes.c_505, stat->total, json, concise);
  format_line(json ? "status.5xx" : "5xx Total", stat->codes.c_5xx, stat->total, json, concise);

  if (!json) {
    std::cout << std::endl;
  }

  format_line(json ? "status.000" : "000 Unknown", stat->codes.c_000, stat->total, json, concise);

  if (!json) {
    std::cout << std::endl << std::endl;

    // Origin hierarchies
    format_detail_header("Origin hierarchies");
  }

  format_line(json ? "hier.none" : "NONE", stat->hierarchies.none, stat->total, json, concise);
  format_line(json ? "hier.direct" : "DIRECT", stat->hierarchies.direct, stat->total, json, concise);
  format_line(json ? "hier.sibling" : "SIBLING", stat->hierarchies.sibling, stat->total, json, concise);
  format_line(json ? "hier.parent" : "PARENT", stat->hierarchies.parent, stat->total, json, concise);
  format_line(json ? "hier.empty" : "EMPTY", stat->hierarchies.empty, stat->total, json, concise);
  format_line(json ? "hier.invalid" : "invalid", stat->hierarchies.invalid, stat->total, json, concise);
  format_line(json ? "hier.other" : "other", stat->hierarchies.other, stat->total, json, concise);

  if (!json) {
    std::cout << std::endl << std::endl;

    // HTTP methods
    format_detail_header("HTTP Methods");
  }

  format_line(json ? "method.options" : "OPTIONS", stat->methods.options, stat->total, json, concise);
  format_line(json ? "method.get" : "GET", stat->methods.get, stat->total, json, concise);
  format_line(json ? "method.head" : "HEAD", stat->methods.head, stat->total, json, concise);
  format_line(json ? "method.post" : "POST", stat->methods.post, stat->total, json, concise);
  format_line(json ? "method.put" : "PUT", stat->methods.put, stat->total, json, concise);
  format_line(json ? "method.delete" : "DELETE", stat->methods.del, stat->total, json, concise);
  format_line(json ? "method.trace" : "TRACE", stat->methods.trace, stat->total, json, concise);
  format_line(json ? "method.connect" : "CONNECT", stat->methods.connect, stat->total, json, concise);
  format_line(json ? "method.purge" : "PURGE", stat->methods.purge, stat->total, json, concise);
  format_line(json ? "method.none" : "none (-)", stat->methods.none, stat->total, json, concise);
  format_line(json ? "method.other" : "other", stat->methods.other, stat->total, json, concise);

  if (!json) {
    std::cout << std::endl << std::endl;

    // URL schemes (HTTP/HTTPs)
    format_detail_header("URL Schemes");
  }

  format_line(json ? "scheme.http" : "HTTP (port 80)", stat->schemes.http, stat->total, json, concise);
  format_line(json ? "scheme.https" : "HTTPS (port 443)", stat->schemes.https, stat->total, json, concise);
  format_line(json ? "scheme.none" : "none", stat->schemes.none, stat->total, json, concise);
  format_line(json ? "scheme.other" : "other", stat->schemes.other, stat->total, json, concise);

  if (!json) {
    std::cout << std::endl << std::endl;

    // Protocol familes
    format_detail_header("Protocols");
  }

  format_line(json ? "proto.ipv4" : "IPv4", stat->protocols.ipv4, stat->total, json, concise);
  format_line(json ? "proto.ipv6" : "IPv6", stat->protocols.ipv6, stat->total, json, concise);

  if (!json) {
    std::cout << std::endl << std::endl;

    // Content types
    format_detail_header("Content Types");
  }

  format_line(json ? "content.text.javascript" : "text/javascript", stat->content.text.javascript, stat->total, json, concise);
  format_line(json ? "content.text.css" : "text/css", stat->content.text.css, stat->total, json, concise);
  format_line(json ? "content.text.html" : "text/html", stat->content.text.html, stat->total, json, concise);
  format_line(json ? "content.text.xml" : "text/xml", stat->content.text.xml, stat->total, json, concise);
  format_line(json ? "content.text.plain" : "text/plain", stat->content.text.plain, stat->total, json, concise);
  format_line(json ? "content.text.other" : "text/ other", stat->content.text.other, stat->total, json, concise);
  format_line(json ? "content.text.total" : "text/ total", stat->content.text.total, stat->total, json, concise);

  if (!json) {
    std::cout << std::endl;
  }

  format_line(json ? "content.image.jpeg" : "image/jpeg", stat->content.image.jpeg, stat->total, json, concise);
  format_line(json ? "content.image.gif" : "image/gif", stat->content.image.gif, stat->total, json, concise);
  format_line(json ? "content.image.png" : "image/png", stat->content.image.png, stat->total, json, concise);
  format_line(json ? "content.image.bmp" : "image/bmp", stat->content.image.bmp, stat->total, json, concise);
  format_line(json ? "content.image.other" : "image/ other", stat->content.image.other, stat->total, json, concise);
  format_line(json ? "content.image.total" : "image/ total", stat->content.image.total, stat->total, json, concise);

  if (!json) {
    std::cout << std::endl;
  }

  format_line(json ? "content.audio.x-wav" : "audio/x-wav", stat->content.audio.wav, stat->total, json, concise);
  format_line(json ? "content.audio.x-mpeg" : "audio/x-mpeg", stat->content.audio.mpeg, stat->total, json, concise);
  format_line(json ? "content.audio.other" : "audio/ other", stat->content.audio.other, stat->total, json, concise);
  format_line(json ? "content.audio.total" : "audio/ total", stat->content.audio.total, stat->total, json, concise);

  if (!json) {
    std::cout << std::endl;
  }

  format_line(json ? "content.application.shockwave" : "application/x-shockwave", stat->content.application.shockwave_flash,
              stat->total, json, concise);
  format_line(json ? "content.application.javascript" : "application/[x-]javascript", stat->content.application.javascript,
              stat->total, json, concise);
  format_line(json ? "content.application.quicktime" : "application/x-quicktime", stat->content.application.quicktime, stat->total,
              json, concise);
  format_line(json ? "content.application.zip" : "application/zip", stat->content.application.zip, stat->total, json, concise);
  format_line(json ? "content.application.rss_xml" : "application/rss+xml", stat->content.application.rss_xml, stat->total, json,
              concise);
  format_line(json ? "content.application.rss_atom" : "application/rss+atom", stat->content.application.rss_atom, stat->total, json,
              concise);
  format_line(json ? "content.application.other" : "application/ other", stat->content.application.other, stat->total, json,
              concise);
  format_line(json ? "content.application.total" : "application/ total", stat->content.application.total, stat->total, json,
              concise);

  if (!json) {
    std::cout << std::endl;
  }

  format_line(json ? "content.none" : "none", stat->content.none, stat->total, json, concise);
  format_line(json ? "content.other" : "other", stat->content.other, stat->total, json, concise);

  if (!json) {
    std::cout << std::endl << std::endl;

    // Elapsed time
    format_elapsed_header();
  }

  format_elapsed_line(json ? "hit.direct.latency" : "Cache hit", stat->elapsed.hits.hit, json, concise);
  format_elapsed_line(json ? "hit.ram.latency" : "Cache hit RAM", stat->elapsed.hits.hit_ram, json, concise);
  format_elapsed_line(json ? "hit.ims.latency" : "Cache hit IMS", stat->elapsed.hits.ims, json, concise);
  format_elapsed_line(json ? "hit.refresh.latency" : "Cache hit refresh", stat->elapsed.hits.refresh, json, concise);
  format_elapsed_line(json ? "hit.other.latency" : "Cache hit other", stat->elapsed.hits.other, json, concise);
  format_elapsed_line(json ? "hit.total.latency" : "Cache hit total", stat->elapsed.hits.total, json, concise);

  format_elapsed_line(json ? "miss.direct.latency" : "Cache miss", stat->elapsed.misses.miss, json, concise);
  format_elapsed_line(json ? "miss.ims.latency" : "Cache miss IMS", stat->elapsed.misses.ims, json, concise);
  format_elapsed_line(json ? "miss.refresh.latency" : "Cache miss refresh", stat->elapsed.misses.refresh, json, concise);
  format_elapsed_line(json ? "miss.other.latency" : "Cache miss other", stat->elapsed.misses.other, json, concise);
  format_elapsed_line(json ? "miss.total.latency" : "Cache miss total", stat->elapsed.misses.total, json, concise);

  if (!json) {
    std::cout << std::endl;
    std::cout << std::setw(cl.line_len) << std::setfill('_') << '_' << std::setfill(' ') << std::endl;
  } else {
    std::cout << "    \"_timestamp\" : \"" << static_cast<int>(ink_time_wall_seconds()) << '"' << std::endl;
  }
}

///////////////////////////////////////////////////////////////////////////////
// Little wrapper around exit, to allow us to exit gracefully
void
my_exit(const ExitStatus &status)
{
  vector<OriginPair> vec;
  bool first = true;
  int max_origins;

  // Special case for URLs output.
  if (urls) {
    urls->dump(cl.as_object);
    if (cl.as_object) {
      std::cout << "}" << std::endl;
    } else {
      std::cout << "]" << std::endl;
    }
    ::exit(status.level);
  }

  if (cl.json) {
    // TODO: produce output
  } else {
    switch (status.level) {
    case EXIT_OK:
      break;
    case EXIT_WARNING:
      std::cout << "warning: " << status.notice << std::endl;
      break;
    case EXIT_CRITICAL:
      std::cout << "critical: " << status.notice << std::endl;
      ::exit(status.level);
      break;
    case EXIT_UNKNOWN:
      std::cout << "unknown: " << status.notice << std::endl;
      ::exit(status.level);
      break;
    }
  }

  if (!origins.empty()) {
    // Sort the Origins by 'traffic'
    for (OriginStorage::iterator i = origins.begin(); i != origins.end(); i++) {
      if (use_origin(i->second)) {
        vec.push_back(*i);
      }
    }
    sort(vec.begin(), vec.end());

    if (!cl.json) {
      // Produce a nice summary first
      format_center("Traffic summary");
      std::cout << std::left << std::setw(33) << "Origin Server";
      std::cout << std::right << std::setw(15) << "Hits";
      std::cout << std::right << std::setw(15) << "Misses";
      std::cout << std::right << std::setw(15) << "Errors" << std::endl;
      std::cout << std::setw(cl.line_len) << std::setfill('-') << '-' << std::setfill(' ') << std::endl;

      max_origins = cl.max_origins > 0 ? cl.max_origins : INT_MAX;
      for (vector<OriginPair>::iterator i = vec.begin(); (i != vec.end()) && (max_origins > 0); ++i, --max_origins) {
        std::cout << std::left << std::setw(33) << i->first;
        std::cout << std::right << std::setw(15);
        format_int(i->second->results.hits.total.count);
        std::cout << std::right << std::setw(15);
        format_int(i->second->results.misses.total.count);
        std::cout << std::right << std::setw(15);
        format_int(i->second->results.errors.total.count);
        std::cout << std::endl;
      }
      std::cout << std::setw(cl.line_len) << std::setfill('=') << '=' << std::setfill(' ') << std::endl;
      std::cout << std::endl << std::endl << std::endl;
    }
  }

  // Next the totals for all Origins, unless we specified a list of origins to filter.
  if (origin_set->empty()) {
    first = false;
    if (cl.json) {
      std::cout << "{ \"total\": {" << std::endl;
      print_detail_stats(&totals, cl.json, cl.concise);
      std::cout << "  }";
    } else {
      format_center("Totals (all Origins combined)");
      print_detail_stats(&totals, cl.json, cl.concise);
      std::cout << std::endl << std::endl << std::endl;
    }
  }

  // And finally the individual Origin Servers.
  max_origins = cl.max_origins > 0 ? cl.max_origins : INT_MAX;
  for (vector<OriginPair>::iterator i = vec.begin(); (i != vec.end()) && (max_origins > 0); ++i, --max_origins) {
    if (cl.json) {
      if (first) {
        std::cout << "{ ";
        first = false;
      } else {
        std::cout << "," << std::endl << "  ";
      }
      std::cout << '"' << i->first << "\": {" << std::endl;
      print_detail_stats(i->second, cl.json, cl.concise);
      std::cout << "  }";
    } else {
      format_center(i->first);
      print_detail_stats(i->second, cl.json, cl.concise);
      std::cout << std::endl << std::endl << std::endl;
    }
  }

  if (cl.json) {
    std::cout << std::endl << "}" << std::endl;
  }

  ::exit(status.level);
}

///////////////////////////////////////////////////////////////////////////////
// Open the "default" log file (squid.blog), allow for it to be rotated.
int
open_main_log(ExitStatus &status)
{
  std::string logfile(Layout::get()->logdir);
  int cnt = 3;
  int main_fd;

  logfile.append("/squid.blog");
  while (((main_fd = open(logfile.c_str(), O_RDONLY)) < 0) && --cnt) {
    switch (errno) {
    case ENOENT:
    case EACCES:
      sleep(5);
      break;
    default:
      status.append(" can't open squid.blog");
      return -1;
    }
  }

  if (main_fd < 0) {
    status.append(" squid.blog not enabled");
    return -1;
  }
#if HAVE_POSIX_FADVISE
  if (0 != posix_fadvise(main_fd, 0, 0, POSIX_FADV_DONTNEED)) {
    status.append(" posix_fadvise() failed");
  }
#endif
  return main_fd;
}

///////////////////////////////////////////////////////////////////////////////
// main
int
main(int /* argc ATS_UNUSED */, const char *argv[])
{
  ExitStatus exit_status;
  int res, cnt;
  int main_fd;
  unsigned max_age;
  struct flock lck;

  // build the application information structure
  appVersionInfo.setup(PACKAGE_NAME, PROGRAM_NAME, PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");

  runroot_handler(argv);
  // Before accessing file system initialize Layout engine
  Layout::create();

  memset(&totals, 0, sizeof(totals));
  init_elapsed(&totals);

  origin_set   = new OriginSet;
  parse_errors = 0;

  // Command line parsing
  cl.parse_arguments(argv);

  // Calculate the max age of acceptable log entries, if necessary
  if (cl.max_age > 0) {
    struct timeval tv;

    gettimeofday(&tv, nullptr);
    max_age = tv.tv_sec - cl.max_age;
  } else {
    max_age = 0;
  }

  // initialize this application for standalone logging operation
  init_log_standalone_basic(PROGRAM_NAME);
  Log::init(Log::NO_REMOTE_MANAGEMENT | Log::LOGCAT);

  // Do we have a list of Origins on the command line?
  if (cl.origin_list[0] != '\0') {
    char *tok;
    char *sep_ptr;

    for (tok = strtok_r(cl.origin_list, ",", &sep_ptr); tok != nullptr;) {
      origin_set->insert(tok);
      tok = strtok_r(nullptr, ",", &sep_ptr);
    }
  }
  // Load origins from an "external" file (\n separated)
  if (cl.origin_file[0] != '\0') {
    std::ifstream fs;

    fs.open(cl.origin_file, std::ios::in);
    if (!fs.is_open()) {
      std::cerr << "can't read " << cl.origin_file << std::endl;
      usage(argument_descriptions, countof(argument_descriptions), USAGE_LINE);
      ::exit(0);
    }

    while (!fs.eof()) {
      std::string line;
      std::string::size_type start, end;

      getline(fs, line);
      start = line.find_first_not_of(" \t");
      if (start != std::string::npos) {
        end = line.find_first_of(" \t#/");
        if (std::string::npos == end) {
          end = line.length();
        }

        if (end > start) {
          char *buf;

          buf = ats_stringdup(line.substr(start, end));
          if (buf) {
            origin_set->insert(buf);
          }
        }
      }
    }
  }

  // Produce the CGI header first (if applicable)
  if (cl.cgi) {
    std::cout << "Content-Type: application/javascript\r\n";
    std::cout << "Cache-Control: no-cache\r\n\r\n";
  }

  // Should we calculate per URL data;
  if (cl.urls != 0) {
    urls = new UrlLru(cl.urls, cl.show_urls);
    if (cl.as_object) {
      std::cout << "{" << std::endl;
    } else {
      std::cout << "[" << std::endl;
    }
  }

  // Do the incremental parse of the default squid log.
  if (cl.incremental) {
    // Change directory to the log dir
    if (chdir(Layout::get()->logdir.c_str()) < 0) {
      exit_status.set(EXIT_CRITICAL, " can't chdir to ");
      exit_status.append(Layout::get()->logdir);
      my_exit(exit_status);
    }

    std::string sf_name(Layout::get()->logdir);
    struct stat stat_buf;
    int state_fd;
    sf_name.append("/logstats.state");

    if (cl.state_tag[0] != '\0') {
      sf_name.append(".");
      sf_name.append(cl.state_tag);
    } else {
      // Default to the username
      struct passwd *pwd = getpwuid(geteuid());

      if (pwd) {
        sf_name.append(".");
        sf_name.append(pwd->pw_name);
      } else {
        exit_status.set(EXIT_CRITICAL, " can't get current UID");
        my_exit(exit_status);
      }
    }

    if ((state_fd = open(sf_name.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR)) < 0) {
      exit_status.set(EXIT_CRITICAL, " can't open state file ");
      exit_status.append(sf_name);
      my_exit(exit_status);
    }
    // Get an exclusive lock, if possible. Try for up to 20 seconds.
    // Use more portable & standard fcntl() over flock()
    lck.l_type   = F_WRLCK;
    lck.l_whence = 0; /* offset l_start from beginning of file*/
    lck.l_start  = (off_t)0;
    lck.l_len    = (off_t)0; /* till end of file*/
    cnt          = 10;
    while (((res = fcntl(state_fd, F_SETLK, &lck)) < 0) && --cnt) {
      switch (errno) {
      case EWOULDBLOCK:
      case EINTR:
        sleep(2);
        break;
      default:
        exit_status.set(EXIT_CRITICAL, " locking failure");
        my_exit(exit_status);
        break;
      }
    }

    if (res < 0) {
      exit_status.set(EXIT_CRITICAL, " can't lock state file");
      my_exit(exit_status);
    }
    // Fetch previous state information, allow for concurrent accesses.
    cnt = 10;
    while (((res = read(state_fd, &last_state, sizeof(last_state))) < 0) && --cnt) {
      switch (errno) {
      case EINTR:
      case EAGAIN:
        sleep(1);
        break;
      default:
        exit_status.set(EXIT_CRITICAL, " can't read state file");
        my_exit(exit_status);
        break;
      }
    }

    if (res != sizeof(last_state)) {
      // First time / empty file, so reset.
      last_state.offset = 0;
      last_state.st_ino = 0;
    }

    if ((main_fd = open_main_log(exit_status)) < 0) {
      exit_status.set(EXIT_CRITICAL);
      my_exit(exit_status);
    }

    // Get stat's from the main log file.
    if (fstat(main_fd, &stat_buf) < 0) {
      exit_status.set(EXIT_CRITICAL, " can't stat squid.blog");
      my_exit(exit_status);
    }
    // Make sure the last_state.st_ino is sane.
    if (last_state.st_ino <= 0) {
      last_state.st_ino = stat_buf.st_ino;
    }

    // Check if the main log file was rotated, and if so, locate
    // the old file first, and parse the remaining log data.
    if (stat_buf.st_ino != last_state.st_ino) {
      DIR *dirp         = nullptr;
      struct dirent *dp = nullptr;
      ino_t old_inode   = last_state.st_ino;

      // Save the current log-file's I-Node number.
      last_state.st_ino = stat_buf.st_ino;

      // Find the old log file.
      dirp = opendir(Layout::get()->logdir.c_str());
      if (nullptr == dirp) {
        exit_status.set(EXIT_WARNING, " can't read log directory");
      } else {
        while ((dp = readdir(dirp)) != nullptr) {
          // coverity[fs_check_call]
          if (stat(dp->d_name, &stat_buf) < 0) {
            exit_status.set(EXIT_WARNING, " can't stat ");
            exit_status.append(dp->d_name);
          } else if (stat_buf.st_ino == old_inode) {
            int old_fd = open(dp->d_name, O_RDONLY);

            if (old_fd < 0) {
              exit_status.set(EXIT_WARNING, " can't open ");
              exit_status.append(dp->d_name);
              break; // Don't attempt any more files
            }
            // Process it
            if (process_file(old_fd, last_state.offset, max_age) != 0) {
              exit_status.set(EXIT_WARNING, " can't read ");
              exit_status.append(dp->d_name);
            }
            close(old_fd);
            break; // Don't attempt any more files
          }
        }
      }
      // Make sure to read from the beginning of the freshly rotated file.
      last_state.offset = 0;
    } else {
      // Make sure the last_state.offset is sane, stat_buf is for the main_fd.
      if (last_state.offset > stat_buf.st_size) {
        last_state.offset = stat_buf.st_size;
      }
    }

    // Process the main file (always)
    if (process_file(main_fd, last_state.offset, max_age) != 0) {
      exit_status.set(EXIT_CRITICAL, " can't parse log");
      last_state.offset = 0;
      last_state.st_ino = 0;
    } else {
      // Save the current file offset.
      last_state.offset = lseek(main_fd, 0, SEEK_CUR);
      if (last_state.offset < 0) {
        exit_status.set(EXIT_WARNING, " can't lseek squid.blog");
        last_state.offset = 0;
      }
    }

    // Save the state, release the lock, and close the FDs.
    if (lseek(state_fd, 0, SEEK_SET) < 0) {
      exit_status.set(EXIT_WARNING, " can't lseek state file");
    } else {
      if (-1 == write(state_fd, &last_state, sizeof(last_state))) {
        exit_status.set(EXIT_WARNING, " can't write state_fd ");
      }
    }
    // flock(state_fd, LOCK_UN);
    lck.l_type = F_UNLCK;
    if (fcntl(state_fd, F_SETLK, &lck) < 0) {
      exit_status.set(EXIT_WARNING, " can't unlock state_fd ");
    }
    close(main_fd);
    close(state_fd);
  } else {
    main_fd = cl.log_file[0] ? open(cl.log_file, O_RDONLY) : open_main_log(exit_status);
    if (main_fd < 0) {
      exit_status.set(EXIT_CRITICAL, " can't open log file ");
      exit_status.append(cl.log_file);
      my_exit(exit_status);
    }

    if (cl.tail > 0) {
      if (lseek(main_fd, 0, SEEK_END) < 0) {
        exit_status.set(EXIT_CRITICAL, " can't lseek squid.blog");
        my_exit(exit_status);
      }
      sleep(cl.tail);
    }

    if (process_file(main_fd, 0, max_age) != 0) {
      close(main_fd);
      exit_status.set(EXIT_CRITICAL, " can't parse log file ");
      exit_status.append(cl.log_file);
      my_exit(exit_status);
    }
    close(main_fd);
  }

  // All done.
  if (EXIT_OK == exit_status.level) {
    exit_status.append(" OK");
  }
  my_exit(exit_status);
}
