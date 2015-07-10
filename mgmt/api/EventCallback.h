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

/***************************************************************************
 * EventCallback.h
 * Purpose:  defines the CallbackTable which stores callback functions for
 *           specific events
 *
 *
 ***************************************************************************/
/*
 * EventCallback.h
 *
 * defines the CallbackTable which stores callback functions for
 * specific events. Used by both local api and remote api.
 */

#ifndef _EVENT_CALLBACK_H_
#define _EVENT_CALLBACK_H_

#include "ts/ink_llqueue.h"

#include "mgmtapi.h"
#include "CoreAPIShared.h"

// when registering the callback function, can pass in void* data which
// will then be passed to the callback function; need to store this data with
// the callback in a struct
typedef struct {
  TSEventSignalFunc func;
  void *data;
} EventCallbackT;

// event_call_back_l is a queue of EventCallbackT
typedef struct {
  LLQ *event_callback_l[NUM_EVENTS];
  ink_mutex event_callback_lock;
} CallbackTable;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

EventCallbackT *create_event_callback(TSEventSignalFunc func, void *data);
void delete_event_callback(EventCallbackT *event_cb);

CallbackTable *create_callback_table(const char *lock_name);

void delete_callback_table(CallbackTable *cb_table);

// returns list of event_id that have at least one callback registered for it
LLQ *get_events_with_callbacks(CallbackTable *cb_table);

TSMgmtError cb_table_register(CallbackTable *cb_table, const char *event_name, TSEventSignalFunc func, void *data, bool *first_cb);
TSMgmtError cb_table_unregister(CallbackTable *cb_table, const char *event_name, TSEventSignalFunc func);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
