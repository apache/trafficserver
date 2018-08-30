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
#include "ts/ts.h"
#include "ts/remap.h"

#include <sys/types.h>
#include <cstdio>
#include <ctime>
#include <cstring>

#include <cctype>
#include <unistd.h>

#include <iostream>
#include <fstream>
#include <string>
#include <cctype>
#include <memory>
#include <sstream>

// Get some specific stuff from libts, yes, we can do that now that we build inside the core.
#include "tscore/ink_platform.h"
#include "tscore/ink_atomic.h"
#include "tscore/ink_time.h"
#include "tscore/ink_inet.h"

#ifdef HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#else
#include <pcre.h>
#endif

static const char *PLUGIN_NAME = "regex_remap";

// Constants
static const int OVECCOUNT = 30; // We support $0 - $9 x2 ints, and this needs to be 1.5x that
static const int MAX_SUBS  = 32; // No more than 32 substitution variables in the subst string

// Substitutions other than regex matches
enum ExtraSubstitutions {
  SUB_HOST       = 11,
  SUB_FROM_HOST  = 12,
  SUB_TO_HOST    = 13,
  SUB_PORT       = 14,
  SUB_SCHEME     = 15,
  SUB_PATH       = 16,
  SUB_QUERY      = 17,
  SUB_MATRIX     = 18,
  SUB_CLIENT_IP  = 19,
  SUB_LOWER_PATH = 20,
};

///////////////////////////////////////////////////////////////////////////////
// Class holding one request URL's component, to simplify the code and
// length calculations (we need all of them).
//
struct UrlComponents {
  UrlComponents()
    : scheme(nullptr),
      host(nullptr),
      path(nullptr),
      query(nullptr),
      matrix(nullptr),
      port(0),
      scheme_len(0),
      host_len(0),
      path_len(0),
      query_len(0),
      matrix_len(0),
      url_len(0)
  {
  }

  void
  populate(TSRemapRequestInfo *rri)
  {
    scheme = TSUrlSchemeGet(rri->requestBufp, rri->requestUrl, &scheme_len);
    host   = TSUrlHostGet(rri->requestBufp, rri->requestUrl, &host_len);
    path   = TSUrlPathGet(rri->requestBufp, rri->requestUrl, &path_len);
    query  = TSUrlHttpQueryGet(rri->requestBufp, rri->requestUrl, &query_len);
    matrix = TSUrlHttpParamsGet(rri->requestBufp, rri->requestUrl, &matrix_len);
    port   = TSUrlPortGet(rri->requestBufp, rri->requestUrl);

    url_len = scheme_len + host_len + path_len + query_len + matrix_len + 32;
  }

  const char *scheme;
  const char *host;
  const char *path;
  const char *query;
  const char *matrix;
  int port;

  int scheme_len;
  int host_len;
  int path_len;
  int query_len;
  int matrix_len;

  int url_len; // Full length, of all components
};

///////////////////////////////////////////////////////////////////////////////
// Class encapsulating one regular expression (and the linked list).
//
class RemapRegex
{
public:
  ~RemapRegex()
  {
    TSDebug(PLUGIN_NAME, "Calling destructor");
    TSfree(_rex_string);
    TSfree(_subst);

    if (_rex) {
      pcre_free(_rex);
    }
    if (_extra) {
      pcre_free(_extra);
    }
  }

  bool initialize(const std::string &reg, const std::string &sub, const std::string &opt);

  // For profiling information
  void
  increment()
  {
    ink_atomic_increment(&(_hits), 1);
  }
  void
  print(int ix, int max, const char *now)
  {
    fprintf(stderr, "[%s]:    Regex %d ( %s ): %.2f%%\n", now, ix, _rex_string, 100.0 * _hits / max);
  }

  int compile(const char *&error, int &erroffset);

  // Perform the regular expression matching against a string.
  int
  match(const char *str, int len, int ovector[])
  {
    return pcre_exec(_rex,       // the compiled pattern
                     _extra,     // Extra data from study (maybe)
                     str,        // the subject string
                     len,        // the length of the subject
                     0,          // start at offset 0 in the subject
                     0,          // default options
                     ovector,    // output vector for substring information
                     OVECCOUNT); // number of elements in the output vector
  }

