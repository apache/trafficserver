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
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

#include <ts/ts.h>
#include <ts/remap.h>

#include "ts/ink_defs.h"

#ifdef HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#else
#include <pcre.h>
#endif

#include <string>
#include "lulu.h"

#if HAVE_GEOIP_H
#include <GeoIP.h>
typedef GeoIP *GeoDBHandle;
#else  /* !HAVE_GEOIP_H */
typedef void *GeoDBHandle;
#endif /* HAVE_GEOIP_H */

// See http://www.iso.org/iso/english_country_names_and_code_elements
// Maxmind allocates 253 country codes,even though there are only 248 according to the above
static const int NUM_ISO_CODES = 253;

// Base class for all ACLs
class Acl
{
public:
  Acl() : _allow(true), _added_tokens(0) {}
  virtual ~Acl() {}
  // These have to be implemented for each ACL type
  virtual void read_regex(const char *fn, int &tokens)             = 0;
  virtual int process_args(int argc, char *argv[])                 = 0;
  virtual bool eval(TSRemapRequestInfo *rri, TSHttpTxn txnp) const = 0;
  virtual void add_token(const std::string &str) = 0;

  void
  set_allow(bool allow)
  {
    _allow = allow;
  }

  void
  send_html(TSHttpTxn txnp) const
  {
    if (_html.size() > 0) {
      char *msg = TSstrdup(_html.c_str());

      TSHttpTxnErrorBodySet(txnp, msg, _html.size(), NULL); // Defaults to text/html
    }
  }

  void read_html(const char *fn);

  int country_id_by_code(const std::string &str) const;
  int country_id_by_addr(const sockaddr *addr) const;

  static bool init();

protected:
  std::string _html;
  bool _allow;
  int _added_tokens;

  // Class members
  static GeoDBHandle _geoip;
  static GeoDBHandle _geoip6;
};

// Base class for all Regex ACLs (which contain Acl() subclassed instances)
class RegexAcl
{
public:
  RegexAcl(Acl *acl) : _extra(NULL), _next(NULL), _acl(acl) {}
  const std::string &
  get_regex() const
  {
    return _regex_s;
  };
  bool
  eval(TSRemapRequestInfo *rri, TSHttpTxn txnp) const
  {
    return _acl->eval(rri, txnp);
  }
  RegexAcl *
  next() const
  {
    return _next;
  }

  bool
  match(const char *str, int len) const
  {
    if (0 == len) {
      return false;
    }

    return (pcre_exec(_rex, _extra, str, len, 0, PCRE_NOTEMPTY, NULL, 0) != -1);
  }

  void append(RegexAcl *ra);
  bool parse_line(const char *filename, const std::string &line, int lineno, int &tokens);

private:
  bool compile(const std::string &str, const char *filename, int lineno);
  std::string _regex_s;
  pcre *_rex;
  pcre_extra *_extra;
  RegexAcl *_next;
  Acl *_acl;
};

// ACLs based on ISO country codes.
class CountryAcl : public Acl
{
public:
  CountryAcl() : _regexes(NULL) { memset(_iso_country_codes, 0, sizeof(_iso_country_codes)); }
  void read_regex(const char *fn, int &tokens);
  int process_args(int argc, char *argv[]);
  bool eval(TSRemapRequestInfo *rri, TSHttpTxn txnp) const;
  void add_token(const std::string &str);

private:
  bool _iso_country_codes[NUM_ISO_CODES];
  RegexAcl *_regexes;
};
