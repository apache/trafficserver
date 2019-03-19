/*
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
////////////////////////////////////////////////////////////////////////////////
// cookie_remap: ATS plugin to do (simple) cookie based remap rules
// To use this plugin, configure a remap.config rule like
//   map http://foo.com http://bar.com @plugin=.../libexec/cookie_remap.so
//   @pparam=maps.reg

#include "cookiejar.h"
#include <ts/ts.h>

#include <pcre.h>
#include <ts/remap.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <list>
#include <map>
#include <set>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>
#include "hash.h"

using namespace std;

// for bucketizing

#define MY_NAME "cookie_remap"
const int OVECCOUNT = 30; // We support $1 - $9 only, and this needs to be 3x that

#if TS_VERSION_MAJOR > 7
#define SETHTTPSTATUS(TXN, STATUS) TSHttpTxnStatusSet((TXN), STATUS)
#else
#define SETHTTPSTATUS(TXN, STATUS) TSHttpTxnSetHttpRetStatus((TXN), STATUS)
#endif

///////////////////////////////////////////////////////////////////////////////
// Helpers for memory management (to make sure pcre uses the TS APIs).
//
inline void *
ink_malloc(size_t s)
{
  return TSmalloc(s);
}

inline void
ink_free(void *s)
{
  return TSfree(s);
}
///////////////////////////////////////////////////////////////////////////////
// Class holding one request URL's component, to simplify the code and
// length calculations (we need all of them).
//
struct UrlComponents {
  UrlComponents() {}

  ~UrlComponents()
  {
    if (url != nullptr)
      TSfree((void *)url);
  }

  void
  populate(TSRemapRequestInfo *rri)
  {
    scheme    = TSUrlSchemeGet(rri->requestBufp, rri->requestUrl, &scheme_len);
    host      = TSUrlHostGet(rri->requestBufp, rri->requestUrl, &host_len);
    tspath    = TSUrlPathGet(rri->requestBufp, rri->requestUrl, &tspath_len);
    query     = TSUrlHttpQueryGet(rri->requestBufp, rri->requestUrl, &query_len);
    matrix    = TSUrlHttpParamsGet(rri->requestBufp, rri->requestUrl, &matrix_len);
    port      = TSUrlPortGet(rri->requestBufp, rri->requestUrl);
    url       = TSUrlStringGet(rri->requestBufp, rri->requestUrl, &url_len);
    from_path = TSUrlPathGet(rri->requestBufp, rri->mapFromUrl, &from_path_len);

    // based on RFC2396, matrix params are part of path segments so
    // we will just
    // append them to the path
    path.assign(tspath, tspath_len);
    if (matrix_len) {
      path += ';';
      path.append(matrix, matrix_len);
    }
  }

  const char *scheme = nullptr;
  const char *host   = nullptr;
  const char *tspath = nullptr;
  std::string path{};
  const char *query     = nullptr;
  const char *matrix    = nullptr;
  const char *url       = nullptr;
  const char *from_path = nullptr;
  int port              = 0;

  int scheme_len    = 0;
  int host_len      = 0;
  int tspath_len    = 0;
  int query_len     = 0;
  int matrix_len    = 0;
  int url_len       = 0;
  int from_path_len = 0;
};

void
setup_memory_allocation()
{
  pcre_malloc = &ink_malloc;
  pcre_free   = &ink_free;
}

enum operation_type { UNKNOWN = -1, EXISTS = 1, NOTEXISTS, REGEXP, STRING, BUCKET };

enum target_type {
  COOKIE = 1,
  URI, // URI = PATH + QUERY
  UNKNOWN_TARGET
};

/***************************************************************************************
                                                                Decimal to Hex
converter

This is a template function which returns a char* array filled with hex digits
when
passed to it a number(can work as a decimal to hex conversion)and will work for
signed
and unsigned: char, short, and integer(long) type parameters passed to it.

Shortcomings:It won't work for decimal numbers because of presence of
bitshifting in its algorithm.

Arguments:
  * _num is the number to convert to hex
  * hdigits two-byte character array, will be populated with the hex number

***************************************************************************************/

