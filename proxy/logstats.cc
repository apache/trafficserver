/** @file

  A brief file description

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

#include "ink_config.h"
#include "ink_file.h"
#include "ink_unused.h"
#include "I_Layout.h"
#include "I_Version.h"

// Includes and namespaces etc.
#include "LogStandalone.cc"

#include "LogObject.h"
#include "hdrs/HTTP.h"

#include <math.h>
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
#if (__GNUC__ >= 3)
#define _BACKWARD_BACKWARD_WARNING_H    // needed for gcc 4.3
#include <ext/hash_map>
#include <ext/hash_set>
#undef _BACKWARD_BACKWARD_WARNING_H
#else
#include <hash_map>
#include <hash_set>
#include <map>
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#include <fcntl.h>

#if defined(__GNUC__)
using namespace __gnu_cxx;
#endif
using namespace std;

// Constants, please update the VERSION number when you make a new build!!!
#define PROGRAM_NAME		"traffic_logstats"

const int MAX_LOGBUFFER_SIZE = 65536;
const int DEFAULT_LINE_LEN = 78;
const double LOG10_1024 = 3.0102999566398116;

// Optimizations for "strcmp()", treat some fixed length (3 or 4 bytes) strings
// as integers.
const int GET_AS_INT = 5522759;
const int PUT_AS_INT = 5526864;
const int HEAD_AS_INT = 1145128264;
const int POST_AS_INT = 1414745936;

const int TEXT_AS_INT = 1954047348;

const int JPEG_AS_INT = 1734701162;
const int JPG_AS_INT = 6778986;
const int GIF_AS_INT = 6711655;
const int PNG_AS_INT = 6778480;
const int BMP_AS_INT = 7368034;
const int CSS_AS_INT = 7566179;
const int XML_AS_INT = 7105912;
const int HTML_AS_INT = 1819112552;
const int ZIP_AS_INT = 7367034;

const int JAVA_AS_INT = 1635148138;     // For "javascript"
const int PLAI_AS_INT = 1767992432;     // For "plain"
const int IMAG_AS_INT = 1734438249;     // For "image"
const int HTTP_AS_INT = 1886680168;     // For "http" followed by "s://" or "://"


// Store our "state" (position in log file etc.)
struct LastState
{
  off_t offset;
  ino_t st_ino;
};
LastState last_state;


// Store the collected counters and stats, per Origin Server (or total)
struct StatsCounter
{
  int64 count;
  int64 bytes;
};

struct ElapsedStats
{
  int min;
  int max;
  float avg;
  float stddev;
};

struct OriginStats
{
  char *server;
  StatsCounter total;

  struct
  {
    struct
    {
      ElapsedStats hit;
      ElapsedStats ims;
      ElapsedStats refresh;
      ElapsedStats other;
      ElapsedStats total;
    } hits;
    struct
    {
      ElapsedStats miss;
      ElapsedStats ims;
      ElapsedStats refresh;
      ElapsedStats other;
      ElapsedStats total;
    } misses;
  } elapsed;

  struct
  {
    struct
    {
      StatsCounter hit;
      StatsCounter ims;
      StatsCounter refresh;
      StatsCounter other;
      StatsCounter total;
    } hits;
    struct
    {
      StatsCounter miss;
      StatsCounter ims;
      StatsCounter refresh;
      StatsCounter other;
      StatsCounter total;
    } misses;
    struct
    {
      StatsCounter client_abort;
      StatsCounter connect_fail;
      StatsCounter invalid_req;
      StatsCounter unknown;
      StatsCounter other;
      StatsCounter total;
    } errors;
    StatsCounter other;
  } results;

  struct
  {
    StatsCounter c_000;         // Bad
    StatsCounter c_200;
    StatsCounter c_204;
    StatsCounter c_206;
    StatsCounter c_2xx;
    StatsCounter c_301;
    StatsCounter c_302;
    StatsCounter c_304;
    StatsCounter c_3xx;
    StatsCounter c_400;
    StatsCounter c_403;
    StatsCounter c_404;
    StatsCounter c_4xx;
    StatsCounter c_501;
    StatsCounter c_502;
    StatsCounter c_503;
    StatsCounter c_5xx;
    StatsCounter c_999;         // YDoD, very bad
  } codes;

  struct
  {
    StatsCounter direct;
    StatsCounter none;
    StatsCounter sibling;
    StatsCounter parent;
    StatsCounter empty;
    StatsCounter invalid;
    StatsCounter other;
  } hierarchies;

  struct
  {
    StatsCounter http;
    StatsCounter https;
    StatsCounter none;
    StatsCounter other;
  } schemes;

  struct
  {
    StatsCounter get;
    StatsCounter put;
    StatsCounter head;
    StatsCounter post;
    StatsCounter del;
    StatsCounter purge;
    StatsCounter options;
    StatsCounter none;
    StatsCounter other;
  } methods;

  struct
  {
    struct
    {
      StatsCounter plain;
      StatsCounter xml;
      StatsCounter html;
      StatsCounter css;
      StatsCounter javascript;
      StatsCounter other;
      StatsCounter total;
    } text;
    struct
    {
      StatsCounter jpeg;
      StatsCounter gif;
      StatsCounter png;
      StatsCounter bmp;
      StatsCounter other;
      StatsCounter total;
    } image;
    struct
    {
      StatsCounter shockwave_flash;
      StatsCounter quicktime;
      StatsCounter javascript;
      StatsCounter zip;
      StatsCounter other;
      StatsCounter total;
    } application;
    struct
    {
      StatsCounter wav;
      StatsCounter mpeg;
      StatsCounter other;
      StatsCounter total;
    } audio;
    StatsCounter none;
    StatsCounter other;
  } content;
};


///////////////////////////////////////////////////////////////////////////////
// Equal operator for char* (for the hash_map)
struct eqstr
{
  inline bool operator() (const char *s1, const char *s2) const
  {
    return strcmp(s1, s2) == 0;
  }
};

typedef hash_map < const char *, OriginStats *, hash < const char *>, eqstr > OriginStorage;
typedef hash_set < const char *, hash < const char *>, eqstr > OriginSet;


///////////////////////////////////////////////////////////////////////////////
// Globals, holding the accumulated stats (ok, I'm lazy ...)
static OriginStats totals;
static OriginStorage origins;
static OriginSet *origin_set;
static int parse_errors;
static char *hostname;

// Command line arguments (parsing)
static struct
{
  char log_file[1024];
  char origin_file[1024];
  char origin_list[2048];
  char state_tag[1024];
  int64 min_hits;
  int max_age;
  int line_len;
  int incremental;              // Do an incremental run
  int tail;                     // Tail the log file
  int ymon;                     // Report in ymon format
  int ysar;                     // Report in ysar format
  int summary;                  // Summary only
  int version;
  int help;
} cl;

ArgumentDescription argument_descriptions[] = {
  {"help", 'h', "Give this help", "T", &cl.help, NULL, NULL},
  {"log_file", 'f', "Specific logfile to parse", "S1023", cl.log_file, NULL, NULL},
  {"origin_list", 'o', "Only show stats for listed Origins", "S2047", cl.origin_list, NULL, NULL},
  {"origin_file", 'O', "File listing Origins to show", "S1023", cl.origin_file, NULL, NULL},
  {"incremental", 'i', "Incremental log parsing", "T", &cl.incremental, NULL, NULL},
  {"statetag", 'S', "Name of the state file to use", "S1023", cl.state_tag, NULL, NULL},
  {"tail", 't', "Parse the last <sec> seconds of log", "I", &cl.tail, NULL, NULL},
  {"summary", 's', "Only produce the summary", "T", &cl.summary, NULL, NULL},
  {"ymon", 'y', "Output is formatted for YMon/Nagios", "T", &cl.ymon, NULL, NULL},
  {"ysar", 'Y', "Output is formatted for YSAR", "T", &cl.ysar, NULL, NULL},
  {"min_hits", 'm', "Minimum total hits for an Origin", "L", &cl.min_hits, NULL, NULL},
  {"max_age", 'a', "Max age for log entries to be considered", "I", &cl.max_age, NULL, NULL},
  {"line_len", 'l', "Output line length", "I", &cl.line_len, NULL, NULL},
  {"debug_tags", 'T', "Colon-Separated Debug Tags", "S1023", &error_tags, NULL, NULL},
  {"version", 'V', "Print Version Id", "T", &cl.version, NULL, NULL},
};
int n_argument_descriptions = SIZE(argument_descriptions);

static const char *USAGE_LINE =
  "Usage: " PROGRAM_NAME " [-l logfile] [-o origin[,...]] [-O originfile] [-m minhits] [-inshv]";


// Enum for YMON return code levels.
enum YmonLevel
{
  YMON_OK = 0,
  YMON_WARNING = 1,
  YMON_CRITICAL = 2,
  YMON_UNKNOWN = 3
};

// Enum for parsing a log line
enum ParseStates
{
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
enum HTTPMethod
{
  METHOD_GET,
  METHOD_PUT,
  METHOD_HEAD,
  METHOD_POST,
  METHOD_PURGE,
  METHOD_DELETE,
  METHOD_OPTIONS,
  METHOD_NONE,
  METHOD_OTHER
};

// Enum for URL schemes
enum URLScheme
{
  SCHEME_HTTP,
  SCHEME_HTTPS,
  SCHEME_NONE,
  SCHEME_OTHER
};



///////////////////////////////////////////////////////////////////////////////
// Initialize the elapsed field
inline void
init_elapsed(OriginStats & stats)
{
  stats.elapsed.hits.hit.min = -1;
  stats.elapsed.hits.ims.min = -1;
  stats.elapsed.hits.refresh.min = -1;
  stats.elapsed.hits.other.min = -1;
  stats.elapsed.hits.total.min = -1;
  stats.elapsed.misses.miss.min = -1;
  stats.elapsed.misses.ims.min = -1;
  stats.elapsed.misses.refresh.min = -1;
  stats.elapsed.misses.other.min = -1;
  stats.elapsed.misses.total.min = -1;
}

// Update the counters for one StatsCounter
inline void
update_counter(StatsCounter & counter, int size)
{
  counter.count++;
  counter.bytes += size;
}

inline void
update_elapsed(ElapsedStats & stat, const int elapsed, const StatsCounter & counter)
{
  int newcount, oldcount;
  float oldavg, newavg, sum_of_squares;
  // Skip all the "0" values.
  if (elapsed == 0)
    return;
  if (stat.min == -1)
    stat.min = elapsed;
  else if (stat.min > elapsed)
    stat.min = elapsed;

  if (stat.max < elapsed)
    stat.max = elapsed;

  // update_counter should have been called on counter.count before calling
  // update_elapsed.
  newcount = counter.count;
  // New count should never be zero, else there was a programming error.
  assert(newcount);
  oldcount = counter.count - 1;
  oldavg = stat.avg;
  newavg = (oldavg * oldcount + elapsed) / newcount;
  // Now find the new standard deviation from the old one

  if (oldcount != 0)
    sum_of_squares = (stat.stddev * stat.stddev * oldcount);
  else
    sum_of_squares = 0;

  //Find the old sum of squares.
  sum_of_squares = sum_of_squares + 2 * oldavg * oldcount * (oldavg - newavg)
    + oldcount * (newavg * newavg - oldavg * oldavg);

  //Now, find the new sum of squares.
  sum_of_squares = sum_of_squares + (elapsed - newavg) * (elapsed - newavg);

  stat.stddev = sqrt(sum_of_squares / newcount);
  stat.avg = newavg;

}

///////////////////////////////////////////////////////////////////////////////
// Update the "result" and "elapsed" stats for a particular record
inline void
update_results_elapsed(OriginStats * stat, int result, int elapsed, int size)
{
  switch (result) {
  case SQUID_LOG_TCP_HIT:
    update_counter(stat->results.hits.hit, size);
    update_counter(stat->results.hits.total, size);
    update_elapsed(stat->elapsed.hits.hit, elapsed, stat->results.hits.hit);
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
  case SQUID_LOG_ERR_CLIENT_ABORT:
    update_counter(stat->results.errors.client_abort, size);
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
  case SQUID_LOG_TCP_DISK_HIT:
  case SQUID_LOG_TCP_MEM_HIT:
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
  default:
    if ((result >= SQUID_LOG_ERR_READ_TIMEOUT) && (result <= SQUID_LOG_ERR_UNKNOWN)) {
      update_counter(stat->results.errors.other, size);
      update_counter(stat->results.errors.total, size);
    } else
      update_counter(stat->results.other, size);
    break;
  }
}


///////////////////////////////////////////////////////////////////////////////
// Update the "codes" stats for a particular record
inline void
update_codes(OriginStats * stat, int code, int size)
{
  // Special case for "200", most common.
  if (code == 200)
    update_counter(stat->codes.c_200, size);
  else if ((code > 199) && (code < 299)) {
    if (code == 204)
      update_counter(stat->codes.c_204, size);
    else if (code == 206)
      update_counter(stat->codes.c_206, size);
    else
      update_counter(stat->codes.c_2xx, size);
  } else if ((code > 299) && (code < 399)) {
    if (code == 301)
      update_counter(stat->codes.c_301, size);
    else if (code == 302)
      update_counter(stat->codes.c_302, size);
    else if (code == 304)
      update_counter(stat->codes.c_304, size);
    else
      update_counter(stat->codes.c_3xx, size);
  } else if ((code > 399) && (code < 499)) {
    if (code == 400)
      update_counter(stat->codes.c_400, size);
    else if (code == 403)
      update_counter(stat->codes.c_403, size);
    else if (code == 404)
      update_counter(stat->codes.c_404, size);
    else
      update_counter(stat->codes.c_4xx, size);
  } else if ((code > 499) && (code < 599)) {
    if (code == 501)
      update_counter(stat->codes.c_501, size);
    else if (code == 502)
      update_counter(stat->codes.c_502, size);
    else if (code == 503)
      update_counter(stat->codes.c_503, size);
    else
      update_counter(stat->codes.c_5xx, size);
  } else if (code == 999)
    update_counter(stat->codes.c_999, size);
  else if (code == 0)
    update_counter(stat->codes.c_000, size);
}


///////////////////////////////////////////////////////////////////////////////
// Update the "methods" stats for a particular record
inline void
update_methods(OriginStats * stat, int method, int size)
{
  // We're so loppsides on GETs, so makes most sense to test 'out of order'.
  if (method == METHOD_GET)
    update_counter(stat->methods.get, size);
  else if (method == METHOD_PUT)
    update_counter(stat->methods.put, size);
  else if (method == METHOD_HEAD)
    update_counter(stat->methods.head, size);
  else if (method == METHOD_POST)
    update_counter(stat->methods.post, size);
  else if (method == METHOD_DELETE)
    update_counter(stat->methods.del, size);
  else if (method == METHOD_PURGE)
    update_counter(stat->methods.purge, size);
  else if (method == METHOD_OPTIONS)
    update_counter(stat->methods.options, size);
  else if (method == METHOD_NONE)
    update_counter(stat->methods.none, size);
  else
    update_counter(stat->methods.other, size);
}


///////////////////////////////////////////////////////////////////////////////
// Update the "schemes" stats for a particular record
inline void
update_schemes(OriginStats * stat, int scheme, int size)
{
  if (scheme == SCHEME_HTTP)
    update_counter(stat->schemes.http, size);
  else if (scheme == SCHEME_HTTPS)
    update_counter(stat->schemes.https, size);
  else if (scheme == SCHEME_NONE)
    update_counter(stat->schemes.none, size);
  else
    update_counter(stat->schemes.other, size);
}


///////////////////////////////////////////////////////////////////////////////
// Parse a log buffer
int
parse_log_buff(LogBufferHeader * buf_header, bool summary = false)
{
  static char *str_buf = NULL;
  static LogFieldList *fieldlist = NULL;

  LogEntryHeader *entry;
  LogBufferIterator buf_iter(buf_header);
  LogField *field;
  OriginStorage::iterator o_iter;
  ParseStates state;

  char *read_from;
  char *tok;
  char *ptr;
  int tok_len;
  int flag = 0;                 // Flag used in state machine to carry "state" forward

  // Parsed results
  int http_code = 0, size = 0, result = 0, hier = 0, elapsed = 0;
  OriginStats *o_stats;
  char *o_server;
  HTTPMethod method;
  URLScheme scheme;


  // Initialize some "static" variables.
  if (str_buf == NULL) {
    str_buf = (char *) xmalloc(LOG_MAX_FORMATTED_LINE);
    if (str_buf == NULL)
      return 0;
  }

  if (!fieldlist) {
    fieldlist = NEW(new LogFieldList);
    ink_assert(fieldlist != NULL);
    bool agg = false;
    LogFormat::parse_symbol_string(buf_header->fmt_fieldlist(), fieldlist, &agg);
  }
  // Loop over all entries
  while ((entry = buf_iter.next())) {
    read_from = (char *) entry + sizeof(LogEntryHeader);
    // We read and skip over the first field, which is the timestamp.
    if ((field = fieldlist->first()))
      read_from += INK_MIN_ALIGN;
    else                        // This shouldn't happen, buffer must be messed up.
      break;

    state = P_STATE_ELAPSED;
    o_stats = NULL;
    o_server = NULL;
    method = METHOD_OTHER;
    scheme = SCHEME_OTHER;

    while ((field = fieldlist->next(field))) {
      switch (state) {
      case P_STATE_ELAPSED:
        state = P_STATE_IP;
        elapsed = *((int64 *) (read_from));
        read_from += INK_MIN_ALIGN;
        break;

      case P_STATE_IP:
        state = P_STATE_RESULT;
        // Just skip the IP, we no longer assume it's always the same.
        //
        // TODO address IP logged in text format (that's not good)
        // Warning: This is maybe not IPv6 safe.
        read_from += LogAccess::strlen(read_from);
        break;

      case P_STATE_RESULT:
        state = P_STATE_CODE;
        result = *((int64 *) (read_from));
        read_from += INK_MIN_ALIGN;
        if ((result<32) || (result> 255)) {
          flag = 1;
          state = P_STATE_END;
        }
        break;

      case P_STATE_CODE:
        state = P_STATE_SIZE;
        http_code = *((int64 *) (read_from));
        read_from += INK_MIN_ALIGN;
        if ((http_code<0) || (http_code> 999)) {
          flag = 1;
          state = P_STATE_END;
        }
        //printf("CODE == %d\n", http_code);
        break;

      case P_STATE_SIZE:
        // Warning: This is not 64-bit safe, when converting the log format,
        // this needs to be fixed as well.
        state = P_STATE_METHOD;
        size = *((int64 *) (read_from));
        read_from += INK_MIN_ALIGN;
        //printf("Size == %d\n", size)
        break;

      case P_STATE_METHOD:
        state = P_STATE_URL;
        //printf("METHOD == %s\n", read_from);
        flag = 0;

        // Small optimization for common (3-4 char) cases
        switch (*reinterpret_cast < int *>(read_from)) {
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
          if ((tok_len == 5) && (strncmp(read_from, "PURGE", 5) == 0))
            method = METHOD_PURGE;
          else if ((tok_len == 6) && (strncmp(read_from, "DELETE", 6) == 0))
            method = METHOD_DELETE;
          else if ((tok_len == 7) && (strncmp(read_from, "OPTIONS", 7) == 0))
            method = METHOD_OPTIONS;
          else if ((tok_len == 1) && (*read_from == '-')) {
            method = METHOD_NONE;
            flag = 1;           // No method, so no need to parse the URL
          } else {
            ptr = read_from;
            while (*ptr && isupper(*ptr))
              ++ptr;
            // Skip URL if it doesn't look like an HTTP method
            if (*ptr != '\0')
              flag = 1;
          }
          read_from += LogAccess::round_strlen(tok_len + 1);
          break;
        }
        break;

      case P_STATE_URL:
        state = P_STATE_RFC931;

        //printf("URL == %s\n", tok);
        // TODO check for read_from being empty string
        if (flag == 0) {
          tok = read_from;
          if (*reinterpret_cast < int *>(tok) == HTTP_AS_INT) {
            tok += 4;
            if (*tok == ':') {
              scheme = SCHEME_HTTP;
              tok += 3;
              tok_len = strlen(tok) + 7;
            } else if (*tok == 's') {
              scheme = SCHEME_HTTPS;
              tok += 4;
              tok_len = strlen(tok) + 8;
            } else
              tok_len = strlen(tok) + 4;
          } else {
            if (*tok == '/')
              scheme = SCHEME_NONE;
            tok_len = strlen(tok);
          }
          //printf("SCHEME = %d\n", scheme);
          if (*tok == '/')      // This is to handle crazy stuff like http:///origin.com
            tok++;
          ptr = strchr(tok, '/');
          if (ptr && !summary)  // Find the origin
          {
            *ptr = '\0';

            if (origin_set ? (origin_set->find(tok) != origin_set->end()) : 1) {
              //printf("ORIGIN = %s\n", tok);
              o_iter = origins.find(tok);
              if (o_iter == origins.end()) {
                o_stats = (OriginStats *) xmalloc(sizeof(OriginStats));
                init_elapsed(*o_stats);
                o_server = xstrdup(tok);
                if (o_stats && o_server) {
                  o_stats->server = o_server;
                  origins[o_server] = o_stats;
                }
              } else
                o_stats = o_iter->second;
            }
          }
        } else {
          // No method given
          if (*read_from == '/')
            scheme = SCHEME_NONE;
          tok_len = strlen(read_from);
        }
        read_from += LogAccess::round_strlen(tok_len + 1);

        // Update the stats so far, since now we have the Origin (maybe)
        update_results_elapsed(&totals, result, elapsed, size);
        update_codes(&totals, http_code, size);
        update_methods(&totals, method, size);
        update_schemes(&totals, scheme, size);
        update_counter(totals.total, size);
        if (o_stats != NULL) {
          update_results_elapsed(o_stats, result, elapsed, size);
          update_codes(o_stats, http_code, size);
          update_methods(o_stats, method, size);
          update_schemes(o_stats, scheme, size);
          update_counter(o_stats->total, size);
        }
        break;

      case P_STATE_RFC931:
        state = P_STATE_HIERARCHY;
        if (*read_from == '-')
          read_from += LogAccess::round_strlen(1 + 1);
        else
          read_from += LogAccess::strlen(read_from);
        break;

      case P_STATE_HIERARCHY:
        state = P_STATE_PEER;
        hier = *((int64 *) (read_from));
        switch (hier) {
        case SQUID_HIER_NONE:
          update_counter(totals.hierarchies.none, size);
          if (o_stats != NULL)
            update_counter(o_stats->hierarchies.none, size);
          break;
        case SQUID_HIER_DIRECT:
          update_counter(totals.hierarchies.direct, size);
          if (o_stats != NULL)
            update_counter(o_stats->hierarchies.direct, size);
          break;
        case SQUID_HIER_SIBLING_HIT:
          update_counter(totals.hierarchies.sibling, size);
          if (o_stats != NULL)
            update_counter(o_stats->hierarchies.sibling, size);
          break;
        case SQUID_HIER_PARENT_HIT:
          update_counter(totals.hierarchies.parent, size);
          if (o_stats != NULL)
            update_counter(o_stats->hierarchies.direct, size);
          break;
        case SQUID_HIER_EMPTY:
          update_counter(totals.hierarchies.empty, size);
          if (o_stats != NULL)
            update_counter(o_stats->hierarchies.empty, size);
          break;
        default:
          if ((hier >= SQUID_HIER_EMPTY) && (hier < SQUID_HIER_INVALID_ASSIGNED_CODE)) {
            update_counter(totals.hierarchies.other, size);
            if (o_stats != NULL)
              update_counter(o_stats->hierarchies.other, size);
          } else {
            update_counter(totals.hierarchies.invalid, size);
            if (o_stats != NULL)
              update_counter(o_stats->hierarchies.invalid, size);
          }
          break;
        }
        read_from += INK_MIN_ALIGN;
        break;

      case P_STATE_PEER:
        state = P_STATE_TYPE;
        if (*read_from == '-')
          read_from += LogAccess::round_strlen(1 + 1);
        else
          read_from += LogAccess::strlen(read_from);
        break;

      case P_STATE_TYPE:
        state = P_STATE_END;
        //printf("TYPE == %s\n", read_from);
        if (*reinterpret_cast < int *>(read_from) == IMAG_AS_INT) {
          update_counter(totals.content.image.total, size);
          if (o_stats != NULL)
            update_counter(o_stats->content.image.total, size);
          tok = read_from + 6;
          //printf("SUBTYPE == %s\n", tok);
          switch (*reinterpret_cast < int *>(tok)) {
          case JPEG_AS_INT:
            tok_len = 10;
            update_counter(totals.content.image.jpeg, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.image.jpeg, size);
            break;
          case JPG_AS_INT:
            tok_len = 9;
            update_counter(totals.content.image.jpeg, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.image.jpeg, size);
            break;
          case GIF_AS_INT:
            tok_len = 9;
            update_counter(totals.content.image.gif, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.image.gif, size);
            break;
          case PNG_AS_INT:
            tok_len = 9;
            update_counter(totals.content.image.png, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.image.png, size);
            break;
          case BMP_AS_INT:
            tok_len = 9;
            update_counter(totals.content.image.bmp, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.image.bmp, size);
            break;
          default:
            tok_len = 6 + strlen(tok);
            update_counter(totals.content.image.other, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.image.other, size);
            break;
          }
        } else if (*reinterpret_cast < int *>(read_from) == TEXT_AS_INT) {
          tok = read_from + 5;
          //printf("SUBTYPE == %s\n", tok);
          update_counter(totals.content.text.total, size);
          if (o_stats != NULL)
            update_counter(o_stats->content.text.total, size);
          switch (*reinterpret_cast < int *>(tok)) {
          case JAVA_AS_INT:
            // TODO verify if really "javascript"
            tok_len = 15;
            update_counter(totals.content.text.javascript, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.text.javascript, size);
            break;
          case CSS_AS_INT:
            tok_len = 8;
            update_counter(totals.content.text.css, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.text.css, size);
            break;
          case XML_AS_INT:
            tok_len = 8;
            update_counter(totals.content.text.xml, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.text.xml, size);
            break;
          case HTML_AS_INT:
            tok_len = 9;
            update_counter(totals.content.text.html, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.text.html, size);
            break;
          case PLAI_AS_INT:
            tok_len = 10;
            update_counter(totals.content.text.plain, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.text.plain, size);
            break;
          default:
            tok_len = 5 + strlen(tok);;
            update_counter(totals.content.text.other, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.text.other, size);
            break;
          }
        } else if (strncmp(read_from, "application", 11) == 0) {
          // TODO optimize token acquisition
          tok = read_from + 12;
          //printf("SUBTYPE == %s\n", tok);
          update_counter(totals.content.application.total, size);
          if (o_stats != NULL)
            update_counter(o_stats->content.application.total, size);
          if (strcmp(tok, "x-shockwave-flash") == 0) {
            tok_len = 29;
            update_counter(totals.content.application.shockwave_flash, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.application.shockwave_flash, size);
          } else if (strcmp(tok, "x-javascript") == 0) {
            tok_len = 24;
            update_counter(totals.content.application.javascript, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.application.javascript, size);
          } else if (strcmp(tok, "x-quicktimeplayer") == 0) {
            tok_len = 29;
            update_counter(totals.content.application.quicktime, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.application.quicktime, size);
          } else if (*reinterpret_cast < int *>(tok) == ZIP_AS_INT) {
            tok_len = 15;
            update_counter(totals.content.application.zip, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.application.zip, size);
          } else {
            tok_len = 12 + strlen(tok);
            update_counter(totals.content.application.other, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.application.other, size);
          }
        } else if (strncmp(read_from, "audio", 5) == 0) {
          // TODO use strcmp()
          tok = read_from + 6;
          tok_len = 6 + strlen(tok);
          update_counter(totals.content.audio.total, size);
          if (o_stats != NULL)
            update_counter(o_stats->content.audio.total, size);
          //printf("SUBTYPE == %s\n", tok);
          if ((strcmp(tok, "x-wav") == 0) || (strcmp(tok, "wav") == 0)) {
            update_counter(totals.content.audio.wav, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.audio.wav, size);
          } else if ((strcmp(tok, "x-mpeg") == 0) || (strcmp(tok, "mpeg") == 0)) {
            update_counter(totals.content.audio.mpeg, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.audio.mpeg, size);
          } else {
            update_counter(totals.content.audio.other, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.audio.other, size);
          }
        } else if (*read_from == '-') {
          tok_len = 1;
          update_counter(totals.content.none, size);
          if (o_stats != NULL)
            update_counter(o_stats->content.none, size);
        } else {
          tok_len = strlen(read_from);
          update_counter(totals.content.other, size);
          if (o_stats != NULL)
            update_counter(o_stats->content.other, size);
        }
        read_from += LogAccess::round_strlen(tok_len + 1);
        flag = 0;               // We exited this state without errors
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

  while (true) {
    Debug("logcat", "Reading buffer ...");
    buffer[0] = '\0';

    unsigned first_read_size = 2 * sizeof(unsigned);
    LogBufferHeader *header = (LogBufferHeader *) & buffer[0];

    // Find the next log header, aligning us properly. This is not
    // particularly optimal, but we shouldn't only have to do this
    // once, and hopefully we'll be aligned immediately.
    if (offset > 0) {
      while (true) {
        if (lseek(in_fd, offset, SEEK_SET) < 0)
          return 1;

        // read the first 8 bytes of the header, which will give us the
        // cookie and the version number.
        nread = read(in_fd, buffer, first_read_size);
        if (!nread || nread == EOF) {
          return 0;
        }
        // ensure that this is a valid logbuffer header
        if (header->cookie && (header->cookie == LOG_SEGMENT_COOKIE)) {
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
      if (!nread || nread == EOF || !header->cookie)
        return 0;

      // ensure that this is a valid logbuffer header
      if (header->cookie != LOG_SEGMENT_COOKIE)
        return 1;
    }

    Debug("logstats", "LogBuffer version %d, current = %d", header->version, LOG_SEGMENT_VERSION);
    if (header->version != LOG_SEGMENT_VERSION)
      return 1;

    // read the rest of the header
    unsigned second_read_size = sizeof(LogBufferHeader) - first_read_size;
    nread = read(in_fd, &buffer[first_read_size], second_read_size);
    if (!nread || nread == EOF)
      return 1;

    // read the rest of the buffer
    if (header->byte_count > sizeof(buffer))
      return 1;

    buffer_bytes = header->byte_count - sizeof(LogBufferHeader) + 1;
    if (buffer_bytes <= 0 || (unsigned int) buffer_bytes > (sizeof(buffer) - sizeof(LogBufferHeader)))
      return 1;

    nread = read(in_fd, &buffer[sizeof(LogBufferHeader)], buffer_bytes);
    if (!nread || nread == EOF)
      return 1;

    // Possibly skip too old entries (the entire buffer is skipped)
    if (header->high_timestamp >= max_age)
      if (parse_log_buff(header, cl.summary != 0) != 0)
        return 1;
  }

  return 0;
}


///////////////////////////////////////////////////////////////////////////////
// Determine if this "stat" (Origin Server) is worthwhile to produce a
// report for.
inline int
use_origin(const OriginStats * stat)
{
  return ((stat->total.count > cl.min_hits) && (strchr(stat->server, '.') != NULL) &&
          (strchr(stat->server, '%') == NULL));
}


///////////////////////////////////////////////////////////////////////////////
// Produce a nicely formatted output for a stats collection on a stream
inline void
format_center(const char *str, std::ostream & out)
{
  out << std::setfill(' ') << std::setw((cl.line_len - strlen(str)) / 2 + strlen(str)) << str << std::endl << std::endl;
}

inline void
format_int(int64 num, std::ostream & out)
{
  if (num > 0) {
    int64 mult = (int64) pow((double)10, (int) (log10((double)num) / 3) * 3);
    int64 div;
    std::stringstream ss;

    ss.fill('0');
    while (mult > 0) {
      div = num / mult;
      ss << div << std::setw(3);
      num -= (div * mult);
      if (mult /= 1000)
        ss << std::setw(0) << ',' << std::setw(3);
    }
    out << ss.str();
  } else
    out << '0';
}

void
format_elapsed_header(std::ostream & out)
{
  out << std::left << std::setw(20) << "Elapsed time stats";
  out << std::right << std::setw(6) << "Min" << std::setw(10) << "Max";
  out << std::right << std::setw(20) << "Avg" << std::setw(24) << "Std Deviation" << std::endl;
  out << std::setw(cl.line_len) << std::setfill('-') << '-' << std::setfill(' ') << std::endl;
}

inline void
format_elapsed_line(const char *desc, const ElapsedStats & stat, std::ostream & out)
{
  static char buf[64];
  out << std::left << std::setw(20) << desc;
  out << std::right << std::setw(6);
  format_int(stat.min, out);
  out << std::right << std::setw(10);
  format_int(stat.max, out);
  snprintf(buf, sizeof(buf), "%20.10f", stat.avg);
  out << std::right << buf;
  snprintf(buf, sizeof(buf), "%24.12f", stat.stddev);
  out << std::right << buf << std::endl;
}


void
format_detail_header(const char *desc, std::ostream & out)
{
  out << std::left << std::setw(29) << desc;
  out << std::right << std::setw(15) << "Count" << std::setw(11) << "Percent";
  out << std::right << std::setw(12) << "Bytes" << std::setw(11) << "Percent" << std::endl;
  out << std::setw(cl.line_len) << std::setfill('-') << '-' << std::setfill(' ') << std::endl;
}

inline void
format_line(const char *desc, const StatsCounter & stat, const StatsCounter & total, std::ostream & out)
{
  static char metrics[] = "KKMGTP";
  static char buf[64];
  int ix = (stat.bytes > 1024 ? (int) (log10((double)stat.bytes) / LOG10_1024) : 1);

  out << std::left << std::setw(29) << desc;

  out << std::right << std::setw(15);
  format_int(stat.count, out);

  snprintf(buf, sizeof(buf), "%10.2f%%", ((double) stat.count / total.count * 100));
  out << std::right << buf;

  snprintf(buf, sizeof(buf), "%10.2f%cB", stat.bytes / pow((double)1024, ix), metrics[ix]);
  out << std::right << buf;

  snprintf(buf, sizeof(buf), "%10.2f%%", ((double) stat.bytes / total.bytes * 100));
  out << std::right << buf << std::endl;
}


// Little "helpers" for the vector we use to sort the Origins.
typedef pair<const char *, OriginStats *>OriginPair;
inline bool
operator<(const OriginPair & a, const OriginPair & b)
{
  return a.second->total.count > b.second->total.count;
}

void
print_detail_stats(const OriginStats * stat, std::ostream & out)
{
  // Cache hit/misses etc.
  format_detail_header("Request Result", out);

  format_line("Cache hit", stat->results.hits.hit, stat->total, out);
  format_line("Cache hit IMS", stat->results.hits.ims, stat->total, out);
  format_line("Cache hit refresh", stat->results.hits.refresh, stat->total, out);
  format_line("Cache hit other", stat->results.hits.other, stat->total, out);
  format_line("Cache hit total", stat->results.hits.total, stat->total, out);

  out << std::endl;

  format_line("Cache miss", stat->results.misses.miss, stat->total, out);
  format_line("Cache miss IMS", stat->results.misses.ims, stat->total, out);
  format_line("Cache miss refresh", stat->results.misses.refresh, stat->total, out);
  format_line("Cache miss other", stat->results.misses.other, stat->total, out);
  format_line("Cache miss total", stat->results.misses.total, stat->total, out);

  out << std::endl;

  format_line("Client aborted", stat->results.errors.client_abort, stat->total, out);
  format_line("Connect failed", stat->results.errors.connect_fail, stat->total, out);
  format_line("Invalid request", stat->results.errors.invalid_req, stat->total, out);
  format_line("Unknown error(99)", stat->results.errors.unknown, stat->total, out);
  format_line("Other errors", stat->results.errors.other, stat->total, out);
  format_line("Errors total", stat->results.errors.total, stat->total, out);

  out << std::setw(cl.line_len) << std::setfill('.') << '.' << std::setfill(' ') << std::endl;

  format_line("Total requests", stat->total, stat->total, out);

  out << std::endl << std::endl;

  // HTTP codes
  format_detail_header("HTTP return codes", out);

  format_line("200 OK", stat->codes.c_200, stat->total, out);
  format_line("204 No content", stat->codes.c_204, stat->total, out);
  format_line("206 Partial content", stat->codes.c_206, stat->total, out);
  format_line("2xx other success", stat->codes.c_2xx, stat->total, out);

  out << std::endl;

  format_line("301 Moved permanently", stat->codes.c_301, stat->total, out);
  format_line("302 Found", stat->codes.c_302, stat->total, out);
  format_line("304 Not modified", stat->codes.c_304, stat->total, out);
  format_line("3xx other redirects", stat->codes.c_3xx, stat->total, out);

  out << std::endl;

  format_line("400 Bad request", stat->codes.c_400, stat->total, out);
  format_line("403 Forbidden", stat->codes.c_403, stat->total, out);
  format_line("404 Not found", stat->codes.c_404, stat->total, out);
  format_line("4xx other client errors", stat->codes.c_4xx, stat->total, out);

  out << std::endl;

  format_line("501 Not implemented", stat->codes.c_501, stat->total, out);
  format_line("502 Bad gateway", stat->codes.c_502, stat->total, out);
  format_line("503 Service unavailable", stat->codes.c_503, stat->total, out);
  format_line("5xx other server errors", stat->codes.c_5xx, stat->total, out);

  out << std::endl;

  format_line("999 YDoD rejection", stat->codes.c_999, stat->total, out);
  format_line("000 Unknown", stat->codes.c_000, stat->total, out);

  out << std::endl << std::endl;

  // Origin hierarchies
  format_detail_header("Origin hierarchies", out);

  format_line("NONE", stat->hierarchies.none, stat->total, out);
  format_line("DIRECT", stat->hierarchies.direct, stat->total, out);
  format_line("SIBLING", stat->hierarchies.sibling, stat->total, out);
  format_line("PARENT", stat->hierarchies.parent, stat->total, out);
  format_line("EMPTY", stat->hierarchies.empty, stat->total, out);
  format_line("invalid", stat->hierarchies.invalid, stat->total, out);
  format_line("other", stat->hierarchies.other, stat->total, out);

  out << std::endl << std::endl;

  // HTTP methods
  format_detail_header("HTTP Methods", out);

  format_line("GET", stat->methods.get, stat->total, out);
  format_line("PUT", stat->methods.put, stat->total, out);
  format_line("HEAD", stat->methods.head, stat->total, out);
  format_line("POST", stat->methods.post, stat->total, out);
  format_line("DELETE", stat->methods.del, stat->total, out);
  format_line("PURGE", stat->methods.purge, stat->total, out);
  format_line("OPTIONS", stat->methods.options, stat->total, out);
  format_line("none (-)", stat->methods.none, stat->total, out);
  format_line("other", stat->methods.other, stat->total, out);

  out << std::endl << std::endl;

  // URL schemes (HTTP/HTTPs)
  format_detail_header("URL Schemes", out);

  format_line("HTTP (port 80)", stat->schemes.http, stat->total, out);
  format_line("HTTPS (port 443)", stat->schemes.https, stat->total, out);
  format_line("none", stat->schemes.none, stat->total, out);
  format_line("other", stat->schemes.other, stat->total, out);

  out << std::endl << std::endl;

  // Content types
  format_detail_header("Content Types", out);

  format_line("text/javascript", stat->content.text.javascript, stat->total, out);
  format_line("text/css", stat->content.text.css, stat->total, out);
  format_line("text/html", stat->content.text.html, stat->total, out);
  format_line("text/xml", stat->content.text.xml, stat->total, out);
  format_line("text/plain", stat->content.text.plain, stat->total, out);
  format_line("text/ other", stat->content.text.other, stat->total, out);
  format_line("text/ total", stat->content.text.total, stat->total, out);

  out << std::endl;

  format_line("image/jpeg", stat->content.image.jpeg, stat->total, out);
  format_line("image/gif", stat->content.image.gif, stat->total, out);
  format_line("image/png", stat->content.image.png, stat->total, out);
  format_line("image/bmp", stat->content.image.bmp, stat->total, out);
  format_line("image/ other", stat->content.image.other, stat->total, out);
  format_line("image/ total", stat->content.image.total, stat->total, out);

  out << std::endl;

  format_line("audio/x-wav", stat->content.audio.wav, stat->total, out);
  format_line("audio/x-mpeg", stat->content.audio.mpeg, stat->total, out);
  format_line("audio/ other", stat->content.audio.other, stat->total, out);
  format_line("audio/ total", stat->content.audio.total, stat->total, out);

  out << std::endl;

  format_line("application/x-shockwave", stat->content.application.shockwave_flash, stat->total, out);
  format_line("application/x-javascript", stat->content.application.javascript, stat->total, out);
  format_line("application/x-quicktime", stat->content.application.quicktime, stat->total, out);
  format_line("application/zip", stat->content.application.zip, stat->total, out);
  format_line("application/ other", stat->content.application.other, stat->total, out);
  format_line("application/ total", stat->content.application.total, stat->total, out);

  out << std::endl;

  format_line("none", stat->content.none, stat->total, out);
  format_line("other", stat->content.other, stat->total, out);

  out << std::endl << std::endl;

  // Elapsed time
  format_elapsed_header(out);

  format_elapsed_line("Cache hit", stat->elapsed.hits.hit, out);
  format_elapsed_line("Cache hit IMS", stat->elapsed.hits.ims, out);
  format_elapsed_line("Cache hit refresh", stat->elapsed.hits.refresh, out);
  format_elapsed_line("Cache hit other", stat->elapsed.hits.other, out);
  format_elapsed_line("Cache hit total", stat->elapsed.hits.total, out);

  format_elapsed_line("Cache miss", stat->elapsed.misses.miss, out);
  format_elapsed_line("Cache miss IMS", stat->elapsed.misses.ims, out);
  format_elapsed_line("Cache miss refresh", stat->elapsed.misses.refresh, out);
  format_elapsed_line("Cache miss other", stat->elapsed.misses.other, out);
  format_elapsed_line("Cache miss total", stat->elapsed.misses.total, out);

  out << std::endl;
  out << std::setw(cl.line_len) << std::setfill('_') << '_' << std::setfill(' ') << std::endl;
}


///////////////////////////////////////////////////////////////////////////////
// Produce metrics in YMon format for a particular Origin.
inline void
format_ymon(const char *subsys, const char *desc, const char *server, const StatsCounter & stat, std::ostream & out)
{
  out << subsys << ".'" << server << "'." << desc << "_cnt=" << stat.count << " ";
  out << subsys << ".'" << server << "'." << desc << "_bytes=" << stat.bytes << " ";
}

// Produce "elapsed" metrics in YMon format for a particular Origin.
inline void
format_elapsed_ymon(const char *subsys, const char *desc, const char *server, const ElapsedStats & stat,
                    std::ostream & out)
{
  out << subsys << ".'" << server << "'." << desc << "_min=" << stat.min << " ";
  out << subsys << ".'" << server << "'." << desc << "_max=" << stat.max << " ";
  out << subsys << ".'" << server << "'." << desc << "_avg=" << stat.avg << " ";
  out << subsys << ".'" << server << "'." << desc << "_stddev=" << stat.stddev << " ";
}

void
print_ymon_metrics(const OriginStats * stat, std::ostream & out)
{
  // Results (hits, misses, errors and a total
  format_ymon("result", "hit", stat->server, stat->results.hits.hit, out);
  format_ymon("result", "hit_ims", stat->server, stat->results.hits.ims, out);
  format_ymon("result", "hit_refresh", stat->server, stat->results.hits.refresh, out);
  format_ymon("result", "hit_other", stat->server, stat->results.hits.other, out);
  format_ymon("result", "hit_total", stat->server, stat->results.hits.total, out);

  format_ymon("result", "miss", stat->server, stat->results.misses.miss, out);
  format_ymon("result", "miss_ims", stat->server, stat->results.misses.ims, out);
  format_ymon("result", "miss_refresh", stat->server, stat->results.misses.refresh, out);
  format_ymon("result", "miss_other", stat->server, stat->results.misses.other, out);
  format_ymon("result", "miss_total", stat->server, stat->results.misses.total, out);

  format_ymon("result", "err_abort", stat->server, stat->results.errors.client_abort, out);
  format_ymon("result", "err_conn", stat->server, stat->results.errors.connect_fail, out);
  format_ymon("result", "err_invalid", stat->server, stat->results.errors.invalid_req, out);
  format_ymon("result", "err_unknown", stat->server, stat->results.errors.unknown, out);
  format_ymon("result", "err_other", stat->server, stat->results.errors.other, out);
  format_ymon("result", "err_total", stat->server, stat->results.errors.total, out);

  format_ymon("result", "total", stat->server, stat->total, out);

  // HTTP codes
  format_ymon("http", "200", stat->server, stat->codes.c_200, out);
  format_ymon("http", "204", stat->server, stat->codes.c_204, out);
  format_ymon("http", "206", stat->server, stat->codes.c_206, out);
  format_ymon("http", "2xx", stat->server, stat->codes.c_2xx, out);

  format_ymon("http", "301", stat->server, stat->codes.c_301, out);
  format_ymon("http", "302", stat->server, stat->codes.c_302, out);
  format_ymon("http", "304", stat->server, stat->codes.c_304, out);
  format_ymon("http", "3xx", stat->server, stat->codes.c_3xx, out);

  format_ymon("http", "400", stat->server, stat->codes.c_400, out);
  format_ymon("http", "403", stat->server, stat->codes.c_403, out);
  format_ymon("http", "404", stat->server, stat->codes.c_404, out);
  format_ymon("http", "4xx", stat->server, stat->codes.c_4xx, out);

  format_ymon("http", "501", stat->server, stat->codes.c_501, out);
  format_ymon("http", "502", stat->server, stat->codes.c_502, out);
  format_ymon("http", "503", stat->server, stat->codes.c_503, out);
  format_ymon("http", "5xx", stat->server, stat->codes.c_5xx, out);

  format_ymon("http", "999", stat->server, stat->codes.c_999, out);
  format_ymon("http", "000", stat->server, stat->codes.c_000, out);

  // Origin hierarchies
  format_ymon("hier", "none", stat->server, stat->hierarchies.none, out);
  format_ymon("hier", "direct", stat->server, stat->hierarchies.direct, out);
  format_ymon("hier", "sibling", stat->server, stat->hierarchies.sibling, out);
  format_ymon("hier", "parent", stat->server, stat->hierarchies.parent, out);
  format_ymon("hier", "empty", stat->server, stat->hierarchies.empty, out);
  format_ymon("hier", "invalid", stat->server, stat->hierarchies.invalid, out);
  format_ymon("hier", "other", stat->server, stat->hierarchies.other, out);

  // HTTP methods
  format_ymon("method", "get", stat->server, stat->methods.get, out);
  format_ymon("method", "put", stat->server, stat->methods.put, out);
  format_ymon("method", "head", stat->server, stat->methods.head, out);
  format_ymon("method", "post", stat->server, stat->methods.post, out);
  format_ymon("method", "delete", stat->server, stat->methods.del, out);
  format_ymon("method", "purge", stat->server, stat->methods.purge, out);
  format_ymon("method", "options", stat->server, stat->methods.options, out);
  format_ymon("method", "none", stat->server, stat->methods.none, out);
  format_ymon("method", "other", stat->server, stat->methods.other, out);

  // URL schemes (HTTP/HTTPs)
  format_ymon("scheme", "http", stat->server, stat->schemes.http, out);
  format_ymon("scheme", "https", stat->server, stat->schemes.https, out);
  format_ymon("scheme", "none", stat->server, stat->schemes.none, out);
  format_ymon("scheme", "other", stat->server, stat->schemes.other, out);

  // Content types
  format_ymon("ctype", "text_js", stat->server, stat->content.text.javascript, out);
  format_ymon("ctype", "text_css", stat->server, stat->content.text.css, out);
  format_ymon("ctype", "text_html", stat->server, stat->content.text.html, out);
  format_ymon("ctype", "text_xml", stat->server, stat->content.text.xml, out);
  format_ymon("ctype", "text_plain", stat->server, stat->content.text.plain, out);
  format_ymon("ctype", "text_other", stat->server, stat->content.text.other, out);
  format_ymon("ctype", "text_total", stat->server, stat->content.text.total, out);

  format_ymon("ctype", "image_jpeg", stat->server, stat->content.image.jpeg, out);
  format_ymon("ctype", "image_gif", stat->server, stat->content.image.gif, out);
  format_ymon("ctype", "image_png", stat->server, stat->content.image.png, out);
  format_ymon("ctype", "image_bmp", stat->server, stat->content.image.bmp, out);
  format_ymon("ctype", "image_other", stat->server, stat->content.image.other, out);
  format_ymon("ctype", "image_total", stat->server, stat->content.image.total, out);

  format_ymon("ctype", "audio_xwav", stat->server, stat->content.audio.wav, out);
  format_ymon("ctype", "audio_xmpeg", stat->server, stat->content.audio.mpeg, out);
  format_ymon("ctype", "audio_other", stat->server, stat->content.audio.other, out);
  format_ymon("ctype", "audio_total", stat->server, stat->content.audio.total, out);

  format_ymon("ctype", "app_shock", stat->server, stat->content.application.shockwave_flash, out);
  format_ymon("ctype", "app_js", stat->server, stat->content.application.javascript, out);
  format_ymon("ctype", "app_qt", stat->server, stat->content.application.quicktime, out);
  format_ymon("ctype", "app_zip", stat->server, stat->content.application.zip, out);
  format_ymon("ctype", "app_other", stat->server, stat->content.application.other, out);
  format_ymon("ctype", "app_total", stat->server, stat->content.application.total, out);

  format_ymon("ctype", "none", stat->server, stat->content.none, out);
  format_ymon("ctype", "other", stat->server, stat->content.other, out);

  // Elapsed stats
  format_elapsed_ymon("elapsed", "hit", stat->server, stat->elapsed.hits.hit, out);
  format_elapsed_ymon("elapsed", "hit_ims", stat->server, stat->elapsed.hits.ims, out);
  format_elapsed_ymon("elapsed", "hit_refresh", stat->server, stat->elapsed.hits.refresh, out);
  format_elapsed_ymon("elapsed", "hit_other", stat->server, stat->elapsed.hits.other, out);
  format_elapsed_ymon("elapsed", "hit_total", stat->server, stat->elapsed.hits.total, out);

  format_elapsed_ymon("elapsed", "miss", stat->server, stat->elapsed.misses.miss, out);
  format_elapsed_ymon("elapsed", "miss_ims", stat->server, stat->elapsed.misses.ims, out);
  format_elapsed_ymon("elapsed", "miss_refresh", stat->server, stat->elapsed.misses.refresh, out);
  format_elapsed_ymon("elapsed", "miss_other", stat->server, stat->elapsed.misses.other, out);
  format_elapsed_ymon("elapsed", "miss_total", stat->server, stat->elapsed.misses.total, out);
}


///////////////////////////////////////////////////////////////////////////////
// Format one value for ysar output
inline void
format_ysar(const StatsCounter & stat, const StatsCounter & tot, bool last = false)
{
  if (last)
    std::cout << 100 * (double) stat.count / tot.count;
  else
    std::cout << 100 * (double) stat.count / tot.count << ',';
}

///////////////////////////////////////////////////////////////////////////////
// Little wrapper around exit, to allow us to exit like a YMon plugin.
void
my_exit(YmonLevel status, const char *notice)
{
  vector<OriginPair> vec;

  if (cl.ysar) {
    std::cout.precision(2);
    std::cout.setf(std::ios::fixed);

    format_ysar(totals.codes.c_200, totals.total);
    format_ysar(totals.codes.c_204, totals.total);
    format_ysar(totals.codes.c_206, totals.total);
    format_ysar(totals.codes.c_2xx, totals.total);

    format_ysar(totals.codes.c_301, totals.total);
    format_ysar(totals.codes.c_302, totals.total);
    format_ysar(totals.codes.c_304, totals.total);
    format_ysar(totals.codes.c_3xx, totals.total);

    format_ysar(totals.codes.c_400, totals.total);
    format_ysar(totals.codes.c_403, totals.total);
    format_ysar(totals.codes.c_404, totals.total);
    format_ysar(totals.codes.c_4xx, totals.total);

    format_ysar(totals.codes.c_501, totals.total);
    format_ysar(totals.codes.c_502, totals.total);
    format_ysar(totals.codes.c_503, totals.total);
    format_ysar(totals.codes.c_5xx, totals.total);

    format_ysar(totals.codes.c_999, totals.total);
    format_ysar(totals.codes.c_000, totals.total);

    format_ysar(totals.content.text.total, totals.total);
    format_ysar(totals.content.image.total, totals.total);
    format_ysar(totals.content.application.total, totals.total);
    format_ysar(totals.content.audio.total, totals.total);
    format_ysar(totals.content.other, totals.total);
    format_ysar(totals.content.none, totals.total, true);
  } else if (cl.ymon) {
    std::cout << hostname << '\t' << "yts_origins\t" << status << '\t' << "ver. " << PACKAGE_VERSION << notice << std::
      endl;
    for (OriginStorage::iterator i = origins.begin(); i != origins.end(); i++) {
      if (use_origin(i->second)) {
        std::cout << hostname << '\t' << "yts_origins" << '\t' <<
          status << '\t' << "ver. " << PACKAGE_VERSION << notice << "|";
        print_ymon_metrics(i->second, std::cout);
        std::cout << std::endl;
      }
    }
  } else {
    switch (status) {
    case YMON_OK:
      break;
    case YMON_WARNING:
      std::cout << "warning: " << notice << std::endl;
      break;
    case YMON_CRITICAL:
      std::cout << "critical: " << notice << std::endl;
      _exit(status);
      break;
    case YMON_UNKNOWN:
      std::cout << "unknown: " << notice << std::endl;
      _exit(status);
      break;
    }

    if (!origins.empty()) {
      // Sort the Origins by 'traffic'
      for (OriginStorage::iterator i = origins.begin(); i != origins.end(); i++)
        if (use_origin(i->second))
          vec.push_back(*i);
      sort(vec.begin(), vec.end());

      // Produce a nice summary first
      format_center("Traffic summary", std::cout);
      std::cout << std::left << std::setw(33) << "Origin Server";
      std::cout << std::right << std::setw(15) << "Hits";
      std::cout << std::right << std::setw(15) << "Misses";
      std::cout << std::right << std::setw(15) << "Errors" << std::endl;
      std::cout << std::setw(cl.line_len) << std::setfill('-') << '-' << std::setfill(' ') << std::endl;

      for (vector<OriginPair>::iterator i = vec.begin(); i != vec.end(); i++) {
        std::cout << std::left << std::setw(33) << i->first;
        std::cout << std::right << std::setw(15);
        format_int(i->second->results.hits.total.count, std::cout);
        std::cout << std::right << std::setw(15);
        format_int(i->second->results.misses.total.count, std::cout);
        std::cout << std::right << std::setw(15);
        format_int(i->second->results.errors.total.count, std::cout);
        std::cout << std::endl;
      }
      std::cout << std::setw(cl.line_len) << std::setfill('=') << '=' << std::setfill(' ') << std::endl;
      std::cout << std::endl << std::endl << std::endl;
    }
    // Next the totals for all Origins
    format_center("Totals (all Origins combined)", std::cout);
    print_detail_stats(&totals, std::cout);

    std::cout << std::endl << std::endl << std::endl;

    // And finally the individual Origin Servers.
    for (vector<OriginPair>::iterator i = vec.begin(); i != vec.end(); i++) {
      format_center(i->first, std::cout);
      print_detail_stats(i->second, std::cout);
      std::cout << std::endl << std::endl << std::endl;
    }
  }

  _exit(status);
}

///////////////////////////////////////////////////////////////////////////////
// Open the "default" log file (squid.blog), allow for it to be rotated.
int
open_main_log(char *ymon_notice, const size_t ymon_notice_size)
{
  int cnt = 3;
  int main_fd;

  while (((main_fd = open("./squid.blog", O_RDONLY)) < 0) && --cnt) {
    switch (errno) {
    case ENOENT:
    case EACCES:
      sleep(5);
      break;
    default:
      strncat(ymon_notice, " can't open squid.blog", ymon_notice_size - strlen(ymon_notice) - 1);
      return -1;
    }
  }

  if (main_fd < 0) {
    strncat(ymon_notice, " squid.blog not enabled", ymon_notice_size - strlen(ymon_notice) - 1);
    return -1;
  }
#if ATS_HAS_POSIX_FADVISE
  posix_fadvise(main_fd, 0, 0, POSIX_FADV_DONTNEED);
#endif
  return main_fd;
}



///////////////////////////////////////////////////////////////////////////////
// main
int
main(int argc, char *argv[])
{
  YmonLevel ymon_status = YMON_OK;
  char ymon_notice[4096];
  struct utsname uts_buf;
  int res, cnt;
  int main_fd;
  unsigned max_age;
  struct flock lck;

  // build the application information structure
  appVersionInfo.setup(PACKAGE_NAME,PROGRAM_NAME, PACKAGE_VERSION, __DATE__,
                       __TIME__, BUILD_MACHINE, BUILD_PERSON, "");

  // Before accessing file system initialize Layout engine
  Layout::create();
  // Initialize some globals
  memset(&totals, 0, sizeof(totals));   // Make sure counters are zero
  // Initialize "elapsed" field
  init_elapsed(totals);
  memset(&cl, 0, sizeof(cl));
  memset(ymon_notice, 0, sizeof(ymon_notice));
  cl.line_len = DEFAULT_LINE_LEN;
  origin_set = NULL;
  parse_errors = 0;

  // Get log directory
  ink_strlcpy(system_log_dir, Layout::get()->logdir, PATH_NAME_MAX);
  if (access(system_log_dir, R_OK) == -1) {
    fprintf(stderr, "unable to change to log directory \"%s\" [%d '%s']\n", system_log_dir, errno, strerror(errno));
    fprintf(stderr, " please set correct path in env variable TS_ROOT \n");
    exit(1);
  }

  // process command-line arguments
  process_args(argument_descriptions, n_argument_descriptions, argv, USAGE_LINE);

  // Post processing
  if (cl.ysar) {
    cl.summary = 1;
    cl.ymon = 0;
    cl.incremental = 1;
    if (cl.state_tag[0] == '\0')
      ink_strncpy(cl.state_tag, "ysar", sizeof(cl.state_tag));
  }
  if (cl.ymon) {
    cl.ysar = 0;
    cl.summary = 0;
  }
  // check for the version number request
  if (cl.version) {
    std::cerr << appVersionInfo.FullVersionInfoStr << std::endl;
    _exit(0);
  }
  // check for help request
  if (cl.help) {
    usage(argument_descriptions, n_argument_descriptions, USAGE_LINE);
    _exit(0);
  }
  // Calculate the max age of acceptable log entries, if necessary
  if (cl.max_age > 0) {
    struct timeval tv;

    gettimeofday(&tv, NULL);
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

    if (origin_set == NULL)
      origin_set = NEW(new OriginSet);
    if (cl.origin_list) {
      for (tok = strtok_r(cl.origin_list, ",", &sep_ptr); tok != NULL;) {
        origin_set->insert(tok);
        tok = strtok_r(NULL, ",", &sep_ptr);
      }
    }
  }
  // Load origins from an "external" file (\n separated)
  if (cl.origin_file[0] != '\0') {
    std::ifstream fs;

    fs.open(cl.origin_file, std::ios::in);
    if (!fs.is_open()) {
      std::cerr << "can't read " << cl.origin_file << std::endl;
      usage(argument_descriptions, n_argument_descriptions, USAGE_LINE);
      _exit(0);
    }

    if (origin_set == NULL)
      origin_set = NEW(new OriginSet);

    while (!fs.eof()) {
      std::string line;
      std::string::size_type start, end;

      getline(fs, line);
      start = line.find_first_not_of(" \t");
      if (start != std::string::npos) {
        end = line.find_first_of(" \t#/");
        if (end == std::string::npos)
          end = line.length();

        if (end > start) {
          char *buf;

          buf = xstrdup(line.substr(start, end).c_str());
          if (buf)
            origin_set->insert(buf);
        }
      }
    }
  }
  // Get the hostname
  if (uname(&uts_buf) < 0) {
    strncat(ymon_notice, " can't get hostname", sizeof(ymon_notice) - strlen(ymon_notice) - 1);
    my_exit(YMON_CRITICAL, ymon_notice);
  }
  hostname = xstrdup(uts_buf.nodename);

  // Change directory to the log dir
  if (chdir(system_log_dir) < 0) {
    snprintf(ymon_notice, sizeof(ymon_notice), "can't chdir to %s", system_log_dir);
    my_exit(YMON_CRITICAL, ymon_notice);
  }

  if (cl.incremental) {
    // Do the incremental parse of the default squid log.
    std::string sf_name(system_log_dir);
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
        strncat(ymon_notice, " can't get current UID", sizeof(ymon_notice) - strlen(ymon_notice) - 1);
        my_exit(YMON_CRITICAL, ymon_notice);
      }
    }

    if ((state_fd = open(sf_name.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR)) < 0) {
      strncat(ymon_notice, " can't open state file", sizeof(ymon_notice) - strlen(ymon_notice) - 1);
      my_exit(YMON_CRITICAL, ymon_notice);
    }
    // Get an exclusive lock, if possible. Try for up to 20 seconds.
    // Use more portable & standard fcntl() over flock()
    lck.l_type = F_WRLCK;
    lck.l_whence = 0; /* offset l_start from beginning of file*/
    lck.l_start = (off_t)0;
    lck.l_len = (off_t)0; /* till end of file*/
    cnt = 10;
    // while (((res = flock(state_fd, LOCK_EX | LOCK_NB)) < 0) && --cnt) {
    while (((res = fcntl(state_fd, F_SETLK, &lck)) < 0) && --cnt) {
      switch (errno) {
      case EWOULDBLOCK:
      case EINTR:
        sleep(2);
        break;
      default:
        strncat(ymon_notice, " locking failure", sizeof(ymon_notice) - strlen(ymon_notice) - 1);
        my_exit(YMON_CRITICAL, ymon_notice);
        break;
      }
    }

    if (res < 0) {
      strncat(ymon_notice, " can't lock state file", sizeof(ymon_notice) - strlen(ymon_notice) - 1);
      my_exit(YMON_CRITICAL, ymon_notice);
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
        strncat(ymon_notice, " can't read state file", sizeof(ymon_notice) - strlen(ymon_notice) - 1);
        my_exit(YMON_CRITICAL, ymon_notice);
        break;
      }
    }

    if (res != sizeof(last_state)) {
      // First time / empty file, so reset.
      last_state.offset = 0;
      last_state.st_ino = 0;
    }

    if ((main_fd = open_main_log(ymon_notice, sizeof(ymon_notice))) < 0)
      my_exit(YMON_CRITICAL, ymon_notice);

    // Get stat's from the main log file.
    if (fstat(main_fd, &stat_buf) < 0) {
      strncat(ymon_notice, " can't stat squid.blog", sizeof(ymon_notice) - strlen(ymon_notice) - 1);
      my_exit(YMON_CRITICAL, ymon_notice);
    }
    // Make sure the last_state.st_ino is sane.
    if (last_state.st_ino <= 0)
      last_state.st_ino = stat_buf.st_ino;

    // Check if the main log file was rotated, and if so, locate
    // the old file first, and parse the remaining log data.
    if (stat_buf.st_ino != last_state.st_ino) {
      DIR *dirp = NULL;
      struct dirent *dp = NULL;
      ino_t old_inode = last_state.st_ino;

      // Save the current log-file's I-Node number.
      last_state.st_ino = stat_buf.st_ino;

      // Find the old log file.
      dirp = opendir(system_log_dir);
      if (dirp == NULL) {
        strncat(ymon_notice, " can't read log directory", sizeof(ymon_notice) - strlen(ymon_notice) - 1);
        if (ymon_status == YMON_OK)
          ymon_status = YMON_WARNING;
      } else {
        while ((dp = readdir(dirp)) != NULL) {
          if (stat(dp->d_name, &stat_buf) < 0) {
            strncat(ymon_notice, " can't stat ", sizeof(ymon_notice) - strlen(ymon_notice) - 1);
            strncat(ymon_notice, dp->d_name, sizeof(ymon_notice) - strlen(ymon_notice) - 1);
            if (ymon_status == YMON_OK)
              ymon_status = YMON_WARNING;
          } else if (stat_buf.st_ino == old_inode) {
            int old_fd = open(dp->d_name, O_RDONLY);

            if (old_fd < 0) {
              strncat(ymon_notice, " can't open ", sizeof(ymon_notice) - strlen(ymon_notice) - 1);
              strncat(ymon_notice, dp->d_name, sizeof(ymon_notice) - strlen(ymon_notice) - 1);
              if (ymon_status == YMON_OK)
                ymon_status = YMON_WARNING;
              break;            // Don't attempt any more files
            }
            // Process it
            if (process_file(old_fd, last_state.offset, max_age) != 0) {
              strncat(ymon_notice, " can't read ", sizeof(ymon_notice) - strlen(ymon_notice) - 1);
              strncat(ymon_notice, dp->d_name, sizeof(ymon_notice) - strlen(ymon_notice) - 1);
              if (ymon_status == YMON_OK)
                ymon_status = YMON_WARNING;
            }
            close(old_fd);
            break;              // Don't attempt any more files
          }
        }
      }
      // Make sure to read from the beginning of the freshly rotated file.
      last_state.offset = 0;
    } else {
      // Make sure the last_state.offset is sane, stat_buf is for the main_fd.
      if (last_state.offset > stat_buf.st_size)
        last_state.offset = stat_buf.st_size;
    }

    // Process the main file (always)
    if (process_file(main_fd, last_state.offset, max_age) != 0) {
      strncat(ymon_notice, " can't parse log", sizeof(ymon_notice) - strlen(ymon_notice) - 1);
      ymon_status = YMON_CRITICAL;

      last_state.offset = 0;
      last_state.st_ino = 0;
    } else {
      // Save the current file offset.
      last_state.offset = lseek(main_fd, 0, SEEK_CUR);
      if (last_state.offset < 0) {
        strncat(ymon_notice, " can't lseek squid.blog", sizeof(ymon_notice) - strlen(ymon_notice) - 1);
        if (ymon_status == YMON_OK)
          ymon_status = YMON_WARNING;
        last_state.offset = 0;
      }
    }

    // Save the state, release the lock, and close the FDs.
    if (lseek(state_fd, 0, SEEK_SET) < 0) {
      strncat(ymon_notice, " can't lseek state file", sizeof(ymon_notice) - strlen(ymon_notice) - 1);
      if (ymon_status == YMON_OK)
        ymon_status = YMON_WARNING;
    } else {
      if (write(state_fd, &last_state, sizeof(last_state)) == (-1)) {
        strncat(ymon_notice, " can't write state_fd ", sizeof(ymon_notice) - strlen(ymon_notice) - 1);
        if (ymon_status == YMON_OK)
          ymon_status = YMON_WARNING;
      }
    }
    //flock(state_fd, LOCK_UN);
    lck.l_type = F_UNLCK;
    fcntl(state_fd, F_SETLK, &lck);
    close(main_fd);
    close(state_fd);
  } else {
    if (cl.log_file[0] != '\0') {
      main_fd = open(cl.log_file, O_RDONLY);
      if (main_fd < 0) {
        strncat(ymon_notice, " can't open log file ", sizeof(ymon_notice) - strlen(ymon_notice) - 1);
        strncat(ymon_notice, cl.log_file, sizeof(ymon_notice) - strlen(ymon_notice) - 1);
        my_exit(YMON_CRITICAL, ymon_notice);
      }
    } else {
      main_fd = open_main_log(ymon_notice, sizeof(ymon_notice));
    }

    if (cl.tail > 0) {
      if (lseek(main_fd, 0, SEEK_END) < 0) {
        strncat(ymon_notice, " can't lseek squid.blog", sizeof(ymon_notice) - strlen(ymon_notice) - 1);
        my_exit(YMON_CRITICAL, ymon_notice);
      }
      sleep(cl.tail);
    }

    if (process_file(main_fd, 0, max_age) != 0) {
      close(main_fd);
      strncat(ymon_notice, " can't parse log file ", sizeof(ymon_notice) - strlen(ymon_notice) - 1);
      strncat(ymon_notice, cl.log_file, sizeof(ymon_notice) - strlen(ymon_notice) - 1);
      my_exit(YMON_CRITICAL, ymon_notice);
    }
    close(main_fd);
  }

  // All done.
  if (ymon_status == YMON_OK)
    strncat(ymon_notice, " OK", sizeof(ymon_notice) - strlen(ymon_notice) - 1);
  my_exit(ymon_status, ymon_notice);
}
