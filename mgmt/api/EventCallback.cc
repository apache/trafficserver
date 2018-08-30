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
 * Filename: EventCallback.cc
 * Purpose: Generic module that deals with callbacks and a callback table
 * Created: 01/08/01
 * Created by: lant
 *
 ***************************************************************************/

#include "tscore/ink_config.h"
#include "tscore/ink_memory.h"

#include "EventCallback.h"
#include "CoreAPIShared.h"

/**********************************************************************
 * create_event_callback
 *
 * purpose: allocates and initializes members of EventCallbackT
 * input: None
 * output: EventCallbackT
 * notes: None
 **********************************************************************/
EventCallbackT *
create_event_callback(TSEventSignalFunc func, void *data)
{
  EventCallbackT *event_cb = (EventCallbackT *)ats_malloc(sizeof(EventCallbackT));

  event_cb->func = func;
  event_cb->data = data;

  return event_cb;
}

/**********************************************************************
 * delete_event_callback
 *
 * purpose:frees EventCallbackT
 * input: None
 * output: EventCallbackT
 * notes: also frees memory for the data passed in; ASSUMES data was
 *        dynamically allocated
 **********************************************************************/
void
delete_event_callback(EventCallbackT *event_cb)
{
  ats_free(event_cb);
  return;
}

/**********************************************************************
 * create_callback_table
 *
 * purpose: initializes the structures used to deal with events
 * input: None
 * output: TS_ERR_xx
 * notes: None
 **********************************************************************/
CallbackTable *
create_callback_table(const char *)
{
  CallbackTable *cb_table = (CallbackTable *)ats_malloc(sizeof(CallbackTable));

  for (auto &i : cb_table->event_callback_l) {
    i = nullptr;
  }

  // initialize the mutex
  ink_mutex_init(&cb_table->event_callback_lock);
  return cb_table;
}

/**********************************************************************
 * delete_callback_table
 *
 * purpose: frees the memory allocated for a CallbackTable; also
 *          destroys the lock
 * input: None
 * output: None
 * notes: doesn't free pointers to functions
 **********************************************************************/
void
delete_callback_table(CallbackTable *cb_table)
{
  EventCallbackT *event_cb;

  // get lock
  ink_mutex_acquire(&cb_table->event_callback_lock);

  // for each event
  for (auto &i : cb_table->event_callback_l) {
    if (i) {
      // remove and delete each EventCallbackT for that event
      while (!queue_is_empty(i)) {
        event_cb = (EventCallbackT *)dequeue(i);
        delete_event_callback(event_cb);
      }

      delete_queue(i);
    }
  }

  // release lock
  ink_mutex_release(&cb_table->event_callback_lock);

  // destroy lock
  ink_mutex_destroy(&cb_table->event_callback_lock);

  ats_free(cb_table);

  return;
}

/**********************************************************************
 * get_events_with_callbacks
 *
 * purpose:  returns a list of the event_id's that have at least
 *           one callback registered for that event
 * input: cb_list - the table of callbacks to check
 * output: returns a list of event_ids with at least one callback fun;
 *         returns NULL if all the events have a registered callback
 * notes:
 **********************************************************************/
LLQ *
get_events_with_callbacks(CallbackTable *cb_table)
{
  LLQ *cb_ev_list;
  bool all_events = true; // set to false if at least one event doesn't have registered callback

  cb_ev_list = create_queue();
  for (int i = 0; i < NUM_EVENTS; i++) {
    if (!cb_table->event_callback_l[i]) {
      all_events = false;
      continue; // no callbacks registered
    }

    enqueue(cb_ev_list, &i);
  }

  if (all_events) {
    delete_queue(cb_ev_list);
    return nullptr;
  }

  return cb_ev_list;
}

/**********************************************************************
 * cb_table_register
 *
 * purpose: Registers the specified function for the specified event in
 *          the specified callback list
 * input: cb_list - the table of callbacks to store the callback fn
 *        event_name - the event to store the callback for (if NULL, register for all events)
 *        func - the callback function
 *        first_cb - true only if this is the event's first callback
 * output: TS_ERR_xx
 * notes:
 **********************************************************************/