template <class type> // template usage to allow multiple types of parameters
void
dec_to_hex(type _num, char *hdigits)
{
  const char *hlookup = "0123456789ABCDEF"; // lookup table stores the hex digits into their
  // corresponding index.

  if (_num < 0)
    _num *= -1; // and make _num positive to clear(zero) the sign bit

  char mask = 0x000f; // mask will clear(zero) out all the bits except lowest 4
  // which represent a single hex digit

  hdigits[1] = hlookup[mask & _num];
  hdigits[0] = hlookup[mask & (_num >> 4)];

  return;
}

void
urlencode(std::string &str)
{
  size_t pos = 0;
  std::string replacement;
  for (; pos < str.length(); pos++) {
    if (!isalnum(str[pos])) {
      char dec[2];
      dec_to_hex(str[pos], dec);
      std::string replacement = "%" + std::string(dec, 2);
      str.replace(pos, 1, replacement);
    }
  }
}

//----------------------------------------------------------------------------
class subop
{
public:
  subop()
    : cookie(""),
      operation(""),

      str_match(""),

      bucket("")

  {
    TSDebug(MY_NAME, "subop constructor called");
  }

  ~subop()
  {
    TSDebug(MY_NAME, "subop destructor called");
    if (regex)
      pcre_free(regex);

    if (regex_extra)
      pcre_free(regex_extra);
  }

  bool
  empty() const
  {
    return (cookie == "" && operation == "" && op_type == UNKNOWN);
  }

  void
  setCookieName(const std::string &s)
  {
    cookie = s;
  }

  const std::string &
  getCookieName() const
  {
    return cookie;
  }

  const std::string &
  getOperation() const
  {
    return operation;
  }

  operation_type
  getOpType() const
  {
    return op_type;
  }

  target_type
  getTargetType() const
  {
    return target;
  }

  void
  setOperation(const std::string &s)
  {
    operation = s;

    if (operation == "string") {
      op_type = STRING;
    }
    if (operation == "regex") {
      op_type = REGEXP;
    }
    if (operation == "exists") {
      op_type = EXISTS;
    }
    if (operation == "not exists") {
      op_type = NOTEXISTS;
    }
    if (operation == "bucket") {
      op_type = BUCKET;
    }
  }

  void
  setTarget(const std::string &s)
  {
    if (s == "uri")
      target = URI;
    else
      target = COOKIE;
  }

  void
  setStringMatch(const std::string &s)
  {
    op_type   = STRING;
    str_match = s;
  }

  const std::string &
  getStringMatch() const
  {
    return str_match;
  }

  void
  setBucket(const std::string &s)
  {
    int start_pos = s.find('/');

    op_type  = BUCKET;
    bucket   = s;
    how_many = atoi(bucket.substr(0, start_pos).c_str());
    out_of   = atoi(bucket.substr(start_pos + 1).c_str());
  }

  int
  bucketGetTaking() const
  {
    return how_many;
  }

  int
  bucketOutOf() const
  {
    return out_of;
  }

  bool
  setRegexMatch(const std::string &s)
  {
    const char *error_comp  = nullptr;
    const char *error_study = nullptr;
    int erroffset;

    op_type      = REGEXP;
    regex_string = s;
    regex        = pcre_compile(regex_string.c_str(), 0, &error_comp, &erroffset, nullptr);

    if (regex == nullptr) {
      return false;
    }
    regex_extra = pcre_study(regex, 0, &error_study);
    if ((regex_extra == nullptr) && (error_study != nullptr)) {
      return false;
    }

    if (pcre_fullinfo(regex, regex_extra, PCRE_INFO_CAPTURECOUNT, &regex_ccount) != 0)
      return false;

    return true;
  }

  const std::string &
  getRegexString() const
  {
    return regex_string;
  }

  int
  getRegexCcount() const
  {
    return regex_ccount;
  }

