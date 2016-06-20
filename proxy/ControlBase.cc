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

/*****************************************************************************
 *
 *  ControlBase.cc - Base class to process generic modifiers to
 *                         ControlMatcher Directives
 *
 *
 ****************************************************************************/
#include "ts/ink_platform.h"
#include "ts/ink_defs.h"
#include "ts/ink_time.h"

#include "Main.h"
#include "URL.h"
#include "ts/Tokenizer.h"
#include "ControlBase.h"
#include "ts/MatcherUtils.h"
#include "HTTP.h"
#include "ControlMatcher.h"
#include "HdrUtils.h"
#include "ts/Vec.h"

#include <ts/TsBuffer.h>

/** Used for printing IP address.
    @code
    uint32_t addr; // IP address.
    printf("IP address = " TS_IP_PRINTF_CODE,TS_IP_OCTETS(addr));
    @endcode
    @internal Need to move these to a common header.
 */
#define TS_IP_OCTETS(x)                                                                               \
  reinterpret_cast<unsigned char const *>(&(x))[0], reinterpret_cast<unsigned char const *>(&(x))[1], \
    reinterpret_cast<unsigned char const *>(&(x))[2], reinterpret_cast<unsigned char const *>(&(x))[3]

// ----------
ControlBase::Modifier::~Modifier()
{
}
ControlBase::Modifier::Type
ControlBase::Modifier::type() const
{
  return MOD_INVALID;
}
// --------------------------
namespace
{
// ----------
struct TimeMod : public ControlBase::Modifier {
  time_t start_time;
  time_t end_time;

  static char const *const NAME;

  virtual Type type() const;
  virtual char const *name() const;
  virtual bool check(HttpRequestData *req) const;
  virtual void print(FILE *f) const;
  static TimeMod *make(char *value, char const **error);
  static const char *timeOfDayToSeconds(const char *time_str, time_t *seconds);
};

char const *const TimeMod::NAME = "Time";
ControlBase::Modifier::Type
TimeMod::type() const
{
  return MOD_TIME;
}
char const *
TimeMod::name() const
{
  return NAME;
}

void
TimeMod::print(FILE *f) const
{
  fprintf(f, "%s=%ld-%ld  ",
          // Have to cast because time_t can be 32 or 64 bits and the compiler
          // will barf if format code doesn't match.
          this->name(), static_cast<long>(start_time), static_cast<long>(end_time));
}
bool
TimeMod::check(HttpRequestData *req) const
{
  struct tm cur_time;
  time_t timeOfDay = req->xact_start;
  // Use this to account for daylight savings time.
  ink_localtime_r(&timeOfDay, &cur_time);
  timeOfDay = cur_time.tm_hour * (60 * 60) + cur_time.tm_min * 60 + cur_time.tm_sec;
  return start_time <= timeOfDay && timeOfDay <= end_time;
}

TimeMod *
TimeMod::make(char *value, char const **error)
{
  Tokenizer rangeTok("-");
  TimeMod *mod = 0;
  TimeMod tmp;
  int num_tok;

  num_tok = rangeTok.Initialize(value, SHARE_TOKS);
  if (num_tok == 1) {
    *error = "End time not specified";
  } else if (num_tok > 2) {
    *error = "Malformed time range";
  } else if (0 == (*error = timeOfDayToSeconds(rangeTok[0], &tmp.start_time)) &&
             0 == (*error = timeOfDayToSeconds(rangeTok[1], &tmp.end_time))) {
    mod = new TimeMod(tmp);
  }
  return mod;
}
/**   Converts TimeOfDay (TOD) to second value.
      @a *seconds is set to number of seconds since midnight
      represented by @a time_str.

      @return 0 on success, static error string on failure.
*/
const char *
TimeMod::timeOfDayToSeconds(const char *time_str, time_t *seconds)
{
  int hour   = 0;
  int min    = 0;
  int sec    = 0;
  time_t tmp = 0;

  // coverity[secure_coding]
  if (sscanf(time_str, "%d:%d:%d", &hour, &min, &sec) != 3) {
    // coverity[secure_coding]
    if (sscanf(time_str, "%d:%d", &hour, &min) != 2) {
      return "Malformed time specified";
    }
  }

  if (!(hour >= 0 && hour <= 23))
    return "Illegal hour specification";

  tmp = hour * 60;

  if (!(min >= 0 && min <= 59))
    return "Illegal minute specification";

  tmp = (tmp + min) * 60;

  if (!(sec >= 0 && sec <= 59))
    return "Illegal second specification";

  tmp += sec;

  *seconds = tmp;
  return 0;
}

// ----------
struct PortMod : public ControlBase::Modifier {
  int start_port;
  int end_port;

