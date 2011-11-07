/** @file

    ATS plugin to do (simple) regular expression remap rules

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

//////////////////////////////////////////////////////////////////////////////////////////////
//
// To use this plugin, configure a remap.config rule like
//
//   map http://foo.com http://bar.com @plugin=.../libexec/regex_remap.so @pparam=maps.reg
//
// An optional argument (@@pparam) with the string "profile" will enable profiling
// of this regex remap rule, e.g.
//
//   ... @pparam=maps.reg @pparam=profile
//
// Profiling is very low overhead, and the information is dumped to traffic.out, which
// is typically in /usr/local/var/logs/trafficserver/traffic.out. In order to force a profile
// dump, you can do
//
//     $ sudo touch /usr/local/etc/trafficserver/remap.config
//     $ sudo traffic_line -x
//
// By default, only the path (and query arguments etc.) of the URL is provided for the
// regular expressions to match. If you want the full (original) URL, use the parameter
// @pparam=full-url. For example:
//
//    ... @pparam=maps.reg @pparam=full-url
//
// The string that you will need to match against looks like
//
//    http://server.com/path?query=bar
//
// If you also wish to match on the HTTP method used (e.g. "GET"), you must use the
// option @pparam=method. For example:
//
//    ... @pparam=maps.reg @pparam=method
//
// With this enabled, the string that you will need to match will look like
//
//    GET/path?query=bar
//
// The "method" parameter can also be used in combination with "full-url", and the
// string to match against will then look like
//
//    GEThttp://server.com/path?query=bar
//
// The methods are always all upper-case, and always followed by one single space. There
// is no space between the method and the rest of the URL (or URI path).
//
//
// Note that the path to the plugin itself must be absolute, and by default it is
//
//    /usr/local/libexec/trafficserver/regex_remap.so
//
// The config file (maps.reg above) can be placed anywhere, but unless you specify an
// absolute path (as above), it will default to the directory
//
//   /usr/local/etc/regex_remap
//

#define UNUSED __attribute__ ((unused))
static char UNUSED rcsId__regex_remap_cc[] = "@(#) $Id$ built on " __DATE__ " " __TIME__;

#include <sys/types.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <pcre.h>
#include <ctype.h>
#include <unistd.h>

#include <ts/ts.h>
#include <ts/remap.h>

#include <iostream>
#include <fstream>
#include <string>


// This is copied from libinktomi++/*.h, only works with gcc 4.x or later (and compatible).
// TODO: We really ought to expose these data types and atomic functions to the plugin APIs.
typedef int int32;
typedef volatile int32 vint32;
typedef vint32 *pvint32;

static inline int ink_atomic_increment(pvint32 mem, int value)
{
  return __sync_fetch_and_add(mem, value);
}


// Constants
const int OVECCOUNT = 30; // We support $0 - $9 x2 ints, and this needs to be 1.5x that
const int MAX_SUBS = 32; // No more than 32 substitution variables in the subst string

// TODO: This should be "autoconf'ed" or something ...
#define DEFAULT_PATH "/usr/local/etc/regex_remap/"

// Substitutions other than regex matches
enum ExtraSubstitutions {
  SUB_HOST = 11,
  SUB_FROM_HOST = 12,
  SUB_TO_HOST = 13,
  SUB_PORT = 14,
  SUB_SCHEME = 15,
  SUB_PATH = 16,
  SUB_QUERY = 17,
  SUB_COOKIE = 18,
  SUB_CLIENT_IP = 19
};


///////////////////////////////////////////////////////////////////////////////
// Class encapsulating one regular expression (and the linked list).
//
class RemapRegex
{
 public:
  RemapRegex(const std::string& reg, const std::string& sub, const std::string& opt) :
    _num_subs(-1), _rex(NULL), _extra(NULL), _order(-1), _simple(false),
    _active_timeout(-1), _no_activity_timeout(-1), _connect_timeout(-1), _dns_timeout(-1)
  {
    INKDebug("regex_remap", "Calling constructor");

    _status = static_cast<INKHttpStatus>(0);

    if (!reg.empty()) {
      if (reg == ".") {
        INKDebug("regex_remap", "Rule is simple, and fast!");
        _simple = true;
      }
      _rex_string = INKstrdup(reg.c_str());
    } else
      _rex_string = NULL;

    if (!sub.empty()) {
      _subst = INKstrdup(sub.c_str());
      _subst_len = sub.length();
    } else {
      _subst = NULL;
      _subst_len = 0;
    }

    _hits = 0;

    memset(_sub_pos, 0, sizeof(_sub_pos));
    memset(_sub_ix, 0, sizeof(_sub_ix));
    _next = NULL;

    // Parse options
    std::string::size_type start = opt.find_first_of("@");
    std::string::size_type pos1, pos2;

    while (start != std::string::npos) {
      std::string opt_val;

      ++start;
      pos1 = opt.find_first_of("=", start);
      if (pos1 == std::string::npos) {
        INKError("Malformed options: %s", opt.c_str());
        break;
      }
      ++pos1;
      pos2 = opt.find_first_of(" \t\n", pos1);
      if (pos2 == std::string::npos)
        pos2 = opt.length();
      opt_val = opt.substr(pos1, pos2-pos1);

      if (opt.compare(start, 6, "status") == 0) {
        _status = static_cast<INKHttpStatus>(atoi(opt_val.c_str()));
      } else if (opt.compare(start, 14, "active_timeout") == 0) {
        _active_timeout = atoi(opt_val.c_str());
      } else if (opt.compare(start, 19, "no_activity_timeout") == 0) {
        _no_activity_timeout = atoi(opt_val.c_str());
      } else if (opt.compare(start, 15, "connect_timeout") == 0) {
        _connect_timeout = atoi(opt_val.c_str());
      } else if (opt.compare(start, 11, "dns_timeout") == 0) {
        _dns_timeout = atoi(opt_val.c_str());
      } else {
        INKError("Unknown options: %s", opt.c_str());
      }
      start = opt.find_first_of("@", pos2);
    }
  };

  ~RemapRegex()
  {
    INKDebug("regex_remap", "Calling destructor");
    if (_rex_string)
      INKfree(_rex_string);
    if (_subst)
      INKfree(_subst);

    if (_rex)
      pcre_free(_rex);
    if (_extra)
      pcre_free(_extra);
  };

  // For profiling information
  inline void
  print(int ix, int max, const char* now)
  {
    fprintf(stderr, "[%s]:\tRegex %d ( %s ): %.2f%%\n", now, ix, _rex_string, 100.0 * _hits / max);
  }

  inline void
  increment()
  {
    ink_atomic_increment(&(_hits), 1);
  }

  // Compile and study the regular expression.
  int
  compile(const char** error, int* erroffset)
  {
    char* str;
    int ccount;

    _rex = pcre_compile(_rex_string,          // the pattern
                        0,                    // default options
                        error,                // for error message
                        erroffset,            // for error offset
                        NULL);                // use default character tables

    if (NULL == _rex)
      return -1;

    _extra = pcre_study(_rex, 0, error);
    if ((_extra == NULL) && (*error != 0))
      return -1;

    if (pcre_fullinfo(_rex, _extra, PCRE_INFO_CAPTURECOUNT, &ccount) != 0)
      return -1;

    // Get some info for the string substitutions
    str = _subst;
    _num_subs = 0;

    while (str && *str) {
      if ((*str == '$')) {
        int ix = -1;

        if (isdigit(*(str+1))) {
          ix = *(str + 1) - '0';
        } else {
          switch (*(str + 1)) {
          case 'h':
            ix = SUB_HOST;
            break;
          case 'f':
            ix = SUB_FROM_HOST;
            break;
          case 't':
            ix = SUB_TO_HOST;
            break;
          case 'p':
            ix = SUB_PORT;
            break;
          case 's':
            ix = SUB_SCHEME;
            break;
          case 'P':
            ix = SUB_PATH;
            break;
          case 'q':
            ix = SUB_QUERY;
            break;
          case 'c':
            ix = SUB_COOKIE;
            break;
          case 'i':
            ix = SUB_CLIENT_IP;
            break;
          default:
            break;
          }
        }

        if (ix > -1) {
          if ((ix < 10) && (ix > ccount)) {
            INKDebug("regex_remap", "Trying to use unavailable substitution, check the regex!");
            return -1; // No substitutions available other than $0
          }

          _sub_ix[_num_subs] = ix;
          _sub_pos[_num_subs] = (str - _subst);
          str += 2;
          ++_num_subs;
        } else { // Not a valid substitution character, so just ignore it
          ++str;
        }
      } else {
        ++str;
      }
    }
    return 0;
  };

  // Perform the regular expression matching against a string.
  int
  match(const char* str, int len, int ovector[])
  {
    return pcre_exec(_rex,                 // the compiled pattern
                     _extra,               // Extra data from study (maybe)
                     str,                  // the subject string
                     len,                  // the length of the subject
                     0,                    // start at offset 0 in the subject
                     0,                    // default options
                     ovector,              // output vector for substring information
                     OVECCOUNT);           // number of elements in the output vector
  };

  // Get the lengths of the matching string(s), taking into account variable substitutions.
  // We also calculate a total length for the new string, which is the max length the
  // substituted string can have (use it to allocate a buffer before calling substitute() ).
  int
  get_lengths(const int ovector[], int lengths[], TSRemapRequestInfo *rri)
  {
    int len = _subst_len + 1;   // Bigger then necessary

    for (int i=0; i < _num_subs; i++) {
      int ix = _sub_ix[i];

      if (ix < 10) {
        lengths[ix] = ovector[2*ix+1] - ovector[2*ix]; // -1 - -1 == 0
        len += lengths[ix];
      } else {
        switch (ix) {
        case SUB_HOST:
          len += rri->request_host_size;
          break;
        case SUB_FROM_HOST:
          len += rri->remap_from_host_size;
          break;
        case SUB_TO_HOST:
          len += rri->remap_to_host_size;
          break;
        case SUB_PORT:
          len += 6; // One extra for snprintf()
          break;
        case SUB_SCHEME:
          len += rri->from_scheme_len;
          break;
        case SUB_PATH:
          len += rri->request_path_size;
          break;
        case SUB_QUERY:
          len += rri->request_query_size;
          break;
        case SUB_COOKIE:
          len += rri->request_cookie_size;
          break;
        case SUB_CLIENT_IP:
          len += 15; // Allow for 255.255.255.255
          break;
        default:
          break;
        }
      }
    }

    return len;
  };

  // Perform substitution on the $0 - $9 variables in the "src" string. $0 is the entire
  // regex that was matches, while $1 - $9 are the corresponding groups. Return the final
  // length of the string as written to dest (not including the trailing '0').
  int
  substitute(char dest[], const char *src, const int ovector[], const int lengths[], TSRemapRequestInfo *rri)
  {
    if (_num_subs > 0) {
      char* p1 = dest;
      char* p2 = _subst;
      int prev = 0;

      for (int i=0; i < _num_subs; i++) {
        int ix = _sub_ix[i];

        memcpy(p1, p2, _sub_pos[i] - prev);
        p1 += (_sub_pos[i] - prev);
        if (ix < 10) {
          memcpy(p1, src + ovector[2*ix], lengths[ix]);
          p1 += lengths[ix];
        } else {
          switch (ix) {
          case SUB_HOST:
            memcpy(p1, rri->request_host, rri->request_host_size);
            p1 += rri->request_host_size;
            break;
          case SUB_FROM_HOST:
            memcpy(p1, rri->remap_from_host, rri->remap_from_host_size);
            p1 += rri->remap_from_host_size;
            break;
          case SUB_TO_HOST:
            memcpy(p1, rri->remap_to_host, rri->remap_to_host_size);
            p1 += rri->remap_to_host_size;
            break;
          case SUB_PORT:
            p1 += snprintf(p1, 6, "%u", rri->remap_from_port);
            break;
          case SUB_SCHEME:
            memcpy(p1, rri->from_scheme, rri->from_scheme_len);
            p1 += rri->from_scheme_len;
            break;
          case SUB_PATH:
            memcpy(p1, rri->request_path, rri->request_path_size);
            p1 += rri->request_path_size;
            break;
          case SUB_QUERY:
            memcpy(p1, rri->request_query, rri->request_query_size);
            p1 += rri->request_query_size;
            break;
          case SUB_COOKIE:
            memcpy(p1, rri->request_cookie, rri->request_cookie_size);
            p1 += rri->request_cookie_size;
            break;
          case SUB_CLIENT_IP:
            {
              unsigned char *ip = (unsigned char*)&rri->client_ip;

              p1 += snprintf(p1, 15, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
            }
            break;
          default:
            break;
          }
        }
        p2 += (_sub_pos[i] - prev + 2);
        prev = _sub_pos[i] + 2;
      }
      memcpy(p1, p2, _subst_len - (p2 - _subst));
      p1 += _subst_len - (p2 - _subst);
      *p1 = 0; // Make sure it's NULL terminated (for safety).
      return p1 - dest;
    } else {
      memcpy(dest, _subst, _subst_len + 1); // No substitutions in the string, copy it all
      return _subst_len;
    }

    return 0; // Shouldn't happen.
  };

  // setter / getters for members the linked list.
  inline void set_next(RemapRegex* next) { _next = next; };
  inline RemapRegex* next() const { return _next; };

  // setter / getters for order number within the linked list
  inline void set_order(int order) { _order = order; };
  inline int order() { return _order; };

  // Various getters
  inline const char* regex() const { return _rex_string;  };
  inline const char* substitution() const { return _subst;  };
  inline int substitutions_used() const { return _num_subs; }

  inline bool is_simple() const { return _simple; }

  inline INKHttpStatus status_option() const { return _status; };
  inline int active_timeout_option() const  { return _active_timeout; };
  inline int no_activity_timeout_option() const  { return _no_activity_timeout; };
  inline int connect_timeout_option() const  { return _connect_timeout; };
  inline int dns_timeout_option() const  { return _dns_timeout; };

 private:
  char* _rex_string;
  char* _subst;
  int _subst_len;
  int _num_subs;
  int _hits;

  pcre* _rex;
  pcre_extra* _extra;
  int _sub_pos[MAX_SUBS];
  int _sub_ix[MAX_SUBS];
  RemapRegex* _next;
  int _order;
  INKHttpStatus _status;
  bool _simple;
  int _active_timeout;
  int _no_activity_timeout;
  int _connect_timeout;
  int _dns_timeout;
};

struct RemapInstance
{
  RemapInstance() :
    first(NULL), last(NULL), profile(false), full_url(false), method(false), query_string(true),
    matrix_params(false), hits(0), misses(0),
    filename("unknown")
  { };

  RemapRegex* first;
  RemapRegex* last;
  bool profile;
  bool full_url;
  bool method;
  bool query_string;
  bool matrix_params;
  int hits;
  int misses;
  std::string filename;
};

///////////////////////////////////////////////////////////////////////////////
// Helpers for memory management (to make sure pcre uses the INK APIs).
//
inline void*
ink_malloc(size_t s)
{
  return INKmalloc(s);
}

inline void
ink_free(void *s)
{
  return INKfree(s);
}

void
setup_memory_allocation()
{
  pcre_malloc = &ink_malloc;
  pcre_free = &ink_free;
}


///////////////////////////////////////////////////////////////////////////////
// Initialize the plugin.
//
int
tsremap_init(TSREMAP_INTERFACE *api_info, char *errbuf, int errbuf_size)
{
  if (!api_info) {
    strncpy(errbuf, "[tsremap_init] - Invalid TSREMAP_INTERFACE argument", errbuf_size - 1);
    return -1;
  }

  if (api_info->size < sizeof(TSREMAP_INTERFACE)) {
    strncpy(errbuf, "[tsremap_init] - Incorrect size of TSREMAP_INTERFACE structure", errbuf_size - 1);
    return -2;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size - 1, "[tsremap_init] - Incorrect API version %ld.%ld",
             api_info->tsremap_version >> 16, (api_info->tsremap_version & 0xffff));
    return -3;
  }

  setup_memory_allocation();

  INKDebug("regex_remap", "plugin is succesfully initialized");
  return 0;                     /* success */
}