  int
  regexMatch(const char *str, int len, int ovector[]) const
  {
    return pcre_exec(regex,       // the compiled pattern
                     regex_extra, // Extra data from study (maybe)
                     str,         // the subject std::string
                     len,         // the length of the subject
                     0,           // start at offset 0 in the subject
                     0,           // default options
                     ovector,     // output vector for substring information
                     OVECCOUNT);  // number of elements in the output vector
  };

  void
  printSubOp() const
  {
    TSDebug(MY_NAME, "\t+++subop+++");
    TSDebug(MY_NAME, "\t\tcookie: %s", cookie.c_str());
    TSDebug(MY_NAME, "\t\toperation: %s", operation.c_str());
    if (str_match.size() > 0) {
      TSDebug(MY_NAME, "\t\tmatching: %s", str_match.c_str());
    }
    if (regex) {
      TSDebug(MY_NAME, "\t\tregex: %s", regex_string.c_str());
    }
    if (bucket.size() > 0) {
      TSDebug(MY_NAME, "\t\tbucket: %s", bucket.c_str());
      TSDebug(MY_NAME, "\t\ttaking: %d", how_many);
      TSDebug(MY_NAME, "\t\tout of: %d", out_of);
    }
  }

private:
  std::string cookie;
  std::string operation;
  enum operation_type op_type = UNKNOWN;
  enum target_type target     = UNKNOWN_TARGET;

  std::string str_match;

  pcre *regex             = nullptr;
  pcre_extra *regex_extra = nullptr;
  std::string regex_string;
  int regex_ccount = 0;

  std::string bucket;
  unsigned int how_many = 0;
  unsigned int out_of   = 0;
};

typedef std::vector<const subop *> SubOpQueue;

//----------------------------------------------------------------------------
class op
{
public:
  op() { TSDebug(MY_NAME, "op constructor called"); }

  ~op()
  {
    TSDebug(MY_NAME, "op destructor called");
    for (auto &subop : subops) {
      delete subop;
    }
  }

  void
  addSubOp(const subop *s)
  {
    subops.push_back(s);
  }

  void
  setSendTo(const std::string &s)
  {
    sendto = s;
  }

  const std::string &
  getSendTo() const
  {
    return sendto;
  }

  void
  setElseSendTo(const std::string &s)
  {
    else_sendto = s;
  }

  void
  setStatus(const std::string &s)
  {
    if (else_sendto.size() > 0)
      else_status = static_cast<TSHttpStatus>(atoi(s.c_str()));
    else
      status = static_cast<TSHttpStatus>(atoi(s.c_str()));
  }

  void
  setElseStatus(const std::string &s)
  {
    else_status = static_cast<TSHttpStatus>(atoi(s.c_str()));
  }

  void
  printOp() const
  {
    TSDebug(MY_NAME, "++++operation++++");
    TSDebug(MY_NAME, "sending to: %s", sendto.c_str());
    TSDebug(MY_NAME, "if these operations match: ");

    for (auto subop : subops) {
      subop->printSubOp();
    }
    if (else_sendto.size() > 0)
      TSDebug(MY_NAME, "else: %s", else_sendto.c_str());
  }

