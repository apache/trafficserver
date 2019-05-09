/** @file

  Class definitions for alarms keeper, class keeps a queue of Alarm
  objects. Can be polled on status of alarms.

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

#include "tscore/ink_mutex.h"
#include <unordered_map>
#include <string>

class AppVersionInfo;

/***********************************************************************
 *
 * MODULARIZATION: if you are adding new alarms, please be sure to add
 *                 the corresponding alarms in lib/records/I_RecAlarms.h
 *
 ***********************************************************************/

// When adding new alarms, please make sure add the
//   corresponding alarm text
//
#define MGMT_ALARM_UNDEFINED 0

#define MGMT_ALARM_PROXY_PROCESS_DIED 1
#define MGMT_ALARM_PROXY_PROCESS_BORN 2
// Currently unused: 3
// Currently unused: 4
#define MGMT_ALARM_PROXY_CONFIG_ERROR 5 /* Data is descriptive string */
#define MGMT_ALARM_PROXY_SYSTEM_ERROR 6
// Currently unused: 7
#define MGMT_ALARM_PROXY_CACHE_ERROR 8
#define MGMT_ALARM_PROXY_CACHE_WARNING 9
#define MGMT_ALARM_PROXY_LOGGING_ERROR 10
#define MGMT_ALARM_PROXY_LOGGING_WARNING 11
// Currently unused: 13
#define MGMT_ALARM_CONFIG_UPDATE_FAILED 14
// Currently unused: 15
// Currently unused: 16
#define MGMT_ALARM_MGMT_CONFIG_ERROR 17

extern const char *alarmText[];
extern const int alarmTextNum;

typedef int alarm_t;
typedef void (*AlarmCallbackFunc)(alarm_t, const char *, const char *);

typedef struct _alarm {
  alarm_t type;
  int priority;
  bool linger;
  bool local;
  bool seen;
  unsigned long inet_address; /* If not local */
  char *description;
} Alarm;

class Alarms
{
public:
  Alarms();
  ~Alarms();

  void registerCallback(AlarmCallbackFunc func);
  bool isCurrentAlarm(alarm_t a, char *ip = nullptr);

  void signalAlarm(alarm_t t, const char *desc, const char *ip = nullptr);
  void resolveAlarm(alarm_t a, char *ip = nullptr);

  void constructAlarmMessage(const AppVersionInfo &version, char *ip, char *message, int max);
  void resetSeenFlag(char *ip);
  void clearUnSeen(char *ip);

  void checkSystemNAlert();
  void execAlarmBin(const char *desc);

  const char *getAlarmText(alarm_t id);
  std::unordered_map<std::string, Alarm *> const &
  getLocalAlarms()
  {
    return local_alarms;
  }

private:
  int cur_cb;
  ink_mutex mutex;

  std::unordered_map<std::string, AlarmCallbackFunc> cblist;
  std::unordered_map<std::string, Alarm *> local_alarms;
  std::unordered_map<std::string, Alarm *> remote_alarms;
}; /* End class Alarms */