  static char const *const NAME;

  virtual char const *name() const;
  virtual bool check(HttpRequestData *req) const;
  virtual void print(FILE *f) const;

  static PortMod *make(char *value, char const **error);
};

char const *const PortMod::NAME = "Port";
char const *
PortMod::name() const
{
  return NAME;
}

void
PortMod::print(FILE *f) const
{
  fprintf(f, "%s=%d-%d  ", this->name(), start_port, end_port);
}

bool
PortMod::check(HttpRequestData *req) const
{
  int port = req->hdr->port_get();
  return start_port <= port && port <= end_port;
}

PortMod *
PortMod::make(char *value, char const **error)
{
  Tokenizer rangeTok("-");
  PortMod tmp;
  int num_tok = rangeTok.Initialize(value, SHARE_TOKS);

  *error = 0;
  if (num_tok > 2) {
    *error = "Malformed Range";
    // coverity[secure_coding]
  } else if (sscanf(rangeTok[0], "%d", &tmp.start_port) != 1) {
    *error = "Invalid start port";
  } else if (num_tok == 2) {
    // coverity[secure_coding]
    if (sscanf(rangeTok[1], "%d", &tmp.end_port) != 1)
      *error = "Invalid end port";
    else if (tmp.end_port < tmp.start_port)
      *error = "Malformed Range: end port < start port";
  } else {
    tmp.end_port = tmp.start_port;
  }

  // If there's an error message, return null.
  // Otherwise create a new item and return it.
  return *error ? 0 : new PortMod(tmp);
}

// ----------
struct IPortMod : public ControlBase::Modifier {
  int _port;

  static char const *const NAME;

  IPortMod(int port);

  virtual char const *name() const;
  virtual bool check(HttpRequestData *req) const;
  virtual void print(FILE *f) const;
  static IPortMod *make(char *value, char const **error);
};

char const *const IPortMod::NAME = "IPort";
IPortMod::IPortMod(int port) : _port(port)
{
}
char const *
IPortMod::name() const
{
  return NAME;
}

void
IPortMod::print(FILE *f) const
{
  fprintf(f, "%s=%d  ", this->name(), _port);
}
bool
IPortMod::check(HttpRequestData *req) const
{
  return req->incoming_port == _port;
}

IPortMod *
IPortMod::make(char *value, char const **error)
{
  IPortMod *zret = 0;
  int port;
  // coverity[secure_coding]
  if (sscanf(value, "%u", &port) == 1) {
    zret = new IPortMod(port);
  } else {
    *error = "Invalid incoming port";
  }
  return zret;
}
// ----------
struct SrcIPMod : public ControlBase::Modifier {
  // Stored in host order because that's how they are compared.
  IpEndpoint start_addr; ///< Start address in HOST order.
  IpEndpoint end_addr;   ///< End address in HOST order.

  static char const *const NAME;

  virtual Type type() const;
  virtual char const *name() const;
  virtual bool check(HttpRequestData *req) const;
  virtual void print(FILE *f) const;
  static SrcIPMod *make(char *value, char const **error);
};

char const *const SrcIPMod::NAME = "SrcIP";
ControlBase::Modifier::Type
SrcIPMod::type() const
{
  return MOD_SRC_IP;
}
char const *
SrcIPMod::name() const
{
  return NAME;
}

void
SrcIPMod::print(FILE *f) const
{
  ip_text_buffer b1, b2;
  fprintf(f, "%s=%s-%s  ", this->name(), ats_ip_ntop(&start_addr.sa, b1, sizeof(b1)), ats_ip_ntop(&end_addr.sa, b2, sizeof(b2)));
}
bool
SrcIPMod::check(HttpRequestData *req) const
{
  // Compare in host order
  return ats_ip_addr_cmp(&start_addr, &req->src_ip) <= 0 && ats_ip_addr_cmp(&req->src_ip, &end_addr) <= 0;
}
SrcIPMod *
SrcIPMod::make(char *value, char const **error)
{
  SrcIPMod tmp;
  SrcIPMod *zret = 0;
  *error         = ExtractIpRange(value, &tmp.start_addr.sa, &tmp.end_addr.sa);

  if (!*error)
    zret = new SrcIPMod(tmp);
  return zret;
}
// ----------
struct SchemeMod : public ControlBase::Modifier {
  int _scheme; ///< Tokenized scheme.

