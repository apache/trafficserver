/** @file

  A brief file description

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

/*****************************************************************************
 *
 *  HostStatus.h - Interface to Host Status System
 *
 *
 ****************************************************************************/

#pragma once

#include <ctime>
#include <string>
#include "tscore/ink_rwlock.h"
#include "records/P_RecProcess.h"

#include <unordered_map>

enum HostStatus_t {
  HOST_STATUS_INIT,
  HOST_STATUS_DOWN,
  HOST_STATUS_UP,
};

struct HostStatRec_t {
  HostStatus_t status;
  time_t marked_down;     // the time that this host was marked down.
  unsigned int down_time; // number of seconds that the host should be down, 0 is indefinately
};

struct Reasons {
  static constexpr const char *ACTIVE      = "active";
  static constexpr const char *LOCAL       = "local";
  static constexpr const char *MANUAL      = "manual";
  static constexpr const char *SELF_DETECT = "self_detect";

  static constexpr const char *reasons[4] = {ACTIVE, LOCAL, MANUAL, SELF_DETECT};

  static bool
  validReason(const char *reason)
  {
    for (const char *i : reasons) {
      if (strcmp(i, reason) == 0) {
        return true;
      }
    }
    return false;
  }
};

static const std::string stat_prefix = "proxy.process.host_status.";

/**
 * Singleton placeholder for next hop status.
 */
struct HostStatus {
  ~HostStatus();

  static HostStatus &
  instance()
  {
    static HostStatus instance;
    return instance;
  }
  void setHostStatus(const char *name, const HostStatus_t status, const unsigned int down_time, const char *reason);
  HostStatus_t getHostStatus(const char *name);
  void createHostStat(const char *name);
  int getHostStatId(const char *name);

private:
  int next_stat_id = 1;
  HostStatus();
  HostStatus(const HostStatus &obj) = delete;
  HostStatus &operator=(HostStatus const &) = delete;

  // next hop status, key is hostname or ip string, data is bool (available).
  std::unordered_map<std::string, HostStatRec_t *> hosts_statuses;
  // next hop stat ids, key is hostname or ip string, data is int stat id.
  std::unordered_map<std::string, int> hosts_stats_ids;

  ink_rwlock host_status_rwlock;
  ink_rwlock host_statids_rwlock;
};