///////////////////////////////////////////////////////////////////////////////
// We don't have any specific "instances" here, at least not yet.
//
int
tsremap_new_instance(int argc, char *argv[], ihandle *ih, char *errbuf, int errbuf_size)
{
  const char* error;
  int erroffset;
  RemapInstance* ri = new RemapInstance();

  std::ifstream f;
  int lineno = 0;
  int count = 0;

  *ih = (ihandle)ri;
  if (ri == NULL) {
    INKError("Unable to create remap instance");
    return -5;
  }

  // Really simple (e.g. basic) config parser
  for (int i=2; i < argc; ++i) {
    if (strncmp(argv[i], "profile", 7) == 0) {
      ri->profile = true;
    } else if (strncmp(argv[i], "no-profile", 10) == 0) {
      ri->profile = false;
    } else if (strncmp(argv[i], "full-url", 8) == 0) {
      ri->full_url = true;
    } else if (strncmp(argv[i], "no-full-url", 11) == 0) {
      ri->full_url = false;
    } else if (strncmp(argv[i], "method", 6) == 0) {
      ri->method = true;
    } else if (strncmp(argv[i], "no-method", 9) == 0) {
      ri->method = true;
    } else if (strncmp(argv[i], "query-string", 12) == 0) {
      ri->query_string = true;
    } else if (strncmp(argv[i], "no-query-string", 15) == 0) {
      ri->query_string = false;
    } else if (strncmp(argv[i], "matrix-parameters", 15) == 0) {
      ri->matrix_params = true;
    } else if (strncmp(argv[i], "no-matrix-parameters", 18) == 0) {
      ri->matrix_params = false;
    } else {
      if (0 != access(argv[2], R_OK)) {
        ri->filename = DEFAULT_PATH;
        ri->filename += argv[2];
      } else {
        ri->filename = argv[2];
      }

      f.open((ri->filename).c_str(), std::ios::in);
      if (!f.is_open()) { // Try with the default path instead
        INKError("unable to open %s", (ri->filename).c_str());
        return -4;
      }
      INKDebug("regex_remap", "loading regular expression maps from %s", (ri->filename).c_str());

      while (!f.eof()) {
        std::string line, regex, subst, options;
        std::string::size_type pos1, pos2;

        getline(f, line);
        ++lineno;
        if (line.empty())
          continue;
        pos1 = line.find_first_not_of(" \t\n");
        if (line[pos1] == '#')
          continue;  // Skip comment lines

        if (pos1 != std::string::npos) {
          pos2 = line.find_first_of(" \t\n", pos1);
          if (pos2 != std::string::npos) {
            regex = line.substr(pos1, pos2-pos1);
            pos1 = line.find_first_not_of(" \t\n#", pos2);
            if (pos1 != std::string::npos) {
              pos2 = line.find_first_of(" \t\n", pos1);
              if (pos2 == std::string::npos)
                pos2 = line.length();
              subst = line.substr(pos1, pos2-pos1);
              pos1 = line.find_first_not_of(" \t\n#", pos2);
              if (pos1 != std::string::npos) {
                pos2 = line.find_first_of("\n#", pos1);
                if (pos2 == std::string::npos)
                  pos2 = line.length();
                options = line.substr(pos1, pos2-pos1);
              }
            }
          }
        }

        if (regex.empty()) {
          // No regex found on this line
          INKError("no regexp found in %s: line %d", (ri->filename).c_str(), lineno);
          continue;
        }
        if (subst.empty() && options.empty()) {
          // No substitution found on this line (and no options)
          INKError("no substitution string found in %s: line %d", (ri->filename).c_str(), lineno);
          continue;
        }

        // Got a regex and substitution string
        RemapRegex* cur = new RemapRegex(regex, subst, options);

        if (cur == NULL) {
          INKError("can't create a new regex remap rule");
          continue;
        }

        if (cur->compile(&error, &erroffset) < 0) {
          INKError("PCRE failed in %s (line %d) at offset %d: %s\n", (ri->filename).c_str(), lineno, erroffset, error);
          delete(cur);
        } else {
          INKDebug("regex_remap", "added regex=%s with substitution=%s and options `%s'",
                   regex.c_str(), subst.c_str(), options.c_str());
          cur->set_order(++count);
          if (ri->first == NULL)
            ri->first = cur;
          else
            ri->last->set_next(cur);
          ri->last = cur;
        }
      }
    }
  }

  // Make sure we got something...
  if (ri->first == NULL) {
    INKError("Got no regular expressions from the maps");
    return -1;
  }

  return 0;
}