  static char const *const NAME;

  SchemeMod(int scheme);

  virtual Type type() const;
  virtual char const *name() const;
  virtual bool check(HttpRequestData *req) const;
  virtual void print(FILE *f) const;

  char const *getWksText() const;

  static SchemeMod *make(char *value, char const **error);
};

char const *const SchemeMod::NAME = "Scheme";

SchemeMod::SchemeMod(int scheme) : _scheme(scheme)
{
}

ControlBase::Modifier::Type
SchemeMod::type() const
{
  return MOD_SCHEME;
}
char const *
SchemeMod::name() const
{
  return NAME;
}
char const *
SchemeMod::getWksText() const
{
  return hdrtoken_index_to_wks(_scheme);
}

bool
SchemeMod::check(HttpRequestData *req) const
{
  return req->hdr->url_get()->scheme_get_wksidx() == _scheme;
}
void
SchemeMod::print(FILE *f) const
{
  fprintf(f, "%s=%s  ", this->name(), hdrtoken_index_to_wks(_scheme));
}
SchemeMod *
SchemeMod::make(char *value, char const **error)
{
  SchemeMod *zret = 0;
  int scheme      = hdrtoken_tokenize(value, strlen(value));
  if (scheme < 0) {
    *error = "Unknown scheme";
  } else {
    zret = new SchemeMod(scheme);
  }
  return zret;
}

// ----------
// This is a base class for all of the mods that have a
// text string.
struct TextMod : public ControlBase::Modifier {
  ts::Buffer text;

  TextMod();
  ~TextMod();

  // Calls name() which the subclass must provide.
  virtual void print(FILE *f) const;

  // Copy the given NUL-terminated string to the text buffer.
  void set(const char *value);
};

TextMod::TextMod() : text()
{
}
TextMod::~TextMod()
{
  free(text.data());
}

void
TextMod::print(FILE *f) const
{
  fprintf(f, "%s=%*s  ", this->name(), static_cast<int>(text.size()), text.data());
}

void
TextMod::set(const char *value)
{
  free(this->text.data());
  this->text.set(ats_strdup(value), strlen(value));
}

struct MultiTextMod : public ControlBase::Modifier {
  Vec<ts::Buffer> text_vec;
  MultiTextMod();
  ~MultiTextMod();

  // Copy the value to the MultiTextMod buffer.
  void set(char *value);

  // Calls name() which the subclass must provide.
  virtual void print(FILE *f) const;
};

MultiTextMod::MultiTextMod()
{
}
MultiTextMod::~MultiTextMod()
{
  text_vec.clear();
}

void
MultiTextMod::print(FILE *f) const
{
  for_Vec(ts::Buffer, text_iter, this->text_vec)
    fprintf(f, "%s=%*s ", this->name(), static_cast<int>(text_iter.size()), text_iter.data());
}

void
MultiTextMod::set(char *value)
{
  Tokenizer rangeTok(",");
  int num_tok = rangeTok.Initialize(value, SHARE_TOKS);
  for (int i = 0; i < num_tok; i++) {
    ts::Buffer text;
    text.set(ats_strdup(rangeTok[i]), strlen(rangeTok[i]));
    this->text_vec.push_back(text);
  }
}

// ----------
struct MethodMod : public TextMod {
  static char const *const NAME;

  virtual Type type() const;
  virtual char const *name() const;
  virtual bool check(HttpRequestData *req) const;

  static MethodMod *make(char *value, char const **error);
};
char const *const MethodMod::NAME = "Method";
ControlBase::Modifier::Type
MethodMod::type() const
{
  return MOD_METHOD;
}
char const *
MethodMod::name() const
{
  return NAME;
}
bool
MethodMod::check(HttpRequestData *req) const
{
  int method_len;
  char const *method = req->hdr->method_get(&method_len);
  return method_len >= static_cast<int>(text.size()) && 0 == strncasecmp(method, text.data(), text.size());
}
MethodMod *
MethodMod::make(char *value, char const **)
{
  MethodMod *mod = new MethodMod();
  mod->set(value);
  return mod;
}

// ----------
struct PrefixMod : public TextMod {
  static char const *const NAME;

