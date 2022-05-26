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

#include "policy.h"

//////////////////////////////////////////////////////////////////////////////////////////////
// This is the simplest of all policies, just give each request a (small)
// percentage chance to be promoted to cache.
//
class ChancePolicy : public PromotionPolicy
{
public:
  bool doPromote(TSHttpTxn /* txnp ATS_UNUSED */) override
  {
    TSDebug(PLUGIN_NAME, "ChancePolicy::doPromote(%f)", getSample());
    incrementStat(_promoted_id, 1);

    return true;
  }

  void
  usage() const override
  {
    TSError("[%s] Usage: @plugin=%s.so @pparam=--policy=chance @pparam=--sample=<x>%%", PLUGIN_NAME, PLUGIN_NAME);
  }

  const char *
  policyName() const override
  {
    return "chance";
  }

  bool
  stats_add(const char *remap_id) override
  {
    std::string_view remap_identifier                 = remap_id;
    const std::tuple<std::string_view, int *> stats[] = {
      {"cache_hits", &_cache_hits_id},
      {"promoted", &_promoted_id},
      {"total_requests", &_total_requests_id},
    };

    if (nullptr == remap_id) {
      TSError("[%s] no remap identifier specified for stats, no stats will be used", PLUGIN_NAME);
      return false;
    }

    for (int ii = 0; ii < 3; ii++) {
      std::string_view name = std::get<0>(stats[ii]);
      int *id               = std::get<1>(stats[ii]);

      if ((*(id) = create_stat(name, remap_identifier)) == TS_ERROR) {
        return false;
      }
    }

    return true;
  }
};
