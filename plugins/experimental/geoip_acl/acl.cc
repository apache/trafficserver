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
#include <arpa/inet.h>

#include <string>
#include <iostream>
#include <fstream>

#include "acl.h"
#include "lulu.h"

// Global GeoIP object.
GeoIP *gGI;

// Implementation of the ACL base class.
void
Acl::read_html(const char *fn)
{
  std::ifstream f;

  f.open(fn, std::ios::in);
  if (f.is_open()) {
    _html.append(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    f.close();
    TSDebug(PLUGIN_NAME, "Loaded HTML from %s", fn);
  } else {
    TSError("Unable to open HTML file %s", fn);
  }
}


// Implementations for the RegexAcl class
bool
RegexAcl::parse_line(const char *filename, const std::string &line, int lineno)
{
  static const char _SEPARATOR[] = " \t\n";
  std::string regex, tmp;
  std::string::size_type pos1, pos2;

  if (line.empty())
    return false;
  pos1 = line.find_first_not_of(_SEPARATOR);
  if (line[pos1] == '#' || pos1 == std::string::npos)
    return false;

  pos2 = line.find_first_of(_SEPARATOR, pos1);
  if (pos2 != std::string::npos) {
    regex = line.substr(pos1, pos2 - pos1);
    pos1 = line.find_first_not_of(_SEPARATOR, pos2);
    if (pos1 != std::string::npos) {
      pos2 = line.find_first_of(_SEPARATOR, pos1);
      if (pos2 != std::string::npos) {
        tmp = line.substr(pos1, pos2 - pos1);
        if (tmp == "allow")
          _acl->set_allow(true);
        else if (tmp == "deny")
          _acl->set_allow(false);
        else {
          TSError("Bad action on in %s:line %d: %s", filename, lineno, tmp.c_str());
          return false;
        }
        // The rest are "tokens"
        while ((pos1 = line.find_first_not_of(_SEPARATOR, pos2)) != std::string::npos) {
          pos2 = line.find_first_of(_SEPARATOR, pos1);
          tmp = line.substr(pos1, pos2 - pos1);
          _acl->add_token(tmp);
        }
        compile(regex, filename, lineno);
        TSDebug(PLUGIN_NAME, "Added regex rule for /%s/", regex.c_str());
        return true;
      }
    }
  }

  return false;
}

bool
RegexAcl::compile(const std::string &str, const char *filename, int lineno)
{
  const char *error;
  int erroffset;

  _regex_s = str;
  _rex = pcre_compile(_regex_s.c_str(), 0, &error, &erroffset, NULL);

  if (NULL != _rex) {
    _extra = pcre_study(_rex, 0, &error);
    if ((NULL == _extra) && error && (*error != 0)) {
      TSError("Failed to study regular expression in %s:line %d at offset %d: %s\n", filename, lineno, erroffset, error);
      return false;
    }
  } else {
    TSError("Failed to compile regular expression in %s:line %d: %s\n", filename, lineno, error);
    return false;
  }

  return true;
}

void
RegexAcl::append(RegexAcl *ra)
{
  if (NULL == _next) {
    _next = ra;
  } else {
    RegexAcl *cur = _next;

    while (cur->_next)
      cur = cur->_next;
    cur->_next = ra;
  }
}


// Implementation of the Country ACL class.
void
CountryAcl::add_token(const std::string &str)
{
  int iso = -1;

  Acl::add_token(str);
  iso = GeoIP_id_by_code(str.c_str());

  if (iso > 0 && iso < NUM_ISO_CODES) {
    _iso_country_codes[iso] = true;
    TSDebug(PLUGIN_NAME, "Added %s(%d) to remap rule, ACL=%d", str.c_str(), iso, _allow);
  } else {
    TSError("Tried setting an ISO code (%d) outside the supported range", iso);
  }
}

void
CountryAcl::read_regex(const char *fn)
{
  std::ifstream f;
  int lineno = 0;

  f.open(fn, std::ios::in);
  if (f.is_open()) {
    std::string line;
    RegexAcl *acl = NULL;

    while (!f.eof()) {
      getline(f, line);
      ++lineno;
      if (!acl)
        acl = new RegexAcl(new CountryAcl());
      if (acl->parse_line(fn, line, lineno)) {
        if (NULL == _regexes)
          _regexes = acl;
        else
          _regexes->append(acl);
        acl = NULL;
      }
    }
    f.close();
    TSDebug(PLUGIN_NAME, "Loaded regex rules from %s", fn);
  } else {
    TSError("Unable to open regex file %s", fn);
  }
}

bool
CountryAcl::eval(TSRemapRequestInfo *rri, TSHttpTxn txnp) const
{
  // If there are regex rules, they take priority first. If a regex matches, we will
  // honor it's eval() rule. If no regexes matches, fall back on the default (which is
  // "allow" if nothing else is specified).
  if (NULL != _regexes) {
    RegexAcl *acl = _regexes;
    int path_len;
    const char *path = TSUrlPathGet(rri->requestBufp, rri->requestUrl, &path_len);

    do {
      if (acl->match(path, path_len)) {
        TSDebug(PLUGIN_NAME, "Path = %.*s matched /%s/", path_len, path, acl->get_regex().c_str());
        return acl->eval(rri, txnp);
      }
    } while ((acl = acl->next()));
  }

  // This is a special case for when there are no ISO codes. It got kinda wonky without it.
  if (0 == _added_tokens)
    return _allow;

  // None of the regexes (if any) matched, so fallback to the remap defaults if there are any.
  int iso = -1;
  const sockaddr *addr = TSHttpTxnClientAddrGet(txnp);

  switch (addr->sa_family) {
  case AF_INET: {
    uint32_t ip = ntohl(reinterpret_cast<const struct sockaddr_in *>(addr)->sin_addr.s_addr);

    iso = GeoIP_id_by_ipnum(gGI, ip);
    if (TSIsDebugTagSet(PLUGIN_NAME)) {
      const char *c = GeoIP_country_code_by_ipnum(gGI, ip);
      TSDebug(PLUGIN_NAME, "eval(): IP=%u seems to come from ISO=%d / %s", ip, iso, c);
    }
  } break;
  case AF_INET6:
    return true;
  default:
    break;
  }

  if ((iso <= 0) || (!_iso_country_codes[iso]))
    return !_allow;

  return _allow;
}


void
CountryAcl::process_args(int argc, char *argv[])
{
  for (int i = 3; i < argc; ++i) {
    if (!strncmp(argv[i], "allow", 5)) {
      _allow = true;
    } else if (!strncmp(argv[i], "deny", 4)) {
      _allow = false;
    } else if (!strncmp(argv[i], "regex::", 7)) {
      read_regex(argv[i] + 7);
    } else if (!strncmp(argv[i], "html::", 6)) {
      read_html(argv[i] + 6);
    } else { // ISO codes assumed for the rest
      add_token(argv[i]);
    }
  }
}
