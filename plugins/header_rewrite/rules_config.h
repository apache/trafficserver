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

#include <array>
#include <memory>
#include <string>

#include "ts/ts.h"
#include "ruleset.h"
#include "resources.h"

class RulesConfig
{
public:
  RulesConfig(int timezone, int inboundIpSource);
  ~RulesConfig();

  bool parse_config(const std::string &fname, TSHttpHookID default_hook, char *from_url = nullptr, char *to_url = nullptr,
                    bool force_hrw4u = false);

  [[nodiscard]] TSCont
  continuation() const
  {
    return _cont;
  }

  [[nodiscard]] ResourceIDs
  resid(int hook) const
  {
    return _resids[hook];
  }

  [[nodiscard]] RuleSet *
  rule(int hook) const
  {
    return _rules[hook].get();
  }

  [[nodiscard]] int
  timezone() const
  {
    return _timezone;
  }

  [[nodiscard]] int
  inboundIpSource() const
  {
    return _inboundIpSource;
  }

private:
  void add_rule(std::unique_ptr<RuleSet> rule);

  TSCont                                                      _cont;
  std::array<std::unique_ptr<RuleSet>, TS_HTTP_LAST_HOOK + 1> _rules{};
  std::array<ResourceIDs, TS_HTTP_LAST_HOOK + 1>              _resids{};

  int _timezone        = 0;
  int _inboundIpSource = 0;
};