  bool
  process(CookieJar &jar, std::string &dest, TSHttpStatus &retstat, TSRemapRequestInfo *rri) const
  {
    if (sendto == "") {
      return false; // guessing every operation must have a
                    // sendto url???
    }

    int retval        = 1;
    bool cookie_found = false;
    std::string c;
    std::string cookie_data;
    std::string object_name; // name of the thing being processed,
                             // cookie, or
                             // request url

    TSDebug(MY_NAME, "starting to process a new operation");

    for (auto subop : subops) {
      // subop* s = *it;
      int subop_type     = subop->getOpType();
      target_type target = subop->getTargetType();

      c = subop->getCookieName();
      if (c.length()) {
        TSDebug(MY_NAME, "processing cookie: %s", c.c_str());

        size_t period_pos = c.find_first_of('.');

        if (period_pos == std::string::npos) { // not a sublevel
                                               // cookie name
          TSDebug(MY_NAME, "processing non-sublevel cookie");

          cookie_found = jar.get_full(c, cookie_data);
          TSDebug(MY_NAME, "full cookie: %s", cookie_data.c_str());
          object_name = c;
        } else { // is in the format FOO.BAR
          std::string cookie_main   = c.substr(0, period_pos);
          std::string cookie_subkey = c.substr(period_pos + 1);

          TSDebug(MY_NAME, "processing sublevel cookie");
          TSDebug(MY_NAME, "c key: %s", cookie_main.c_str());
          TSDebug(MY_NAME, "c subkey: %s", cookie_subkey.c_str());

          cookie_found = jar.get_part(cookie_main, cookie_subkey, cookie_data);
          object_name  = cookie_main + " . " + cookie_subkey;
        }
        // invariant:  cookie name is in object_name and
        // cookie data (if any) is
        // in cookie_data

        if (cookie_found == false) { // cookie name or sub-key not found
                                     // inside cookies
          if (subop_type == NOTEXISTS) {
            TSDebug(MY_NAME,
                    "cookie %s was not "
                    "found (and we wanted "
                    "that)",
                    object_name.c_str());
            continue; // we can short
                      // circuit more
                      // testing
          }
          TSDebug(MY_NAME, "cookie %s was not found", object_name.c_str());
          retval &= 0;
          break;
        } else {
          // cookie exists
          if (subop_type == NOTEXISTS) { // we found the cookie
                                         // but are asking
            // for non existence
            TSDebug(MY_NAME,
                    "cookie %s was found, "
                    "but operation "
                    "requires "
                    "non-existence",
                    object_name.c_str());
            retval &= 0;
            break;
          }

          if (subop_type == EXISTS) {
            TSDebug(MY_NAME, "cookie %s was found", object_name.c_str()); // got what
                                                                          // we were
                                                                          // looking
                                                                          // for
            continue;                                                     // we can short
                                                                          // circuit more
                                                                          // testing
          }
        } // handled EXISTS / NOTEXISTS subops

        TSDebug(MY_NAME, "processing cookie data: \"%s\"", cookie_data.c_str());
      } else
        target = URI;

      // INVARIANT: we now have the data from the cookie (if
      // any) inside
      // cookie_data and we are here because we need
      // to continue processing this suboperation in some way

      if (!rri) { // too dangerous to continue without the
                  // rri; hopefully that
        // never happens
        TSDebug(MY_NAME, "request info structure is "
                         "empty; can't continue "
                         "processing this subop");
        retval &= 0;
        break;
      }

      // If the user has specified a cookie in his
      // suboperation, use the cookie
      // data for matching;
      //  otherwise, use the request uri (path + query)
      std::string request_uri; // only set the value if we
                               // need it; we might
      // match the cookie data instead
      const std::string &string_to_match(target == URI ? request_uri : cookie_data);
      if (target == URI) {
        UrlComponents req_url;
        req_url.populate(rri);

        TSDebug(MY_NAME, "process req_url.path = %s", req_url.path.c_str());
        request_uri = req_url.path;
        if (request_uri.length() && request_uri[0] != '/')
          request_uri.insert(0, 1, '/');
        if (req_url.query_len > 0) {
          request_uri += '?';
          request_uri += std::string(req_url.query, req_url.query_len);
        }
        object_name = "request uri";
      }

      // invariant:  we've decided at this point what string
      // we'll match, if we
      // do matching

      // OPERATION::string matching
      if (subop_type == STRING) {
        if (string_to_match == subop->getStringMatch()) {
          TSDebug(MY_NAME, "string match succeeded");
          continue;
        } else {
          TSDebug(MY_NAME, "string match failed");
          retval &= 0;
          break;
        }
      }

      // OPERATION::regex matching
      if (subop_type == REGEXP) {
        int ovector[OVECCOUNT];
        int ret = subop->regexMatch(string_to_match.c_str(), string_to_match.length(), ovector);

        if (ret >= 0) {
          std::string::size_type pos  = sendto.find('$');
          std::string::size_type ppos = 0;

          dest.erase();                    // we only reset dest if
                                           // there is a successful
                                           // regex
                                           // match
          dest.reserve(sendto.size() * 2); // Wild guess at this
                                           // time ... is
          // sucks we can't precalculate this
          // like regex_remap.

          TSDebug(MY_NAME, "found %d matches", ret);
          TSDebug(MY_NAME,
                  "successful regex "
                  "match of: %s with %s "
                  "rewriting string: %s",
                  string_to_match.c_str(), subop->getRegexString().c_str(), sendto.c_str());

          // replace the $(1-9) in the sendto url
          // as necessary
          const size_t LAST_IDX_TO_SEARCH(sendto.length() - 2); // otherwise the below loop can
                                                                // access "sendto" out of range
          while (pos <= LAST_IDX_TO_SEARCH) {
            if (isdigit(sendto[pos + 1])) {
              int ix = sendto[pos + 1] - '0';

              if (ix <= subop->getRegexCcount()) { // Just skip an illegal regex group
                dest += sendto.substr(ppos, pos - ppos);
                dest += string_to_match.substr(ovector[ix * 2], ovector[ix * 2 + 1] - ovector[ix * 2]);
                ppos = pos + 2;
              } else {
                TSDebug(MY_NAME,
                        "bad "
                        "rewriting "
                        "string, "
                        "for group "
                        "%d: %s",
                        ix, sendto.c_str());
              }
            }
            pos = sendto.find('$', pos + 1);
          }
          dest += sendto.substr(ppos);
          continue; // next subop, please
        } else {
          TSDebug(MY_NAME,
                  "could not match "
                  "regular expression "
                  "%s to %s",
                  subop->getRegexString().c_str(), string_to_match.c_str());
          retval &= 0;
          break;
        }
      }

      // OPERATION::bucket ranges
      if (subop_type == BUCKET) {
        unsigned int taking = subop->bucketGetTaking();
        unsigned int out_of = subop->bucketOutOf();

        uint32_t hash;

        if (taking == 0 || out_of == 0) {
          TSDebug(MY_NAME,
                  "taking %d out of %d "
                  "makes no sense?!",
                  taking, out_of);
          retval &= 0;
          break;
        }

        hash = hash_fnv32_buckets(cookie_data.c_str(), cookie_data.size(), out_of);
        TSDebug(MY_NAME,
                "we hashed this to bucket: %u "
                "taking: %u out of: %u",
                hash, taking, out_of);

        if (hash < taking) {
          TSDebug(MY_NAME, "we hashed in the range, yay!");
          continue; // we hashed in the range
        } else {
          TSDebug(MY_NAME, "we didnt hash in the "
                           "range requested, so "
                           "sad");
          retval &= 0;
          break;
        }
      }
    }

    if (retval == 1) {
      if (dest.size() == 0) // Unless already set by one of
                            // the operators (e.g. regex)
        dest = sendto;
      if (status > 0) {
        retstat = status;
      }
      return true;
    } else if (else_sendto.size() > 0 && retval == 0) {
      dest = else_sendto;
      if (else_status > 0) {
        retstat = else_status;
      }
      return true;
    } else {
      dest = "";
      return false;
    }
  }

private:
  SubOpQueue subops{};
  std::string sendto{""};
  std::string else_sendto{""};
  TSHttpStatus status      = TS_HTTP_STATUS_NONE;
  TSHttpStatus else_status = TS_HTTP_STATUS_NONE;
};