TSMgmtError
cb_table_register(CallbackTable *cb_table, const char *event_name, TSEventSignalFunc func, void *data, bool *first_cb)
{
  bool first_time = false;
  int id;
  EventCallbackT *event_cb; // create new EventCallbackT EACH TIME enqueue

  // the data and event_name can be NULL
  if (func == nullptr || !cb_table) {
    return TS_ERR_PARAMS;
  }

  ink_mutex_acquire(&(cb_table->event_callback_lock));

  // got lock, add it
  if (event_name == nullptr) { // register for all alarms
    // printf("[EventSignalCbRegister] Register callback for all alarms\n");
    for (auto &i : cb_table->event_callback_l) {
      if (!i) {
        i          = create_queue();
        first_time = true;
      }

      if (!i) {
        ink_mutex_release(&cb_table->event_callback_lock);
        return TS_ERR_SYS_CALL;
      }

      event_cb = create_event_callback(func, data);
      enqueue(i, event_cb);
    }
  } else { // register callback for specific alarm
    // printf("[EventSignalCbRegister] Register callback for %s\n", event_name);
    id = get_event_id(event_name);
    if (id != -1) {
      if (!cb_table->event_callback_l[id]) {
        cb_table->event_callback_l[id] = create_queue();
        first_time                     = true;
      }

      if (!cb_table->event_callback_l[id]) {
        ink_mutex_release(&cb_table->event_callback_lock);
        return TS_ERR_SYS_CALL;
      }
      // now add to list
      event_cb = create_event_callback(func, data);
      enqueue(cb_table->event_callback_l[id], event_cb);
    }
  }

  // release lock on callback table
  ink_mutex_release(&cb_table->event_callback_lock);

  if (first_cb) {
    *first_cb = first_time;
  }

  return TS_ERR_OKAY;
}

/**********************************************************************
 * cb_table_unregister
 *
 * purpose: Unregisters the specified function for the specified event in
 *          the specified callback list
 * input: cb_table - the table of callbacks to store the callback fn
 *        event_name - the event to store the callback for (if NULL, register for all events)
 *        func - the callback function
 *        first_cb - true only if this is the event's first callback
 * output: TS_ERR_xx
 * notes:
 **********************************************************************/
TSMgmtError
cb_table_unregister(CallbackTable *cb_table, const char *event_name, TSEventSignalFunc func)
{
  TSEventSignalFunc cb_fun;
  EventCallbackT *event_cb;

  ink_mutex_acquire(&cb_table->event_callback_lock);

  // got lock, add it
  if (event_name == nullptr) { // unregister the callback for ALL EVENTS
    // for each event
    for (auto &i : cb_table->event_callback_l) {
      if (!i) { // this event has no callbacks
        continue;
      }

      // func == NULL means unregister all functions associated with alarm
      if (func == nullptr) {
        while (!queue_is_empty(i)) {
          event_cb = (EventCallbackT *)dequeue(i);
          delete_event_callback(event_cb);
        }
        // clean up queue and set to NULL
        delete_queue(i);
        i = nullptr;
      } else { // only remove the func passed in
        int queue_depth;

        queue_depth = queue_len(i);
        // remove this function
        for (int j = 0; j < queue_depth; j++) {
          event_cb = (EventCallbackT *)dequeue(i);
          cb_fun   = event_cb->func;

          // the pointers are the same so don't enqueue the fn back on
          if (*cb_fun == *func) {
            delete_event_callback(event_cb);
            continue;
          }

          enqueue(i, event_cb);
        }

        // is queue empty now? then clean up
        if (queue_is_empty(i)) {
          delete_queue(i);
          i = nullptr;
        }
      }
    } // end for (int i = 0; i < NUM_EVENTS; i++) {
  } else {
    // unregister for specific event
    int id = get_event_id(event_name);
    if (id != -1) {
      if (cb_table->event_callback_l[id]) {
        int queue_depth;

        queue_depth = queue_len(cb_table->event_callback_l[id]);
        // func == NULL means unregister all functions associated with alarm
        if (func == nullptr) {
          while (!queue_is_empty(cb_table->event_callback_l[id])) {
            event_cb = (EventCallbackT *)dequeue(cb_table->event_callback_l[id]);
            delete_event_callback(event_cb);
          }

          // clean up queue and set to NULL
          delete_queue(cb_table->event_callback_l[id]);
          cb_table->event_callback_l[id] = nullptr;
        } else {
          // remove this function
          for (int j = 0; j < queue_depth; j++) {
            event_cb = (EventCallbackT *)dequeue(cb_table->event_callback_l[id]);
            cb_fun   = event_cb->func;

            // the pointers are the same
            if (*cb_fun == *func) {
              delete_event_callback(event_cb);
              continue;
            }

            enqueue(cb_table->event_callback_l[id], event_cb);
          }

          // is queue empty now?
          if (queue_is_empty(cb_table->event_callback_l[id])) {
            delete_queue(cb_table->event_callback_l[id]);
            cb_table->event_callback_l[id] = nullptr;
          }
        } // end if NULL else
      }
    }
  }

  ink_mutex_release(&cb_table->event_callback_lock);

  return TS_ERR_OKAY;
}