  virtual Type type() const;
  virtual char const *name() const;
  virtual bool check(HttpRequestData *req) const;
  static PrefixMod *make(char *value, char const **error);
};

char const *const PrefixMod::NAME = "Prefix";
ControlBase::Modifier::Type
PrefixMod::type() const
{
  return MOD_PREFIX;
}
char const *
PrefixMod::name() const
{
  return NAME;
}
bool
PrefixMod::check(HttpRequestData *req) const
{
  int path_len;
  char const *path = req->hdr->url_get()->path_get(&path_len);
  bool zret        = path_len >= static_cast<int>(text.size()) && 0 == memcmp(path, text.data(), text.size());
  /*
    Debug("cache_control", "Prefix check: URL=%0.*s Mod=%0.*s Z=%s",
      path_len, path, text.size(), text.data(),
      zret ? "Match" : "Fail"
    );
  */
  return zret;
}
PrefixMod *
PrefixMod::make(char *value, char const ** /* error ATS_UNUSED */)
{
  PrefixMod *mod = new PrefixMod();
  // strip leading slashes because get_path which is used later
  // doesn't include them from the URL.
  while ('/' == *value)
    ++value;
  mod->set(value);
  return mod;
}

// ----------
struct SuffixMod : public MultiTextMod {
  static char const *const NAME;

  virtual Type type() const;
  virtual char const *name() const;
  virtual bool check(HttpRequestData *req) const;
  static SuffixMod *make(char *value, char const **error);
};
char const *const SuffixMod::NAME = "Suffix";
ControlBase::Modifier::Type
SuffixMod::type() const
{
  return MOD_SUFFIX;
}
char const *
SuffixMod::name() const
{
  return NAME;
}
bool
SuffixMod::check(HttpRequestData *req) const
{
  int path_len;
  char const *path = req->hdr->url_get()->path_get(&path_len);
  if (1 == static_cast<int>(this->text_vec.count()) && 1 == static_cast<int>(this->text_vec[0].size()) &&
      0 == strcmp(this->text_vec[0].data(), "*"))
    return true;
  for_Vec(ts::Buffer, text_iter, this->text_vec)
  {
    if (path_len >= static_cast<int>(text_iter.size()) &&
        0 == strncasecmp(path + path_len - text_iter.size(), text_iter.data(), text_iter.size()))
      return true;
  }
  return false;
}
SuffixMod *
SuffixMod::make(char *value, char const ** /* error ATS_UNUSED */)
{
  SuffixMod *mod = new SuffixMod();
  mod->set(value);
  return mod;
}

// ----------
struct TagMod : public TextMod {
  static char const *const NAME;

  virtual Type type() const;
  virtual char const *name() const;
  virtual bool check(HttpRequestData *req) const;
  static TagMod *make(char *value, char const **error);
};
char const *const TagMod::NAME = "Tag";
ControlBase::Modifier::Type
TagMod::type() const
{
  return MOD_TAG;
}
char const *
TagMod::name() const
{
  return NAME;
}
bool
TagMod::check(HttpRequestData *req) const
{
  return 0 == strcmp(req->tag, text.data());
}
TagMod *
TagMod::make(char *value, char const ** /* error ATS_UNUSED */)
{
  TagMod *mod = new TagMod();
  mod->set(value);
  return mod;
}

// ----------
struct InternalMod : public ControlBase::Modifier {
  bool flag;
  static char const *const NAME;

  virtual Type
  type() const
  {
    return MOD_INTERNAL;
  }
  virtual char const *
  name() const
  {
    return NAME;
  }
  virtual bool
  check(HttpRequestData *req) const
  {
    return req->internal_txn == flag;
  }
  virtual void
  print(FILE *f) const
  {
    fprintf(f, "%s=%s  ", this->name(), flag ? "true" : "false");
  }
  static InternalMod *make(char *value, char const **error);
};

char const *const InternalMod::NAME = "Internal";

InternalMod *
InternalMod::make(char *value, char const **error)
{
  InternalMod tmp;

  if (0 == strncasecmp("false", value, 5)) {
    tmp.flag = false;
  } else if (0 == strncasecmp("true", value, 4)) {
    tmp.flag = true;
  } else {
    *error = "Value must be true or false";
  }

  if (*error) {
    return NULL;
  } else {
    return new InternalMod(tmp);
  }
}

// ----------
} // anon name space
// ------------------------------------------------
ControlBase::~ControlBase()
{
  this->clear();
}

void
ControlBase::clear()
{
  _mods.delete_and_clear();
}

// static const modifier_el default_el = { MOD_INVALID, NULL };

