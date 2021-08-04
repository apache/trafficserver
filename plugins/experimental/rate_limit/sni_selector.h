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

///////////////////////////////////////////////////////////////////////////////
// SNI based limiter selector
//
class SniSelector
{
public:
  using Limiters = std::unordered_map<std::string_view, SniRateLimiter *>;

  Limiters &
  limiters()
  {
    return _limiters;
  }

  size_t factory(const char *sni_list, int argc, const char *argv[]);
  void setupQueueCont();

  SniRateLimiter *find(std::string_view sni);
  bool insert(std::string_view sni, SniRateLimiter *limiter);
  bool erase(std::string_view sni);

private:
  bool _needs_queue_cont = false;
  TSCont _queue_cont     = nullptr; // Continuation processing the queue periodically
  TSAction _action       = nullptr; // The action associated with the queue continuation, needed to shut it down
  Limiters _limiters;
};
