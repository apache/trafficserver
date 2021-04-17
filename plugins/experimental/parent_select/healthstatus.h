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
#include "ts/parentselectdefs.h"

struct PLHostRecord;

enum PLNHCmd { PL_NH_MARK_UP, PL_NH_MARK_DOWN };

typedef enum {
  PL_NH_PARENT_UNDEFINED,
  PL_NH_PARENT_DIRECT,
  PL_NH_PARENT_SPECIFIED,
  PL_NH_PARENT_AGENT,
  PL_NH_PARENT_FAIL,
} PLNHParentResultType;

struct PLStatusTxn {
  PLNHParentResultType result = PL_NH_PARENT_UNDEFINED;
  bool retry                  = false;
};

class PLNextHopHealthStatus
{
public:
  void insert(std::vector<std::shared_ptr<PLHostRecord>> &hosts);
  void mark(TSHttpTxn txn, PLStatusTxn *healthTxn, const char *hostname, const size_t hostname_len, const in_port_t port,
            const PLNHCmd status, const time_t now = 0);
  PLNextHopHealthStatus(){};

private:
  std::unordered_map<std::string, std::shared_ptr<PLHostRecord>> host_map;
};
