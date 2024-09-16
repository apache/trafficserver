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

#pragma once

#include <cstdio>
#include <cstring>
#include <arpa/inet.h>
#include <ts/ts.h>
#include <ts/remap.h>
#include <ts/ts.h>
#include <ts/remap.h>
#include <string>
#include <cstring>
#include <iostream>
#include <fstream>
#include <yaml-cpp/yaml.h>
#include <unordered_map>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iterator>
#include <maxminddb.h>
#include "swoc/swoc_ip.h"

#ifdef HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#else
#include <pcre.h>
#endif

#define PLUGIN_NAME  "maxmind_acl"
#define CONFIG_TMOUT 60000

namespace maxmind_acl_ns
{
extern DbgCtl dbg_ctl;
}
using namespace maxmind_acl_ns;

struct plugin_regex {
  std::string _regex_s;
  pcre       *_rex;
  pcre_extra *_extra;
};

enum ipstate { ALLOW_IP, DENY_IP, UNKNOWN_IP };

// Base class for all ACLs
class Acl
{
public:
  Acl() { memset(&_mmdb, 0, sizeof(_mmdb)); }
  ~Acl()
  {
    if (db_loaded) {
      MMDB_close(&_mmdb);
    }
  }

  bool eval(TSRemapRequestInfo *rri, TSHttpTxn txnp);
  bool init(char const *filename);

  void
  send_html(TSHttpTxn txnp) const
  {
    if (_html.size() > 0) {
      char *msg = TSstrdup(_html.c_str());

      TSHttpTxnErrorBodySet(txnp, msg, _html.size(), nullptr); // Defaults to text/html
    }
  }

protected:
  // Class members
  std::string                           configloc;
  YAML::Node                            _config;
  MMDB_s                                _mmdb;
  std::string                           _html;
  std::unordered_map<std::string, bool> allow_country;

  std::unordered_map<std::string, std::vector<plugin_regex>> allow_regex;
  std::unordered_map<std::string, std::vector<plugin_regex>> deny_regex;

  swoc::IPRangeSet allow_ip_map;
  swoc::IPRangeSet deny_ip_map;

  // Anonymous blocking default to off
  bool _anonymous_ip      = false;
  bool _anonymous_vpn     = false;
  bool _hosting_provider  = false;
  bool _public_proxy      = false;
  bool _tor_exit_node     = false;
  bool _residential_proxy = false;

  // GeoGuard specific fields
  bool _vpn_datacenter  = false;
  bool _relay_proxy     = false;
  bool _proxy_over_vpn  = false;
  bool _smart_dns_proxy = false;

  bool _anonymous_blocking = false;

  // Do we want to allow by default or not? Useful
  // for deny only rules
  bool default_allow = false;
  bool db_loaded     = false;

  bool    loaddb(const YAML::Node &dbNode);
  bool    loadallow(const YAML::Node &allowNode);
  bool    loaddeny(const YAML::Node &denyNode);
  void    loadhtml(const YAML::Node &htmlNode);
  bool    loadanonymous(const YAML::Node &anonNode);
  bool    eval_country(MMDB_entry_data_s *entry_data, const std::string &url);
  bool    eval_anonymous(MMDB_entry_s *entry_data);
  void    parseregex(const YAML::Node &regex, bool allow);
  ipstate eval_ip(const sockaddr *sock) const;
};