typedef std::pair<std::string, std::string> StringPair;
typedef std::vector<StringPair> OpMap;

//----------------------------------------------------------------------------
static bool
build_op(op &o, OpMap const &q)
{
  StringPair m;

  subop *sub = new subop();

  // loop through the array of key->value pairs
  for (auto const &pr : q) {
    std::string const &key = pr.first;
    std::string const &val = pr.second;

    if (key == "cookie") {
      if (!sub->empty()) {
        TSDebug(MY_NAME, "ERROR: you need to define a connector");
        goto error;
      }
      sub->setCookieName(val);
    }

    if (key == "sendto" || key == "url") {
      o.setSendTo(val);
    }

    if (key == "else") {
      o.setElseSendTo(val);
    }

    if (key == "status") {
      o.setStatus(val);
    }

    if (key == "operation") {
      sub->setOperation(val);
    }

    if (key == "target") {
      sub->setTarget(val);
    }

    if (key == "match") {
      sub->setStringMatch(val);
    }

    if (key == "regex") {
      bool ret = sub->setRegexMatch(val);

      if (!ret) {
        goto error;
      }
    }

    if (key == "bucket" || key == "hash") {
      sub->setBucket(val);
    }

    if (key == "connector") {
      o.addSubOp(sub);
      sub = new subop();
    }
  }

  o.addSubOp(sub);
  return true;

error:
  TSDebug(MY_NAME, "error building operation");
  return false;
}