void
ControlBase::Print()
{
  int n = _mods.length();

  if (0 >= n)
    return;

  printf("\t\t\t");
  for (intptr_t i = 0; i < n; ++i) {
    Modifier *cur_mod = _mods[i];
    if (!cur_mod)
      printf("INVALID  ");
    else
      cur_mod->print(stdout);
  }
  printf("\n");
}

char const *
ControlBase::getSchemeModText() const
{
  char const *zret = 0;
  Modifier *mod    = this->findModOfType(Modifier::MOD_SCHEME);
  if (mod)
    zret = static_cast<SchemeMod *>(mod)->getWksText();
  return zret;
}

bool
ControlBase::CheckModifiers(HttpRequestData *request_data)
{
  if (!request_data->hdr) {
    // we use the same request_data for Socks as well (only IpMatcher)
    // we just return false here
    return true;
  }

  // If the incoming request has no tag but the entry does, or both
  // have tags that do not match, then we do NOT have a match.
  if (!request_data->tag && findModOfType(Modifier::MOD_TAG))
    return false;

  forv_Vec(Modifier, cur_mod, _mods) if (cur_mod && !cur_mod->check(request_data)) return false;

  return true;
}

enum mod_errors {
  ME_UNKNOWN,
  ME_PARSE_FAILED,
  ME_BAD_MOD,
  ME_CALLEE_GENERATED,
};

static const char *errorFormats[] = {
  "Unknown error parsing modifier", "Unable to parse modifier", "Unknown modifier", "Callee Generated",
};

ControlBase::Modifier *
ControlBase::findModOfType(Modifier::Type t) const
{
  forv_Vec(Modifier, m, _mods) if (m && t == m->type()) return m;
  return 0;
}

const char *
ControlBase::ProcessModifiers(matcher_line *line_info)
{
  // Variables for error processing
  const char *errBuf = NULL;
  mod_errors err     = ME_UNKNOWN;

  int n_elts = line_info->num_el; // Element count for line.

  // No elements -> no modifiers.
  if (0 >= n_elts)
    return 0;
  // Can't have more modifiers than elements, so reasonable upper bound.
  _mods.clear();
  _mods.reserve(n_elts);

  // As elements are consumed, the labels are nulled out and the element
  // count decremented. So we have to scan the entire array to be sure of
  // finding all the elements. We'll track the element count so we can
  // escape if we've found all of the elements.
  for (int i = 0; n_elts && ME_UNKNOWN == err && i < MATCHER_MAX_TOKENS; ++i) {
    Modifier *mod = 0;

    char *label = line_info->line[0][i];
    char *value = line_info->line[1][i];

    if (!label)
      continue; // Already use.
    if (!value) {
      err = ME_PARSE_FAILED;
      break;
    }

    if (strcasecmp(label, "port") == 0) {
      mod = PortMod::make(value, &errBuf);
    } else if (strcasecmp(label, "iport") == 0) {
      mod = IPortMod::make(value, &errBuf);
    } else if (strcasecmp(label, "scheme") == 0) {
      mod = SchemeMod::make(value, &errBuf);
    } else if (strcasecmp(label, "method") == 0) {
      mod = MethodMod::make(value, &errBuf);
    } else if (strcasecmp(label, "prefix") == 0) {
      mod = PrefixMod::make(value, &errBuf);
    } else if (strcasecmp(label, "suffix") == 0) {
      mod = SuffixMod::make(value, &errBuf);
    } else if (strcasecmp(label, "src_ip") == 0) {
      mod = SrcIPMod::make(value, &errBuf);
    } else if (strcasecmp(label, "time") == 0) {
      mod = TimeMod::make(value, &errBuf);
    } else if (strcasecmp(label, "tag") == 0) {
      mod = TagMod::make(value, &errBuf);
    } else if (strcasecmp(label, "internal") == 0) {
      mod = InternalMod::make(value, &errBuf);
    } else {
      err = ME_BAD_MOD;
    }

    if (errBuf)
      err = ME_CALLEE_GENERATED; // Mod make failed.

    // If nothing went wrong, add the mod and bump the element count.
    if (ME_UNKNOWN == err) {
      _mods.push_back(mod);
      --n_elts;
    } else {
      delete mod; // we still need to clean up because we didn't transfer ownership.
    }
  }

  if (err != ME_UNKNOWN) {
    this->clear();
    if (err != ME_CALLEE_GENERATED) {
      errBuf = errorFormats[err];
    }
  }

  return errBuf;
}
