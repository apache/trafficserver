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

#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <time.h>
#include "ts/nexthop.h"

struct HostRecord;

class NextHopHealthStatus
{
public:
  void insert(std::vector<std::shared_ptr<HostRecord>> &hosts);
  void markNextHop(TSHttpTxn txn, const char *hostname, const int port, const NHCmd status, const time_t now = 0);
  NextHopHealthStatus(){};

private:
  std::unordered_map<std::string, std::shared_ptr<HostRecord>> host_map;
};
