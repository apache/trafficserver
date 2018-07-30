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
 * Filename: EventControlMain.h
 * Purpose: Handles event callbacks only
 * Created: 6/26/00
 * Created by: lant
 *
 ***************************************************************************/

#pragma once

#include "mgmtapi.h"       //add the include path b/c included in web dir
#include "CoreAPIShared.h" // for NUM_EVENTS
#include "Alarms.h"

// use events_registered[event_id] as index to check if alarm is registered
typedef struct {
  int fd; // client socket
  struct sockaddr *adr;
  bool events_registered[NUM_EVENTS];
} EventClientT;

EventClientT *new_event_client();
void delete_event_client(EventClientT *client);
void remove_event_client(EventClientT *client, InkHashTable *table);

TSMgmtError init_mgmt_events();
void delete_mgmt_events();
void delete_event_queue(LLQ *q);

void apiAlarmCallback(alarm_t newAlarm, char *ip, char *desc);
void *event_callback_main(void *arg);