typedef std::vector<const op *> OpsQueue;

//----------------------------------------------------------------------------
// init
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  setup_memory_allocation();

  return TS_SUCCESS;
}

//----------------------------------------------------------------------------
// initialization of structures from config parameters
TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf, int errbuf_size)
{
  if (argc != 3) {
    TSError("arguments not equal to 3: %d", argc);
    TSDebug(MY_NAME, "arguments not equal to 3: %d", argc);
    return TS_ERROR;
  }

  std::string filename(argv[2]);
  try {
    YAML::Node config = YAML::LoadFile(filename);

    std::unique_ptr<OpsQueue> ops(new OpsQueue);
    OpMap op_data;

    for (YAML::const_iterator it = config.begin(); it != config.end(); ++it) {
      const string &name         = it->first.as<std::string>();
      YAML::NodeType::value type = it->second.Type();

      if (name != "op" || type != YAML::NodeType::Map) {
        const string reason = "Top level nodes must be named op and be of type map";
        TSError("Invalid YAML Configuration format for cookie_remap: %s, reason: %s", filename.c_str(), reason.c_str());
        return TS_ERROR;
      }

      for (YAML::const_iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
        const YAML::Node &first  = it2->first;
        const YAML::Node &second = it2->second;

        if (second.Type() != YAML::NodeType::Scalar) {
          const string reason = "All op nodes must be of type scalar";
          TSError("Invalid YAML Configuration format for cookie_remap: %s, reason: %s", filename.c_str(), reason.c_str());
          return TS_ERROR;
        }

        const string &key   = first.as<std::string>();
        const string &value = second.as<std::string>();
        op_data.emplace_back(key, value);
      }

      if (op_data.size()) {
        op *o = new op();
        if (!build_op(*o, op_data)) {
          delete o;

          TSError("building operation, check configuration file: %s", filename.c_str());
          return TS_ERROR;
        } else {
          ops->push_back(o);
        }
        o->printOp();
        op_data.clear();
      }
    }

    TSDebug(MY_NAME, "# of ops: %d", (int)ops->size());
    *ih = static_cast<void *>(ops.release());
  } catch (const YAML::Exception &e) {
    TSError("YAML::Exception %s when parsing YAML config file %s for cookie_remap", e.what(), filename.c_str());
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

//----------------------------------------------------------------------------
// called whenever we need to perform substiturions on a string; used to replace
// things like
//  $url, $unmatched_path, $cr_req_url, and $cr_url_encode
// returns 0 if no substitutions, 1 otw.
int
cr_substitutions(std::string &obj, TSRemapRequestInfo *rri)
{
  int retval = 0;

  size_t pos      = obj.find('$');
  size_t tmp_pos  = 0;
  size_t last_pos = pos;

  UrlComponents req_url;
  req_url.populate(rri);
  TSDebug(MY_NAME, "x req_url.path: %s %zu", req_url.path.c_str(), req_url.path.length());
  TSDebug(MY_NAME, "x req_url.url: %s %d", std::string(req_url.url, req_url.url_len).c_str(), req_url.url_len);

  while (pos < obj.length()) {
    if (pos + 1 >= obj.length())
      break; // trailing ?

    switch (obj[pos + 1]) {
    case 'p':
      if (obj.substr(pos + 1, 4) == "path") {
        obj.replace(pos, 5, req_url.path);
        pos += req_url.path.length();
      }
      break;
    case 'u':
      if (obj.substr(pos + 1, 14) == "unmatched_path") {
        // we found $unmatched_path inside the sendto
        // url
        std::string unmatched_path(req_url.path);
        TSDebug(MY_NAME, "unmatched_path: %s", unmatched_path.c_str());
        TSDebug(MY_NAME, "from_path: %s", req_url.from_path);

        tmp_pos = unmatched_path.find(req_url.from_path, 0, req_url.from_path_len); // from patch
        if (tmp_pos != std::string::npos) {
          unmatched_path.erase(tmp_pos, req_url.from_path_len);
        }
        TSDebug(MY_NAME, "unmatched_path: %s", unmatched_path.c_str());
        TSDebug(MY_NAME, "obj: %s", obj.c_str());
        obj.replace(pos, 15, unmatched_path);
        TSDebug(MY_NAME, "obj: %s", obj.c_str());
        pos += unmatched_path.length();
      }
      break;
    case 'c':
      if (obj.substr(pos + 1, 3) == "cr_")
        switch (obj[pos + 4]) {
        case 'r':
          if (obj.substr(pos + 4, 7) == "req_url") {
            // we found $cr_req_url inside
            // the sendto url
            std::string request_url;
            if (req_url.url && req_url.url_len > 0) {
              request_url = std::string(req_url.url, req_url.url_len);
            }
            TSDebug(MY_NAME, "req_url.url: %s %d", std::string(req_url.url, req_url.url_len).c_str(), req_url.url_len);
            obj.replace(pos, 11, request_url);
            pos += request_url.length();
          }
          break;
        case 'u':
          // note that this allows for variables
          // nested in the function, but not
          // for functions nested in the function
          if (obj.substr(pos + 4, 10) == "urlencode(" && (tmp_pos = obj.find(')', pos + 14)) != std::string::npos) {
            std::string str = obj.substr(pos + 14, tmp_pos - pos - 14); // the string to
                                                                        // encode
            cr_substitutions(str, rri);                                 // expand any
                                                                        // variables as
                                                                        // necessary
                                                                        // before
                                                                        // encoding
            urlencode(str);
            obj.replace(pos, tmp_pos - pos + 1, str); // +1 for the
                                                      // closing
                                                      // parens
            pos += str.length();
          }
          break;
        }
      break;
    }
    if (last_pos == pos)
      pos++; // don't search the same place again and again
    last_pos = pos;
    pos      = obj.find('$', pos);
  }

  return retval;
}

//----------------------------------------------------------------------------
// called on each request
// returns 0 on error or failure to match rules, 1 on a match
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo *rri)
{
  OpsQueue *ops       = (OpsQueue *)ih;
  TSHttpStatus status = TS_HTTP_STATUS_NONE;

  UrlComponents req_url;
  req_url.populate(rri);

  if (ops == (OpsQueue *)nullptr) {
    TSError("serious error with encountered while attempting to "
            "cookie_remap");
    TSDebug(MY_NAME, "serious error with encountered while attempting to remap");
    return TSREMAP_NO_REMAP;
  }

  // get any query params..we will append that to the answer (possibly)
  std::string client_req_query_params;
  if (req_url.query_len) {
    client_req_query_params = "?";
    client_req_query_params += std::string(req_url.query, req_url.query_len);
  }
  TSDebug(MY_NAME, "Query Parameters: %s", client_req_query_params.c_str());

  std::string rewrite_to;
  char cookie_str[] = "Cookie";
  TSMLoc field      = TSMimeHdrFieldFind(rri->requestBufp, rri->requestHdrp, cookie_str, sizeof(cookie_str) - 1);

  // cookie header doesn't exist
  if (field == nullptr) {
    TSDebug(MY_NAME, "no cookie header");
    // return TSREMAP_NO_REMAP;
  }

  const char *cookie = nullptr;
  int cookie_len     = 0;
  if (field != nullptr) {
    cookie = TSMimeHdrFieldValueStringGet(rri->requestBufp, rri->requestHdrp, field, -1, &cookie_len);
  }
  std::string temp_cookie(cookie, cookie_len);
  CookieJar jar;
  jar.create(temp_cookie);

  for (auto &op : *ops) {
    TSDebug(MY_NAME, ">>> processing new operation");
    if (op->process(jar, rewrite_to, status, rri)) {
      cr_substitutions(rewrite_to, rri);

      size_t pos = 7;                             // 7 because we want to ignore the // in
                                                  // http:// :)
      size_t tmp_pos = rewrite_to.find('?', pos); // we don't want to alter the query string
      do {
        pos = rewrite_to.find("//", pos);
        if (pos < tmp_pos)
          rewrite_to.erase(pos, 1); // remove one '/'
      } while (pos <= rewrite_to.length() && pos < tmp_pos);

      // Add Query Parameters if not already present
      if (!client_req_query_params.empty() && rewrite_to.find('?') == std::string::npos) {
        rewrite_to.append(client_req_query_params);
      }

      TSDebug(MY_NAME, "rewriting to: %s", rewrite_to.c_str());

      // Maybe set the return status
      if (status > TS_HTTP_STATUS_NONE) {
        TSDebug(MY_NAME, "Setting return status to %d", status);
        SETHTTPSTATUS(txnp, status);
        if ((status == TS_HTTP_STATUS_MOVED_PERMANENTLY) || (status == TS_HTTP_STATUS_MOVED_TEMPORARILY)) {
          if (rewrite_to.size() > 8192) {
            TSError("Redirect in target "
                    "URL too long");
            SETHTTPSTATUS(txnp, TS_HTTP_STATUS_REQUEST_URI_TOO_LONG);
          } else {
            const char *start = rewrite_to.c_str();
            int dest_len      = rewrite_to.size();

            if (TS_PARSE_ERROR == TSUrlParse(rri->requestBufp, rri->requestUrl, &start, start + dest_len)) {
              SETHTTPSTATUS(txnp, TS_HTTP_STATUS_INTERNAL_SERVER_ERROR);
              TSError("can't parse "
                      "substituted "
                      "URL string");
            } else {
              rri->redirect = 1;
            }
          }
        }
        if (field != nullptr) {
          TSHandleMLocRelease(rri->requestBufp, rri->requestHdrp, field);
        }
        if (rri->redirect) {
          return TSREMAP_DID_REMAP;
        } else {
          return TSREMAP_NO_REMAP;
        }
      }

      const char *start = rewrite_to.c_str();

      // set the new url
      if (TSUrlParse(rri->requestBufp, rri->requestUrl, &start, start + rewrite_to.length()) == TS_PARSE_ERROR) {
        SETHTTPSTATUS(txnp, TS_HTTP_STATUS_INTERNAL_SERVER_ERROR);
        TSError("can't parse substituted URL string");
        goto error;
      } else {
        if (field != nullptr) {
          TSHandleMLocRelease(rri->requestBufp, rri->requestHdrp, field);
        }
        return TSREMAP_DID_REMAP;
      }

    // Cleanup
    error:
      if (field != nullptr) {
        TSHandleMLocRelease(rri->requestBufp, rri->requestHdrp, field);
      }
      return TSREMAP_NO_REMAP;
    }
  }

  TSDebug(MY_NAME, "could not execute ANY of the cookie remap operations... "
                   "falling back to default in remap.config");

  if (field != nullptr) {
    TSHandleMLocRelease(rri->requestBufp, rri->requestHdrp, field);
  }
  return TSREMAP_NO_REMAP;
}

//----------------------------------------------------------------------------
// unload
void
TSRemapDeleteInstance(void *ih)
{
  OpsQueue *ops = (OpsQueue *)ih;

  TSDebug(MY_NAME, "deleting loaded operations");
  for (auto &op : *ops) {
    delete op;
  }

  delete ops;

  return;
}
