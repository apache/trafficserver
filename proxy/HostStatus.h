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

#include <time.h>
#include <string>
#include <sstream>
#include "tscore/ink_rwlock.h"
#include "records/P_RecProcess.h"
#include "tscore/ink_hash_table.h"
#include "tscore/ink_rwlock.h"

#include <unordered_map>

// host_status stats prefix.
static const std::string stat_prefix = "proxy.process.host_status.";

enum HostStatus_t {
  HOST_STATUS_INIT,
  HOST_STATUS_DOWN,
  HOST_STATUS_UP,
};

static const constexpr char *HostStatusNames[3] = {"HOST_STATUS_INIT", "HOST_STATUS_DOWN", "HOST_STATUS_UP"};
static const constexpr char *ReasonStatus[2]    = {"UP", "DOWN"};

struct Reason {
  static constexpr const unsigned int ACTIVE      = 0x1;
  static constexpr const unsigned int LOCAL       = 0x2;
  static constexpr const unsigned int MANUAL      = 0x4;
  static constexpr const unsigned int SELF_DETECT = 0x8;
  static constexpr const unsigned int ALL         = 0xf;

  static constexpr const char *ACTIVE_REASON      = "active";
  static constexpr const char *LOCAL_REASON       = "local";
  static constexpr const char *MANUAL_REASON      = "manual";
  static constexpr const char *SELF_DETECT_REASON = "self_detect";
  static constexpr const char *ALL_REASON         = "all";

  static constexpr const char *reasons[3] = {ACTIVE_REASON, LOCAL_REASON, MANUAL_REASON};

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

  static unsigned int
  getReason(const char *reason_str)
  {
    if (strcmp(reason_str, ACTIVE_REASON) == 0) {
      return ACTIVE;
    } else if (strcmp(reason_str, LOCAL_REASON) == 0) {
      return LOCAL;
    } else if (strcmp(reason_str, MANUAL_REASON) == 0) {
      return MANUAL;
    } else if (strcmp(reason_str, SELF_DETECT_REASON) == 0) {
      return SELF_DETECT;
    } else if (strcmp(reason_str, ALL_REASON) == 0) {
      return ALL;
    }
    // default is MANUAL
    return MANUAL;
  }
};

// host status POD
struct HostStatRec {
  HostStatus_t status;
  unsigned int reasons;
  // time the host was marked down for a given reason.
  time_t active_marked_down;
  time_t local_marked_down;
  time_t manual_marked_down;
  time_t self_detect_marked_down;
  // number of seconds that the host should be marked down for a given reason.
  unsigned int active_down_time;
  unsigned int local_down_time;
  unsigned int manual_down_time;

  HostStatRec();
  HostStatRec(std::string str);
  HostStatRec(const HostStatRec &src)
  {
    status                  = src.status;
    reasons                 = src.reasons;
    active_marked_down      = src.active_marked_down;
    active_down_time        = src.active_down_time;
    local_marked_down       = src.local_marked_down;
    local_down_time         = src.local_down_time;
    manual_marked_down      = src.manual_marked_down;
    manual_down_time        = src.manual_down_time;
    self_detect_marked_down = src.self_detect_marked_down;
  }
  ~HostStatRec() {}

  HostStatRec &operator=(const HostStatRec &source) = default;

  // serialize this HostStatusRec
  std::stringstream &
  operator<<(std::stringstream &os)
  {
    unsigned int r = getReasonState(Reason::ACTIVE);
    os << HostStatusNames[status];
    os << ",ACTIVE:" << ReasonStatus[r] << ":" << active_marked_down << ":" << active_down_time;
    r = getReasonState(Reason::LOCAL);
    os << ",LOCAL:" << ReasonStatus[r] << ":" << local_marked_down << ":" << local_down_time;
    r = getReasonState(Reason::MANUAL);
    os << ",MANUAL:" << ReasonStatus[r] << ":" << manual_marked_down << ":" << manual_down_time;
    r = getReasonState(Reason::SELF_DETECT);
    os << ",SELF_DETECT:" << ReasonStatus[r] << ":" << self_detect_marked_down;

    return os;
  }

  // serialize a HostStatRec
  friend std::stringstream &
  operator<<(std::stringstream &os, HostStatRec &hs)
  {
    unsigned int r = hs.getReasonState(Reason::ACTIVE);
    os << HostStatusNames[hs.status];
    os << ",ACTIVE:" << ReasonStatus[r] << ":" << hs.active_marked_down << ":" << hs.active_down_time;
    r = hs.getReasonState(Reason::LOCAL);
    os << ",LOCAL:" << ReasonStatus[r] << ":" << hs.local_marked_down << ":" << hs.local_down_time;
    r = hs.getReasonState(Reason::MANUAL);
    os << ",MANUAL:" << ReasonStatus[r] << ":" << hs.manual_marked_down << ":" << hs.manual_down_time;
    r = hs.getReasonState(Reason::SELF_DETECT);
    os << ",SELF_DETECT:" << ReasonStatus[r] << ":" << hs.self_detect_marked_down;

    return os;
  }

  inline unsigned int
  getReasonState(unsigned int reason)
  {
    unsigned int r = 0;
    if (reasons == 0) {
      r = 0;
    } else if (reasons & reason) {
      r = 1;
    }
    return r;
  }
};

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
  void setHostStatus(const char *name, const HostStatus_t status, const unsigned int down_time, const unsigned int reason);
  HostStatRec *getHostStatus(const char *name);
  void createHostStat(const char *name, const char *data = nullptr);
  void loadHostStatusFromStats();
  void loadRecord(std::string &name, HostStatRec &h);
  int getHostStat(std::string &stat_name, char *buf, unsigned int buf_len);

private:
  HostStatus();
  HostStatus(const HostStatus &obj) = delete;
  HostStatus &operator=(HostStatus const &) = delete;

  // next hop status, key is hostname or ip string, data is HostStatRec
  std::unordered_map<std::string, HostStatRec *> hosts_statuses;

  ink_rwlock host_status_rwlock;
};
