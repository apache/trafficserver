/** @file

  Implementation of various round robin nexthop selections strategies.

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

#pragma once

#include <mutex>
#include "NextHopSelectionStrategy.h"

class NextHopRoundRobin : public NextHopSelectionStrategy
{
  std::mutex _mutex;
  uint32_t latched_index = 0;

public:
  NextHopRoundRobin() = delete;
  NextHopRoundRobin(const std::string_view &name, const NHPolicyType &policy, ts::Yaml::Map &n)
    : NextHopSelectionStrategy(name, policy, n)
  {
  }
  ~NextHopRoundRobin();
  void findNextHop(TSHttpTxn txnp, void *ih = nullptr, time_t now = 0) override;
};
