/** @file

  Public REC_ALARM defines

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

// copy from mgmt/Alarms.h
#define REC_ALARM_PROXY_PROCESS_DIED 1
#define REC_ALARM_PROXY_PROCESS_BORN 2
#define REC_ALARM_PROXY_PEER_BORN 3
#define REC_ALARM_PROXY_PEER_DIED 4
#define REC_ALARM_PROXY_CONFIG_ERROR 5
#define REC_ALARM_PROXY_SYSTEM_ERROR 6
#define REC_ALARM_PROXY_LOG_SPACE_CRISIS 7
#define REC_ALARM_PROXY_CACHE_ERROR 8
#define REC_ALARM_PROXY_CACHE_WARNING 9
#define REC_ALARM_PROXY_LOGGING_ERROR 10
#define REC_ALARM_PROXY_LOGGING_WARNING 11
// Currently unused: 12
#define REC_ALARM_REC_TEST 13
#define REC_ALARM_CONFIG_UPDATE_FAILED 14
#define REC_ALARM_WEB_ERROR 15
#define REC_ALARM_PING_FAILURE 16
#define REC_ALARM_REC_CONFIG_ERROR 17
#define REC_ALARM_ADD_ALARM 18
#define REC_ALARM_PROXY_LOG_SPACE_ROLLED 19
