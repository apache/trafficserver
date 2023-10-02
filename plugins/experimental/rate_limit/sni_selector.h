/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>

#include "ts/ts.h"
#include "utilities.h"
#include "sni_limiter.h"
#include "ip_reputation.h"

// This is the list of things (CIDR's) to exclude from blocking etc.
// ToDo: This has to be finished
class ListType
{
  const std::string_view
  name() const
  {
    return _name;
  }

  std::string _name; // Name of the list
};

///////////////////////////////////////////////////////////////////////////////
// SNI based limiter selector, this will have one single instance globally.
//
class SniSelector
{
public:
  using Limiters      = std::unordered_map<std::string_view, SniRateLimiter *>;
  using Lists         = std::vector<ListType *>;
  using IPReputations = std::vector<IpReputation::SieveLru *>;

  SniSelector()          = default;
  virtual ~SniSelector() = default; // ToDo: This has to clean stuff up

  bool yamlParser(const std::string yaml_file);

  SniSelector *
  acquire()
  {
    ++_leases;
    return this;
  }

  void
  release()
  {
    if (0 == --_leases) {
      delete this;
    }
  }

  Limiters &
  limiters()
  {
    return _limiters;
  }

  void
  addIPReputation(IpReputation::SieveLru *iprep)
  {
    _reputations.emplace_back(iprep);
  }

  void
  addLimiter(SniRateLimiter *limiter)
  {
    _needs_queue_cont = (limiter->max_queue > 0);
    _limiters.emplace(limiter->name, limiter);
  }
  void setupQueueCont();

  IpReputation::SieveLru *findIpRep(std::string_view name);
  SniRateLimiter *findSNI(std::string_view sni);

private:
  std::string _yaml_file;
  bool _needs_queue_cont = false;
  TSCont _queue_cont     = nullptr;  // Continuation processing the queue periodically
  TSAction _action       = nullptr;  // The action associated with the queue continuation, needed to shut it down
  Limiters _limiters;                // The SNI limiters
  IPReputations _reputations;        // IP-Reputation rules
  Lists _lists;                      // Exclude lists (things not to block)
  std::atomic<uint32_t> _leases = 0; // Number of leases we have on the current selector
};