void
tsremap_delete_instance(ihandle ih)
{
  RemapInstance* ri = static_cast<RemapInstance*>(ih);
  RemapRegex* re = ri->first;
  RemapRegex* tmp;
  int ix = 1;
  char now[64];
  time_t tim = time(NULL);

  if (ri->profile) {
    if (ctime_r(&tim, now))
      now[strlen(now) - 1] = '\0';
    else {
      memcpy(now, "unknown time", 12);
      *(now + 12) = '\0';
    }

    fprintf(stderr, "[%s]: Profiling information for regex_remap file `%s':\n", now, (ri->filename).c_str());
    fprintf(stderr, "[%s]:\tTotal hits (matches): %d\n", now, ri->hits);
    fprintf(stderr, "[%s]:\tTotal missed (no regex matches): %d\n", now, ri->misses);
  }

  if (ri->hits > 0) { // Avoid divide by zeros...
    while (re) {
      if (ri->profile)
        re->print(ix, ri->hits, now);
      tmp = re->next();
      delete re;
      re = tmp;
      ++ix;
    }
  }

  delete ri;
}


///////////////////////////////////////////////////////////////////////////////
// This is the main "entry" point for the plugin, called for every request.
//
int
tsremap_remap(ihandle ih, rhandle rh, TSRemapRequestInfo *rri)
{
  if (NULL == ih) {
    INKDebug("regex_remap", "Falling back to default URL on regex remap without rules");
    return 0;
  } else {
    RemapInstance* ri = (RemapInstance*)ih;
    int ovector[OVECCOUNT];
    int lengths[OVECCOUNT/2 + 1];
    int dest_len;
    int retval = 1;
    RemapRegex* re = ri->first;
    char match_buf[rri->orig_url_size + 16 + 5]; // Worst case scenario and padded for /,? and ;
    int match_len = 0;

    if (ri->method) { // Prepend the URI path or URL with the HTTP method
      INKMBuffer mBuf;
      INKMLoc reqHttpHdrLoc;
      const char *method;

      // Note that Method can not be longer than 16 bytes, or we'll simply truncate it
      if (INKHttpTxnClientReqGet(static_cast<INKHttpTxn>(rh), &mBuf, &reqHttpHdrLoc)) {
        method = INKHttpHdrMethodGet(mBuf, reqHttpHdrLoc, &match_len);
        if (method && (match_len > 0)) {
          if (match_len > 16)
            match_len = 16;
          memcpy(match_buf, method, match_len);
          INKHandleStringRelease(mBuf, reqHttpHdrLoc, method);
        }
      }
    }

    if (ri->full_url) { // This ignores all the other match string options
      memcpy(match_buf + match_len, rri->orig_url, rri->orig_url_size);
      match_len += rri->orig_url_size;
    } else {
      *(match_buf + match_len) = '/';
      memcpy(match_buf + match_len + 1, rri->request_path, rri->request_path_size);
      match_len += rri->request_path_size + 1;

      // Todo: This is crazy, but for now, we have to parse the URL to extract (possibly) the
      // matrix parameters from the original URL, and append them.
      // XXX: This can now be fixed in Apache.
      if (ri->matrix_params) {
        INKMBuffer bufp = (INKMBuffer)INK_ERROR_PTR;
        INKMLoc url_loc = (INKMLoc)INK_ERROR_PTR;
        const char* start = rri->orig_url;
        const char* params;
        int params_len;

        bufp = INKMBufferCreate();
        if (bufp == INK_ERROR_PTR) {
          INKHttpTxnSetHttpRetStatus((INKHttpTxn)rh, (INKHttpStatus)500);
          INKError("can't create MBuffer");
          goto param_error;
        }

        url_loc = INKUrlCreate(bufp);
        if (url_loc == INK_ERROR_PTR) {
          INKHttpTxnSetHttpRetStatus((INKHttpTxn)rh, (INKHttpStatus)500);
          INKError("can't create URL buffer");
          goto param_error;
        }

        if (INKUrlParse(bufp, url_loc, &start, start + rri->orig_url_size) == INK_PARSE_ERROR) {
          INKHttpTxnSetHttpRetStatus((INKHttpTxn)rh, (INKHttpStatus)500);
          INKError("can't parse original URL string");
          goto param_error;
        }

        params = INKUrlHttpParamsGet(bufp, url_loc, &params_len);
        if (params && (params_len > 0)) {
          *(match_buf + match_len) = ';';
          memcpy(match_buf + match_len + 1 , params, params_len);
          match_len += (params_len + 1);
          INKHandleStringRelease(bufp, url_loc, params);
        }

      param_error:
        if (url_loc != INK_ERROR_PTR) {
          INKUrlDestroy(bufp, url_loc);
          INKHandleMLocRelease(bufp, INK_NULL_MLOC, url_loc);
        }
        if (bufp != INK_ERROR_PTR)
          INKMBufferDestroy(bufp);
      }

      if (ri->query_string && (rri->request_query_size > 0)) {
        *(match_buf + match_len) = '?';
        memcpy(match_buf + match_len + 1 , rri->request_query, rri->request_query_size);
        match_len += (rri->request_query_size + 1);
      }
    }
    match_buf[match_len] = '\0'; // NULL terminate the match string

    INKDebug("regex_remap", "original match string is %s (length %d out of %d)", match_buf,
             match_len, rri->orig_url_size + 16 + 5);
    INKReleaseAssert(match_len < (rri->orig_url_size + 16 + 5)); // Just in case ...

    // Apply the regular expressions, in order. First one wins.
    while (re) {
      // Since we check substitutions on parse time, we don't need to reset ovector
      if (re->is_simple() || (re->match(match_buf, match_len, ovector) != (-1))) {
        int new_len = re->get_lengths(ovector, lengths, rri);

        // Set timeouts
        if (re->active_timeout_option() > (-1)) {
          INKDebug("regex_remap", "Setting active timeout to %d", re->active_timeout_option());
          INKHttpTxnActiveTimeoutSet((INKHttpTxn)rh, re->active_timeout_option());
        }
        if (re->no_activity_timeout_option() > (-1)) {
          INKDebug("regex_remap", "Setting no activity timeout to %d", re->no_activity_timeout_option());
          INKHttpTxnNoActivityTimeoutSet((INKHttpTxn)rh, re->no_activity_timeout_option());
        }
        if (re->connect_timeout_option() > (-1)) {
          INKDebug("regex_remap", "Setting connect timeout to %d", re->connect_timeout_option());
          INKHttpTxnConnectTimeoutSet((INKHttpTxn)rh, re->connect_timeout_option());
        }
        if (re->dns_timeout_option() > (-1)) {
          INKDebug("regex_remap", "Setting DNS timeout to %d", re->dns_timeout_option());
          INKHttpTxnDNSTimeoutSet((INKHttpTxn)rh, re->dns_timeout_option());
        }

        // Update profiling if requested
        if (ri->profile) {
          re->increment();
          ink_atomic_increment(&(ri->hits), 1);
        }

        if (new_len > 0) {
          char dest[new_len+8];

          dest_len = re->substitute(dest, match_buf, ovector, lengths, rri);

          INKDebug("regex_remap", "New URL is estimated to be %d bytes long, or less\n", new_len);
          INKDebug("regex_remap", "New URL is %s (length %d)", dest, dest_len);
          INKDebug("regex_remap", "    matched rule %d [%s]", re->order(), re->regex());

          // Check for a quick response, if the status option is set
          if (re->status_option() > 0) {
            INKHttpTxnSetHttpRetStatus((INKHttpTxn)rh, re->status_option());
            if ((re->status_option() == (INKHttpStatus)301) || (re->status_option() == (INKHttpStatus)302)) {
              if (dest_len > TSREMAP_RRI_MAX_REDIRECT_URL) {
                INKError("Redirect in target URL too long");
                INKHttpTxnSetHttpRetStatus((INKHttpTxn)rh, INK_HTTP_STATUS_REQUEST_URI_TOO_LONG);
              } else {
                memcpy(rri->redirect_url, dest, dest_len);
                rri->redirect_url_size = dest_len;
              }
            }
            return 1;
          }

          if (dest_len > 0) {
            INKMBuffer bufp = (INKMBuffer)INK_ERROR_PTR;
            INKMLoc url_loc = (INKMLoc)INK_ERROR_PTR;
            const char *temp;
            const char *start = dest;
            int len, port;

            bufp = INKMBufferCreate();
            if (bufp == INK_ERROR_PTR) {
              INKHttpTxnSetHttpRetStatus((INKHttpTxn)rh, (INKHttpStatus)500);
              INKError("can't create MBuffer");
              goto error;
            }

            url_loc = INKUrlCreate(bufp);
            if (url_loc == INK_ERROR_PTR) {
              INKHttpTxnSetHttpRetStatus((INKHttpTxn)rh, (INKHttpStatus)500);
              INKError("can't create URL buffer");
              goto error;
            }

            if (INKUrlParse(bufp, url_loc, &start, start + dest_len) == INK_PARSE_ERROR) {
              INKHttpTxnSetHttpRetStatus((INKHttpTxn)rh, (INKHttpStatus)500);
              INKError("can't parse substituted URL string");
              goto error;
            }

            // Update the Host (if necessary)
            temp = INKUrlHostGet(bufp, url_loc, &len);
            if (len > TSREMAP_RRI_MAX_HOST_SIZE) {
              INKError("Host in target URL too long");
              INKHttpTxnSetHttpRetStatus((INKHttpTxn)rh, INK_HTTP_STATUS_REQUEST_URI_TOO_LONG);
              INKHandleStringRelease(bufp, url_loc, temp);
              goto error;
            } else {
              if (temp && (len > 0) && ((len != rri->remap_to_host_size) || strncmp(temp, rri->remap_to_host, len))) {
                INKDebug("regex_remap", "new host string: %s (len = %d)", temp, len);
                memcpy(rri->new_host, temp, len);
                rri->new_host_size = len;
                INKHandleStringRelease(bufp, url_loc, temp);
              }
            }

            // Update the Path (if necessary)
            // ToDo: Should we maybe compare with rri->remap_to_path + rri->request_path ?
            temp = INKUrlPathGet(bufp, url_loc, &len);
            if (len > TSREMAP_RRI_MAX_PATH_SIZE) {
              INKError("Path in target URL too long");
              INKHttpTxnSetHttpRetStatus((INKHttpTxn)rh, INK_HTTP_STATUS_REQUEST_URI_TOO_LONG);
              INKHandleStringRelease(bufp, url_loc, temp);
              goto error;
            } else {
              if (!temp || (len <= 0)) {
                if (rri->request_path_size > 0) {
                  rri->new_path_size = -1;
                  INKDebug("regex_remap", "new path is empty");
                }
              } else {
                if (*temp == '/') {
                  ++temp;
                  --len;
                }
                // ToDo: This is an ugly hack, since we must "modify" the path for the parameter hack
                // to function :/.
                if ((len != rri->request_path_size) || strncmp(temp, rri->request_path, len)) {
                  INKDebug("regex_remap", "new path string: %s (len = %d)", temp, len);
                }
                memcpy(rri->new_path, temp, len);
                rri->new_path_size = len;
                INKHandleStringRelease(bufp, url_loc, temp);
              }
            }

            // Update the matrix parameters (if necessary). TODO: This is very hacky right now,
            // instead of appending to the new_path, we really ought to set the matrix parameters
            // using a proper API (which is not supported at this point). This only works if
            // there is a path as well.
            if (rri->new_path_size != (-1)) {
              temp = INKUrlHttpParamsGet(bufp, url_loc, &len);
              if (len >= (TSREMAP_RRI_MAX_PATH_SIZE - rri->new_path_size - 3)) {
                INKError("Matrix parameters in target URL too long");
                INKHttpTxnSetHttpRetStatus((INKHttpTxn)rh, INK_HTTP_STATUS_REQUEST_URI_TOO_LONG);
                INKHandleStringRelease(bufp, url_loc, temp);
                goto error;
              } else {
                if (temp && (len > 0)) {
                  *(rri->new_path + rri->new_path_size) = ';';
                  memcpy(rri->new_path + rri->new_path_size + 1, temp, len);
                  rri->new_path_size += (len + 1);
                  INKHandleStringRelease(bufp, url_loc, temp);
                  INKDebug("regex_remap", "appending matrix parameters: %.*s", len, temp);
                  INKDebug("regex_remap", "new path string: %.*s", rri->new_path_size, rri->new_path);
                }
              }
            }

            // Update the Query string (if necessary)
            temp = INKUrlHttpQueryGet(bufp, url_loc, &len);
            if (len > TSREMAP_RRI_MAX_PATH_SIZE) {
              INKError("Query in target URL too long");
              INKHttpTxnSetHttpRetStatus((INKHttpTxn)rh, INK_HTTP_STATUS_REQUEST_URI_TOO_LONG);
              INKHandleStringRelease(bufp, url_loc, temp);
              goto error;
            } else {
              if (!temp || (len <= 0)) {
                if (rri->request_query_size > 0) {
                  *(rri->new_query) = 0;
                  rri->new_query_size = -1; // Delete it
                  INKDebug("regex_remap", "new query is empty");
                }
              } else {
                if (*temp == '?') {
                  ++temp;
                  --len;
                }
                if ((len != rri->request_query_size) || strncmp(temp, rri->request_query, len)) {
                  INKDebug("regex_remap", "new query string: %s (len = %d)", temp, len);
                  memcpy(rri->new_query, temp, len);
                  rri->new_query_size = len;
                }
                INKHandleStringRelease(bufp, url_loc, temp);
              }
            }

            // Update the port number (if necessary)
            port = INKUrlPortGet(bufp, url_loc);
            if (port != rri->remap_to_port) {
              INKDebug("regex_remap", "new port: %d", port);
              rri->new_port = port;
            }

            // Update the scheme (HTTP or HTTPS)
            temp = INKUrlSchemeGet(bufp, url_loc, &len);
            if (temp && (len > 0) && (len != rri->to_scheme_len)) {
              if (!strncmp(temp, INK_URL_SCHEME_HTTPS, INK_URL_LEN_HTTPS)) {
                rri->require_ssl = 1;
                INKDebug("regex_remap", "changing scheme to HTTPS");
              } else {
                rri->require_ssl = 0;
                INKDebug("regex_remap", "changing scheme to HTTP");
              }
              INKHandleStringRelease(bufp, url_loc, temp);
            }

            // Cleanup
          error:
            if (url_loc != INK_ERROR_PTR) {
              INKUrlDestroy(bufp, url_loc);
              INKHandleMLocRelease(bufp, INK_NULL_MLOC, url_loc);
            }
            if (bufp != INK_ERROR_PTR)
              INKMBufferDestroy(bufp);
          }
          break; // We're done
        }
      }
      re = re->next();
      if (re == NULL) {
        retval = 0; // No match
        if (ri->profile)
          ink_atomic_increment(&(ri->misses), 1);
      }
    }
    return retval;
  }

  // This shouldn't happen, but just in case
  return 0;
}



/*
  local variables:
  mode: C++
  indent-tabs-mode: nil
  c-basic-offset: 2
  c-comment-only-line-offset: 0
  c-file-offsets: ((statement-block-intro . +)
  (label . 0)
  (statement-cont . +)
  (innamespace . 0))
  end:

  Indent with: /usr/bin/indent -ncs -nut -npcs -l 120 logstats.cc
*/