  // Substitutions
  int get_lengths(const int ovector[], int lengths[], TSRemapRequestInfo *rri, UrlComponents *req_url);
  int substitute(char dest[], const char *src, const int ovector[], const int lengths[], TSHttpTxn txnp, TSRemapRequestInfo *rri,
                 UrlComponents *req_url, bool lowercase_substitutions);

  // setter / getters for members the linked list.
  inline void
  set_next(RemapRegex *next)
  {
    _next = next;
  }
  inline RemapRegex *
  next() const
  {
    return _next;
  }

  // setter / getters for order number within the linked list
  inline void
  set_order(int order)
  {
    _order = order;
  }
  inline int
  order()
  {
    return _order;
  }

  // Various getters
  inline const char *
  regex() const
  {
    return _rex_string;
  }
  inline bool
  regex_empty() const
  {
    return !_rex_string || !*_rex_string;
  }
  inline const char *
  substitution() const
  {
    return _subst;
  }
  inline int
  substitutions_used() const
  {
    return _num_subs;
  }

  inline TSHttpStatus
  status_option() const
  {
    return _status;
  }
  inline int
  active_timeout_option() const
  {
    return _active_timeout;
  }
  inline int
  no_activity_timeout_option() const
  {
    return _no_activity_timeout;
  }
  inline int
  connect_timeout_option() const
  {
    return _connect_timeout;
  }
  inline int
  dns_timeout_option() const
  {
    return _dns_timeout;
  }
  inline bool
  lowercase_substitutions_option() const
  {
    return _lowercase_substitutions;
  }

  // Hold an overridable configurations
  struct Override {
    TSOverridableConfigKey key;
    TSRecordDataType type;
    TSRecordData data;
    int data_len; // Used when data is a string
    Override *next;
  };

  Override *
  get_overrides() const
  {
    return _first_override;
  }

private:
  char *_rex_string = nullptr;
  char *_subst      = nullptr;
  int _subst_len    = 0;
  int _num_subs     = -1;
  int _hits         = 0;
  int _options      = 0;
  int _order        = -1;

  bool _lowercase_substitutions = false;

  pcre *_rex           = nullptr;
  pcre_extra *_extra   = nullptr;
  RemapRegex *_next    = nullptr;
  TSHttpStatus _status = static_cast<TSHttpStatus>(0);

  int _active_timeout      = -1;
  int _no_activity_timeout = -1;
  int _connect_timeout     = -1;
  int _dns_timeout         = -1;

  Override *_first_override = nullptr;
  int _sub_pos[MAX_SUBS];
  int _sub_ix[MAX_SUBS];
};

