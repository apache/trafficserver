/** @file

  Public REC_EVENT defines

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

// copy from mgmt/BaseManager.h
#define REC_EVENT_SYNC_KEY 10000
#define REC_EVENT_SHUTDOWN 10001
#define REC_EVENT_RESTART 10002
#define REC_EVENT_BOUNCE 10003
#define REC_EVENT_CLEAR_STATS 10004
#define REC_EVENT_CONFIG_FILE_UPDATE 10005
#define REC_EVENT_PLUGIN_CONFIG_UPDATE 10006
#define REC_EVENT_ROLL_LOG_FILES 10008
#define REC_EVENT_LIBRECORDS 10009

#define REC_EVENT_CACHE_DISK_CONTROL 10011
