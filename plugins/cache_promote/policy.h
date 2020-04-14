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

#include <string>
#include <time.h>

#include "ts/ts.h"
#include "ts/remap.h"

#define MAX_STAT_LENGTH (1 << 8)
extern const char *PLUGIN_NAME;

//////////////////////////////////////////////////////////////////////////////////////////////
// Abstract base class for all policies.
//
class PromotionPolicy
{
public:
  PromotionPolicy()
  {
    // This doesn't have to be perfect, since this is just chance sampling.
    // coverity[dont_call]
    TSDebug(PLUGIN_NAME, "PromotionPolicy() CTOR");
    srand48(static_cast<long>(time(nullptr)));
  }

  virtual ~PromotionPolicy() = default;

  void
  setSample(char *s)
  {
    _sample = strtof(s, nullptr) / 100.0;
  }

  float
  getSample() const
  {
    return _sample;
  }

  void
  decrementStat(const int stat, const int amount)
  {
    if (stats_enabled) {
      TSStatIntDecrement(stat, amount);
    }
  }

  void
  incrementStat(const int stat, const int amount)
  {
    if (stats_enabled) {
      TSStatIntIncrement(stat, amount);
    }
  }

  virtual bool
  parseOption(int opt, char *optarg)
  {
    return false;
  }

  virtual const std::string
  id() const
  {
    return "";
  }

  bool doSample() const;
  int create_stat(std::string_view name, std::string_view remap_identifier);

  // These are pure virtual
  virtual bool doPromote(TSHttpTxn txnp)       = 0;
  virtual const char *policyName() const       = 0;
  virtual void usage() const                   = 0;
  virtual bool stats_add(const char *remap_id) = 0;

  // when true stats are incremented.
  bool stats_enabled    = false;
  int cache_hits_id     = -1;
  int promoted_id       = -1;
  int total_requests_id = -1;

private:
  float _sample = 0.0;

protected:
  std::string _label = "";
};