bool
RemapRegex::initialize(const std::string &reg, const std::string &sub, const std::string &opt)
{
  if (!reg.empty()) {
    _rex_string = TSstrdup(reg.c_str());
  }

  if (!sub.empty()) {
    _subst     = TSstrdup(sub.c_str());
    _subst_len = sub.length();
  }

  memset(_sub_pos, 0, sizeof(_sub_pos));
  memset(_sub_ix, 0, sizeof(_sub_ix));

  // Parse options
  std::string::size_type start = opt.find_first_of('@');
  std::string::size_type pos1, pos2;
  Override *last_override = nullptr;

  while (start != std::string::npos) {
    std::string opt_val;

    ++start;
    pos1 = opt.find_first_of('=', start);
    pos2 = opt.find_first_of(" \t\n", pos1);
    if (pos2 == std::string::npos) {
      pos2 = opt.length();
    }

    if (pos1 != std::string::npos) {
      // Get the value as well
      ++pos1;
      opt_val = opt.substr(pos1, pos2 - pos1);
    }

    // These take an option 0|1 value, without value it implies 1
    if (opt.compare(start, 8, "caseless") == 0) {
      _options |= PCRE_CASELESS;
    } else if (opt.compare(start, 23, "lowercase_substitutions") == 0) {
      _lowercase_substitutions = true;
    } else if (opt_val.size() <= 0) {
      // All other options have a required value
      TSError("[%s] Malformed options: %s", PLUGIN_NAME, opt.c_str());
      break;
    } else if (opt.compare(start, 6, "status") == 0) {
      _status = static_cast<TSHttpStatus>(strtol(opt_val.c_str(), nullptr, 10));
    } else if (opt.compare(start, 14, "active_timeout") == 0) {
      _active_timeout = strtol(opt_val.c_str(), nullptr, 10);
    } else if (opt.compare(start, 19, "no_activity_timeout") == 0) {
      _no_activity_timeout = strtol(opt_val.c_str(), nullptr, 10);
    } else if (opt.compare(start, 15, "connect_timeout") == 0) {
      _connect_timeout = strtol(opt_val.c_str(), nullptr, 10);
    } else if (opt.compare(start, 11, "dns_timeout") == 0) {
      _dns_timeout = strtol(opt_val.c_str(), nullptr, 10);
    } else {
      TSOverridableConfigKey key;
      TSRecordDataType type;
      std::string opt_name = opt.substr(start, pos1 - start - 1);

      if (TS_SUCCESS == TSHttpTxnConfigFind(opt_name.c_str(), opt_name.length(), &key, &type)) {
        std::unique_ptr<Override> cur(new Override);

        TSReleaseAssert(cur.get());
        switch (type) {
        case TS_RECORDDATATYPE_INT:
          cur->data.rec_int = strtoll(opt_val.c_str(), nullptr, 10);
          break;
        case TS_RECORDDATATYPE_FLOAT:
          cur->data.rec_float = strtof(opt_val.c_str(), nullptr);
          break;
        case TS_RECORDDATATYPE_STRING:
          cur->data.rec_string = TSstrdup(opt_val.c_str());
          cur->data_len        = opt_val.size();
          break;
        default:
          TSError("[%s] configuration variable '%s' is of an unsupported type", PLUGIN_NAME, opt_name.c_str());
          return false;
        }
        TSDebug(PLUGIN_NAME, "Overridable config %s=%s", opt_name.c_str(), opt_val.c_str());
        cur->key  = key;
        cur->type = type;
        cur->next = nullptr;
        auto tmp  = cur.get();
        if (nullptr == last_override) {
          _first_override = cur.release();
        } else {
          last_override->next = cur.release();
        }
        last_override = tmp;
      } else {
        TSError("[%s] Unknown options: %s", PLUGIN_NAME, opt.c_str());
      }
    }
    start = opt.find_first_of('@', pos2);
  }

  return true;
}

