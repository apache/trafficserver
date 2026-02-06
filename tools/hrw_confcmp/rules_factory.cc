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

// Factory functions to create/destroy RulesConfig without exposing the full definition.
// This avoids ODR violations between main.cc and the library.

#include "parser.h"
#include "ruleset.h"
#include "resources.h"

// Forward declare RulesConfig from header_rewrite.cc
class RulesConfig;

#include <fstream>
#include <mutex>
#include <string>
#include <stack>
#include <stdexcept>
#include <array>

#include "ts/ts.h"
#include "hrw4u.h"

// RulesConfig definition must match header_rewrite.cc exactly
class RulesConfig
{
public:
  RulesConfig(int timezone, int inboundIpSource);
  ~RulesConfig();

  bool parse_config(const std::string &fname, TSHttpHookID default_hook, char *from_url = nullptr, char *to_url = nullptr,
                    bool force_hrw4u = false);

  ResourceIDs resid(int hook) const;
  RuleSet    *rule(int hook) const;
  int         timezone() const;
  int         inboundIpSource() const;

private:
  void add_rule(std::unique_ptr<RuleSet> rule);

  TSCont                                                      _cont;
  std::array<std::unique_ptr<RuleSet>, TS_HTTP_LAST_HOOK + 1> _rules{};
  std::array<ResourceIDs, TS_HTTP_LAST_HOOK + 1>              _resids{};

  int _timezone        = 0;
  int _inboundIpSource = 0;
};

// Factory function implementations
extern "C" {

RulesConfig *
create_rules_config(int timezone, int inboundIpSource)
{
  return new RulesConfig(timezone, inboundIpSource);
}

void
destroy_rules_config(RulesConfig *conf)
{
  delete conf;
}

bool
rules_config_parse(RulesConfig *conf, const char *fname, int default_hook, char *from_url, char *to_url, bool force_hrw4u)
{
  return conf->parse_config(fname, static_cast<TSHttpHookID>(default_hook), from_url, to_url, force_hrw4u);
}

RuleSet *
rules_config_get_rule(RulesConfig *conf, int hook)
{
  return conf->rule(hook);
}

} // extern "C"
