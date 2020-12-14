/** @file

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

#include "ts/apidefs.h"
#include "tscore/ConsistentHash.h"

// Needed by core. Disabling the warning for plugins
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
static const char *ParentResultStr[] = {"PARENT_UNDEFINED", "PARENT_DIRECT", "PARENT_SPECIFIED", "PARENT_AGENT", "PARENT_FAIL"};
#pragma GCC diagnostic pop

struct TSParentResult {
  const char *hostname;
  int port;
  bool retry;
  TSParentResultType result;
  bool chash_init[TS_MAX_GROUP_RINGS] = {false};
  TSHostStatus first_choice_status = TSHostStatus::TS_HOST_STATUS_INIT;
  // Internal use only
  //   Not to be modified by HTTP
  int line_number;
  uint32_t last_parent;
  uint32_t start_parent;
  uint32_t last_group;
  bool wrap_around;
  bool mapWrapped[2];
  // state for consistent hash.
  int last_lookup;
  ATSConsistentHashIter chashIter[TS_MAX_GROUP_RINGS];
};