// Compile and study the regular expression.
int
RemapRegex::compile(const char *&error, int &erroffset)
{
  char *str;
  int ccount;

  // Initialize these in case they are not set.
  error     = "unknown error";
  erroffset = -1;

  _rex = pcre_compile(_rex_string, // the pattern
                      _options,    // options
                      &error,      // for error message
                      &erroffset,  // for error offset
                      nullptr);    // use default character tables

  if (nullptr == _rex) {
    return -1;
  }

  _extra = pcre_study(_rex, 0, &error);
  if ((_extra == nullptr) && (error != nullptr)) {
    return -1;
  }

  if (pcre_fullinfo(_rex, _extra, PCRE_INFO_CAPTURECOUNT, &ccount) != 0) {
    error = "call to pcre_fullinfo() failed";
    return -1;
  }

  // Get some info for the string substitutions
  str       = _subst;
  _num_subs = 0;

  while (str && *str) {
    if ('$' == *str) {
      int ix = -1;

      if (isdigit(*(str + 1))) {
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
        case 'l':
          ix = SUB_LOWER_PATH;
          break;
        case 'q':
          ix = SUB_QUERY;
          break;
        case 'm':
          ix = SUB_MATRIX;
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
          error = "using unavailable captured substring ($n) in substitution";
          return -1;
        }

        _sub_ix[_num_subs]  = ix;
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
}

// Get the lengths of the matching string(s), taking into account variable substitutions.
// We also calculate a total length for the new string, which is the max length the
// substituted string can have (use it to allocate a buffer before calling substitute() ).
int
RemapRegex::get_lengths(const int ovector[], int lengths[], TSRemapRequestInfo *rri, UrlComponents *req_url)
{
  int len = _subst_len + 1; // Bigger then necessary

  for (int i = 0; i < _num_subs; i++) {
    int ix = _sub_ix[i];

    if (ix < 10) {
      lengths[ix] = ovector[2 * ix + 1] - ovector[2 * ix]; // -1 - -1 == 0
      len += lengths[ix];
    } else {
      int tmp_len;

      switch (ix) {
      case SUB_HOST:
        len += req_url->host_len;
        break;
      case SUB_FROM_HOST:
        TSUrlHostGet(rri->requestBufp, rri->mapFromUrl, &tmp_len);
        len += tmp_len;
        break;
      case SUB_TO_HOST:
        TSUrlHostGet(rri->requestBufp, rri->mapToUrl, &tmp_len);
        len += tmp_len;
        break;
      case SUB_PORT:
        len += 6; // One extra for snprintf()
        break;
      case SUB_SCHEME:
        len += req_url->scheme_len;
        break;
      case SUB_PATH:
      case SUB_LOWER_PATH:
        len += req_url->path_len;
        break;
      case SUB_QUERY:
        len += req_url->query_len;
        break;
      case SUB_MATRIX:
        len += req_url->matrix_len;
        break;
      case SUB_CLIENT_IP:
        len += INET6_ADDRSTRLEN;
        break;
      default:
        break;
      }
    }
  }

  return len;
}

// Perform substitution on the $0 - $9 variables in the "src" string. $0 is the entire
// regex that was matches, while $1 - $9 are the corresponding groups. Return the final
// length of the string as written to dest (not including the trailing '0').
int
RemapRegex::substitute(char dest[], const char *src, const int ovector[], const int lengths[], TSHttpTxn txnp,
                       TSRemapRequestInfo *rri, UrlComponents *req_url, bool lowercase_substitutions)
{
  if (_num_subs > 0) {
    char *p1 = dest;
    char *p2 = _subst;
    int prev = 0;

    for (int i = 0; i < _num_subs; i++) {
      char *start = p1;
      int ix      = _sub_ix[i];

      memcpy(p1, p2, _sub_pos[i] - prev);
      p1 += (_sub_pos[i] - prev);
      if (ix < 10) {
        memcpy(p1, src + ovector[2 * ix], lengths[ix]);
        p1 += lengths[ix];
      } else {
        char buff[INET6_ADDRSTRLEN];
        const char *str = nullptr;
        int len         = 0;

        switch (ix) {
        case SUB_HOST:
          str = req_url->host;
          len = req_url->host_len;
          break;
        case SUB_FROM_HOST:
          str = TSUrlHostGet(rri->requestBufp, rri->mapFromUrl, &len);
          break;
        case SUB_TO_HOST:
          str = TSUrlHostGet(rri->requestBufp, rri->mapToUrl, &len);
          break;
        case SUB_PORT:
          p1 += snprintf(p1, 6, "%u", req_url->port);
          break;
        case SUB_SCHEME:
          str = req_url->scheme;
          len = req_url->scheme_len;
          break;
        case SUB_PATH:
        case SUB_LOWER_PATH:
          str = req_url->path;
          len = req_url->path_len;
          break;
        case SUB_QUERY:
          str = req_url->query;
          len = req_url->query_len;
          break;
        case SUB_MATRIX:
          str = req_url->matrix;
          len = req_url->matrix_len;
          break;
        case SUB_CLIENT_IP:
          str = ats_ip_ntop(TSHttpTxnClientAddrGet(txnp), buff, INET6_ADDRSTRLEN);
          len = strlen(str);
          break;
        default:
          break;
        }
        // If one of the rules fetched a read-only string, copy it in.
        if (str && len > 0) {
          memcpy(p1, str, len);
          p1 += len;
        }
      }
      p2 += (_sub_pos[i] - prev + 2);
      prev = _sub_pos[i] + 2;

      if (lowercase_substitutions == true || ix == SUB_LOWER_PATH) {
        while (start < p1) {
          *start = tolower(*start);
          start++;
        }
      }
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
}

// Hold one remap instance
struct RemapInstance {
  RemapInstance()
    : first(nullptr),
      last(nullptr),
      profile(false),
      method(false),
      query_string(true),
      matrix_params(false),
      host(false),
      hits(0),
      misses(0),
      filename("unknown")
  {
  }

  RemapRegex *first;
  RemapRegex *last;
  bool profile;
  bool method;
  bool query_string;
  bool matrix_params;
  bool host;
  int hits;
  int misses;
  std::string filename;
};

///////////////////////////////////////////////////////////////////////////////
// Helpers for memory management (to make sure pcre uses the TS APIs).
//
inline void *
ts_malloc(size_t s)
{
  return TSmalloc(s);
}

inline void
ts_free(void *s)
{
  return TSfree(s);
}

void
setup_memory_allocation()
{
  pcre_malloc = &ts_malloc;
  pcre_free   = &ts_free;
}

///////////////////////////////////////////////////////////////////////////////
// Initialize the plugin.
//
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  if (!api_info) {
    strncpy(errbuf, "[tsremap_init] - Invalid TSRemapInterface argument", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size, "[TSRemapInit] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
             (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  setup_memory_allocation();
  TSDebug(PLUGIN_NAME, "Plugin is successfully initialized");
  return TS_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// We don't have any specific "instances" here, at least not yet.
//
TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /* errbuf ATS_UNUSED */, int /* errbuf_sizeATS_UNUSED */)
{
  RemapInstance *ri = new RemapInstance();

  std::ifstream f;
  int lineno = 0;
  int count  = 0;

  *ih = (void *)ri;
  if (ri == nullptr) {
    TSError("[%s] Unable to create remap instance", PLUGIN_NAME);
    return TS_ERROR;
  }

  if (argc < 3) {
    TSError("[%s] missing configuration file", PLUGIN_NAME);
    return TS_ERROR;
  }

  // Really simple (e.g. basic) config parser
  for (int i = 3; i < argc; ++i) {
    if (strncmp(argv[i], "profile", 7) == 0) {
      ri->profile = true;
    } else if (strncmp(argv[i], "no-profile", 10) == 0) {
      ri->profile = false;
    } else if (strncmp(argv[i], "method", 6) == 0) {
      ri->method = true;
    } else if (strncmp(argv[i], "no-method", 9) == 0) {
      ri->method = false;
    } else if (strncmp(argv[i], "query-string", 12) == 0) {
      ri->query_string = true;
    } else if (strncmp(argv[i], "no-query-string", 15) == 0) {
      ri->query_string = false;
    } else if (strncmp(argv[i], "matrix-parameters", 17) == 0) {
      ri->matrix_params = true;
    } else if (strncmp(argv[i], "no-matrix-parameters", 20) == 0) {
      ri->matrix_params = false;
    } else if (strncmp(argv[i], "host", 4) == 0) {
      ri->host = true;
    } else if (strncmp(argv[i], "no-host", 7) == 0) {
      ri->host = false;
    } else {
      TSError("[%s] invalid option '%s'", PLUGIN_NAME, argv[i]);
    }
  }

  if (*argv[2] == '/') {
    // Absolute path, just use it.
    ri->filename = argv[2];
  } else {
    // Relative path. Make it relative to the configuration directory.
    ri->filename = TSConfigDirGet();
    ri->filename += "/";
    ri->filename += argv[2];
  }

  if (0 != access(ri->filename.c_str(), R_OK)) {
    TSError("[%s] failed to access %s: %s", PLUGIN_NAME, ri->filename.c_str(), strerror(errno));
    return TS_ERROR;
  }

  f.open((ri->filename).c_str(), std::ios::in);
  if (!f.is_open()) {
    TSError("[%s] unable to open %s", PLUGIN_NAME, (ri->filename).c_str());
    return TS_ERROR;
  }
  TSDebug(PLUGIN_NAME, "Loading regular expressions from %s", (ri->filename).c_str());

  while (!f.eof()) {
    std::string line, regex, subst, options;
    std::string::size_type pos1, pos2;

    getline(f, line);
    ++lineno;
    if (line.empty()) {
      continue;
    }

    pos1 = line.find_first_not_of(" \t\n");
    if (pos1 != std::string::npos) {
      if (line[pos1] == '#') {
        continue; // Skip comment lines
      }

      pos2 = line.find_first_of(" \t\n", pos1);
      if (pos2 != std::string::npos) {
        regex = line.substr(pos1, pos2 - pos1);
        pos1  = line.find_first_not_of(" \t\n#", pos2);
        if (pos1 != std::string::npos) {
          pos2 = line.find_first_of(" \t\n", pos1);
          if (pos2 == std::string::npos) {
            pos2 = line.length();
          }
          subst = line.substr(pos1, pos2 - pos1);
          pos1  = line.find_first_not_of(" \t\n#", pos2);
          if (pos1 != std::string::npos) {
            pos2 = line.find_first_of("\n#", pos1);
            if (pos2 == std::string::npos) {
              pos2 = line.length();
            }
            options = line.substr(pos1, pos2 - pos1);
          }
        }
      }
    }

    if (regex.empty()) {
      // No regex found on this line
      TSError("[%s] no regexp found in %s: line %d", PLUGIN_NAME, (ri->filename).c_str(), lineno);
      continue;
    }
    if (subst.empty() && options.empty()) {
      // No substitution found on this line (and no options)
      TSError("[%s] no substitution string found in %s: line %d", PLUGIN_NAME, (ri->filename).c_str(), lineno);
      continue;
    }

    // Got a regex and substitution string
    std::unique_ptr<RemapRegex> cur(new RemapRegex);

    if (!cur->initialize(regex, subst, options)) {
      TSError("[%s] can't create a new regex remap rule", PLUGIN_NAME);
      continue;
    }

    const char *error;
    int erroffset;
    if (cur->compile(error, erroffset) < 0) {
      std::ostringstream oss;
      oss << '[' << PLUGIN_NAME << "] PCRE failed in " << (ri->filename).c_str() << " (line " << lineno << ')';
      if (erroffset > 0) {
        oss << " at offset " << erroffset;
      }
      oss << ": " << error;
      if (cur->regex_empty()) {
        oss << "  (no regular expression)";
      } else {
        oss << "  regex: \"" << cur->regex() << '"';
      }
      TSError("%s", oss.str().c_str());
    } else {
      TSDebug(PLUGIN_NAME, "Added regex=%s with subs=%s and options `%s'", regex.c_str(), subst.c_str(), options.c_str());
      cur->set_order(++count);
      auto tmp = cur.get();
      if (ri->first == nullptr) {
        ri->first = cur.release();
      } else {
        ri->last->set_next(cur.release());
      }
      ri->last = tmp;
    }
  }

  // Make sure we got something...
  if (ri->first == nullptr) {
    TSError("[%s] no regular expressions from the maps", PLUGIN_NAME);
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  RemapInstance *ri = static_cast<RemapInstance *>(ih);
  RemapRegex *re;
  RemapRegex *tmp;

  if (ri->profile) {
    char now[64];
    const ink_time_t tim = time(nullptr);

    if (ink_ctime_r(&tim, now)) {
      now[strlen(now) - 1] = '\0';
    } else {
      memcpy(now, "unknown time", 12);
      *(now + 12) = '\0';
    }

    fprintf(stderr, "[%s]: Profiling information for regex_remap file `%s':\n", now, (ri->filename).c_str());
    fprintf(stderr, "[%s]:    Total hits (matches): %d\n", now, ri->hits);
    fprintf(stderr, "[%s]:    Total missed (no regex matches): %d\n", now, ri->misses);

    if (ri->hits > 0) { // Avoid divide by zeros...
      int ix = 1;

      re = ri->first;
      while (re) {
        re->print(ix, ri->hits, now);
        re = re->next();
        ++ix;
      }
    }
  }

  re = ri->first;
  while (re) {
    RemapRegex::Override *override = re->get_overrides();

    while (override) {
      RemapRegex::Override *tmp = override;

      if (TS_RECORDDATATYPE_STRING == override->type) {
        TSfree(override->data.rec_string);
      }
      override = override->next;
      delete tmp;
    }
    tmp = re;
    re  = re->next();
    delete tmp;
  }

  delete ri;
}

///////////////////////////////////////////////////////////////////////////////
// This is the main "entry" point for the plugin, called for every request.
//
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo *rri)
{
  if (nullptr == ih) {
    TSDebug(PLUGIN_NAME, "Falling back to default URL on regex remap without rules");
    return TSREMAP_NO_REMAP;
  }

  // Populate the request url
  UrlComponents req_url;
  req_url.populate(rri);

  RemapInstance *ri = (RemapInstance *)ih;
  int ovector[OVECCOUNT];
  int lengths[OVECCOUNT / 2 + 1];
  int dest_len;
  TSRemapStatus retval = TSREMAP_DID_REMAP;
  RemapRegex *re       = ri->first;
  int match_len        = 0;
  char *match_buf;

  match_buf = (char *)alloca(req_url.url_len + 32);

  if (ri->method) { // Prepend the URI path or URL with the HTTP method
    TSMBuffer mBuf;
    TSMLoc reqHttpHdrLoc;
    const char *method;

    // Note that Method can not be longer than 16 bytes, or we'll simply truncate it
    if (TS_SUCCESS == TSHttpTxnClientReqGet(static_cast<TSHttpTxn>(txnp), &mBuf, &reqHttpHdrLoc)) {
      method = TSHttpHdrMethodGet(mBuf, reqHttpHdrLoc, &match_len);
      if (method && (match_len > 0)) {
        if (match_len > 16) {
          match_len = 16;
        }
        memcpy(match_buf, method, match_len);
      }
    }
  }

  if (ri->host && req_url.host && req_url.host_len > 0) {
    memcpy(match_buf + match_len, "//", 2);
    memcpy(match_buf + match_len + 2, req_url.host, req_url.host_len);
    match_len += (req_url.host_len + 2);
  }

  *(match_buf + match_len) = '/';
  match_len++;
  if (req_url.path && req_url.path_len > 0) {
    memcpy(match_buf + match_len, req_url.path, req_url.path_len);
    match_len += (req_url.path_len);
  }

  if (ri->matrix_params && req_url.matrix && req_url.matrix_len > 0) {
    *(match_buf + match_len) = ';';
    memcpy(match_buf + match_len + 1, req_url.matrix, req_url.matrix_len);
    match_len += (req_url.matrix_len + 1);
  }

  if (ri->query_string && req_url.query && req_url.query_len > 0) {
    *(match_buf + match_len) = '?';
    memcpy(match_buf + match_len + 1, req_url.query, req_url.query_len);
    match_len += (req_url.query_len + 1);
  }
  match_buf[match_len] = '\0'; // NULL terminate the match string
  TSDebug(PLUGIN_NAME, "Target match string is `%s'", match_buf);

  // Apply the regular expressions, in order. First one wins.
  while (re) {
    // Since we check substitutions on parse time, we don't need to reset ovector
    if (re->match(match_buf, match_len, ovector) != -1) {
      int new_len = re->get_lengths(ovector, lengths, rri, &req_url);

      // Set timeouts
      if (re->active_timeout_option() > (-1)) {
        TSDebug(PLUGIN_NAME, "Setting active timeout to %d", re->active_timeout_option());
        TSHttpTxnActiveTimeoutSet(txnp, re->active_timeout_option());
      }
      if (re->no_activity_timeout_option() > (-1)) {
        TSDebug(PLUGIN_NAME, "Setting no activity timeout to %d", re->no_activity_timeout_option());
        TSHttpTxnNoActivityTimeoutSet(txnp, re->no_activity_timeout_option());
      }
      if (re->connect_timeout_option() > (-1)) {
        TSDebug(PLUGIN_NAME, "Setting connect timeout to %d", re->connect_timeout_option());
        TSHttpTxnConnectTimeoutSet(txnp, re->connect_timeout_option());
      }
      if (re->dns_timeout_option() > (-1)) {
        TSDebug(PLUGIN_NAME, "Setting DNS timeout to %d", re->dns_timeout_option());
        TSHttpTxnDNSTimeoutSet(txnp, re->dns_timeout_option());
      }
      bool lowercase_substitutions = false;
      if (re->lowercase_substitutions_option() == true) {
        TSDebug(PLUGIN_NAME, "Setting lowercasing substitutions on");
        lowercase_substitutions = true;
      }

      RemapRegex::Override *override = re->get_overrides();

      while (override) {
        switch (override->type) {
        case TS_RECORDDATATYPE_INT:
          TSHttpTxnConfigIntSet(txnp, override->key, override->data.rec_int);
          TSDebug(PLUGIN_NAME, "Setting config id %d to `%" PRId64 "'", override->key, override->data.rec_int);
          break;
        case TS_RECORDDATATYPE_FLOAT:
          TSHttpTxnConfigFloatSet(txnp, override->key, override->data.rec_float);
          TSDebug(PLUGIN_NAME, "Setting config id %d to `%f'", override->key, override->data.rec_float);
          break;
        case TS_RECORDDATATYPE_STRING:
          TSHttpTxnConfigStringSet(txnp, override->key, override->data.rec_string, override->data_len);
          TSDebug(PLUGIN_NAME, "Setting config id %d to `%s'", override->key, override->data.rec_string);
          break;
        default:
          break; // Error ?
        }
        override = override->next;
      }

      // Update profiling if requested
      if (ri->profile) {
        re->increment();
        ink_atomic_increment(&(ri->hits), 1);
      }

      if (new_len > 0) {
        char *dest;

        dest     = (char *)alloca(new_len + 8);
        dest_len = re->substitute(dest, match_buf, ovector, lengths, txnp, rri, &req_url, lowercase_substitutions);

        TSDebug(PLUGIN_NAME, "New URL is estimated to be %d bytes long, or less", new_len);
        TSDebug(PLUGIN_NAME, "New URL is %s (length %d)", dest, dest_len);
        TSDebug(PLUGIN_NAME, "    matched rule %d [%s]", re->order(), re->regex());

        // Check for a quick response, if the status option is set
        if (re->status_option() > 0) {
          if (re->status_option() != TS_HTTP_STATUS_MOVED_PERMANENTLY && re->status_option() != TS_HTTP_STATUS_MOVED_TEMPORARILY &&
              re->status_option() != TS_HTTP_STATUS_TEMPORARY_REDIRECT &&
              re->status_option() != TS_HTTP_STATUS_PERMANENT_REDIRECT) {
            // Don't set the URL / Location for this.
            TSHttpTxnStatusSet(txnp, re->status_option());
            break;
          }

          TSDebug(PLUGIN_NAME, "Redirecting URL, status=%d", re->status_option());
          TSHttpTxnStatusSet(txnp, re->status_option());
          rri->redirect = 1;
        }

        // Now parse the new URL, which can also be the redirect URL
        if (dest_len > 0) {
          const char *start = dest;

          // Setup the new URL
          if (TS_PARSE_ERROR == TSUrlParse(rri->requestBufp, rri->requestUrl, &start, start + dest_len)) {
            TSHttpTxnStatusSet(txnp, TS_HTTP_STATUS_INTERNAL_SERVER_ERROR);
            TSError("[%s] can't parse substituted URL string", PLUGIN_NAME);
          }
        }
        break;
      }
    }

    // Try the next regex
    re = re->next();
    if (re == nullptr) {
      retval = TSREMAP_NO_REMAP; // No match
      if (ri->profile) {
        ink_atomic_increment(&(ri->misses), 1);
      }
    }
  }

  return retval;
}
