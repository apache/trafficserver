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
#include <atomic>

#include "ts/ts.h"
#include "sni_limiter.h"
#include "utilities.h"
#include "ip_reputation.h"
#include "lists.h"

///////////////////////////////////////////////////////////////////////////////
// SNI based limiter selector, this will have one singleton instance.
//
class SniSelector
{
  using self_type = SniSelector;

public:
  using Limiters      = std::unordered_map<std::string, std::tuple<bool, SniRateLimiter *>>;
  using IPReputations = std::vector<IpReputation::SieveLru *>;
  using Lists         = std::vector<List::IP *>;

  SniSelector() = default;

  SniSelector(self_type &&)               = delete;
  self_type &operator=(const self_type &) = delete;
  self_type &operator=(self_type &&)      = delete;

  virtual ~SniSelector()
  {
    if (_queue_action) {
      TSActionCancel(_queue_action);
    }

    if (_queue_cont) {
      TSContDestroy(_queue_cont);
    }

    for (auto &iprep : _reputations) {
      delete iprep;
    }

    for (auto &list : _lists) {
      delete list;
    }

    delete _default;
    for (auto &limiter : _limiters) {
      auto &[owner, ptr] = limiter.second;

      if (owner) {
        delete ptr;
      }
    }
  }

  static void
  swap(self_type *other)
  {
    _instance.exchange(other);
  }

  static SniSelector *
  instance()
  {
    auto sel = _instance.load();
    ++sel->_leases;
    return sel;
  }

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

  SniRateLimiter *
  findLimiter(const std::string &sni)
  {
    auto iter = _limiters.find(sni);

    return ((iter != _limiters.end()) ? std::get<1>(iter->second) : _default);
  }

  void
  addLimiter(SniRateLimiter *limiter)
  {
    _needs_queue_cont |= (limiter->max_queue() > 0);
    _limiters.emplace(limiter->name(), std::forward_as_tuple(true, limiter));
  }

  void
  addAlias(std::string alias, SniRateLimiter *limiter)
  {
    _limiters.emplace(alias, std::forward_as_tuple(false, limiter));
  }

  const std::string &
  yamlFile() const
  {
    return _yaml_file;
  }

  void
  addIPReputation(IpReputation::SieveLru *iprep)
  {
    _reputations.emplace_back(iprep);
  }

  IpReputation::SieveLru *
  findIpRep(const std::string &name)
  {
    auto it = std::find_if(_reputations.begin(), _reputations.end(),
                           [&name](const IpReputation::SieveLru *iprep) { return iprep->name() == name; });

    if (it != _reputations.end()) {
      return *it;
    }

    return nullptr;
  }

  void
  addList(List::IP *list)
  {
    _lists.emplace_back(list);
  }

  List::IP *
  findList(const std::string &name)
  {
    auto it = std::find_if(_lists.begin(), _lists.end(), [&name](const List::IP *list) { return list->name() == name; });

    if (it != _lists.end()) {
      return *it;
    }

    return nullptr;
  }

  void setupQueueCont();
  bool yamlParser(const std::string &yaml_file);

  static void startup(const std::string &yaml_file);

private:
  std::string _yaml_file;
  bool _needs_queue_cont = false;
  TSCont _queue_cont     = nullptr;   // Continuation processing the queue periodically
  TSAction _queue_action = nullptr;   // The action associated with the queue continuation, needed to shut it down
  Limiters _limiters;                 // The SNI limiters
  SniRateLimiter *_default = nullptr; // Default limiter, if any
  IPReputations _reputations;         // IP-Reputation rules
  Lists _lists;                       // IP lists (for now, could be generalized later)
  std::atomic<uint32_t> _leases = 0;  // Number of leases we have on the current selector, start with one

  static std::atomic<self_type *> _instance; // Holds the singleton instance, initialized in the .cc file
};
