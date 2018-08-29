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

#include "ts/ink_hash_table.h"
#include "ts/ink_mutex.h"

class AppVersionInfo;

/***********************************************************************
 *
 * MODULARIZATTION: if you are adding new alarms, please ensure to add
 *                 the corresponding alarms in lib/records/I_RecAlarms.h
 *
 ***********************************************************************/

// When adding new alarms, please make sure add the
//   corresponding alarm text
//
#define MGMT_ALARM_PROXY_PROCESS_DIED 1
#define MGMT_ALARM_PROXY_PROCESS_BORN 2
#define MGMT_ALARM_PROXY_PEER_BORN 3 /* Data is ip addr */
#define MGMT_ALARM_PROXY_PEER_DIED 4
#define MGMT_ALARM_PROXY_CONFIG_ERROR 5 /* Data is descriptive string */
#define MGMT_ALARM_PROXY_SYSTEM_ERROR 6
#define MGMT_ALARM_PROXY_LOG_SPACE_CRISIS 7
#define MGMT_ALARM_PROXY_CACHE_ERROR 8
#define MGMT_ALARM_PROXY_CACHE_WARNING 9
#define MGMT_ALARM_PROXY_LOGGING_ERROR 10
#define MGMT_ALARM_PROXY_LOGGING_WARNING 11
#define MGMT_ALARM_MGMT_TEST 13 /* to aid in debugging */
#define MGMT_ALARM_CONFIG_UPDATE_FAILED 14
#define MGMT_ALARM_WEB_ERROR 15
#define MGMT_ALARM_PING_FAILURE 16
#define MGMT_ALARM_MGMT_CONFIG_ERROR 17
#define MGMT_ALARM_ADD_ALARM 18              /* OEM_ALARM */
#define MGMT_ALARM_PROXY_LOG_SPACE_ROLLED 19 /* Alarm when log files will be rolled */

#define MGMT_ALARM_SAC_SERVER_DOWN 400

extern const char *alarmText[];
extern const int alarmTextNum;

/* OEM_ALARM: the alarm type is used as a key for hash tables;
   need offset and modulo constants which will keep the unique
   keys for OEM alarms within a specified range */
const int minOEMkey = 1000; // used as offset
const int maxOEMkey = 6000;

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
  InkHashTable *
  getLocalAlarms()
  {
    return local_alarms;
  }

private:
  int cur_cb;
  ink_mutex mutex;

  InkHashTable *cblist;
  InkHashTable *local_alarms;
  InkHashTable *remote_alarms;

  /* counter is added in order to provide unique keys for OEM alarms,
     since an OEM_ALARM type can be associated with many different
     alarm descriptions */
  int alarmOEMcount;

}; /* End class Alarms */
