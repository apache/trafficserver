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

/**************************************
 *
 * BaseManager.cc
 *   Member function definitions for Base Manager class.
 *
 * $Date: 2003-06-01 18:37:18 $
 *
 *
 */

#include "ts/ink_platform.h"
#include "ts/ink_hash_table.h"
#include "ts/ink_memory.h"
#include "BaseManager.h"

BaseManager::BaseManager()
{
  /* Setup the event queue and callback tables */
  mgmt_event_queue    = create_queue();
  mgmt_callback_table = ink_hash_table_create(InkHashTableKeyType_Word);

} /* End BaseManager::BaseManager */

BaseManager::~BaseManager()
{
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  while (!queue_is_empty(mgmt_event_queue)) {
    MgmtMessageHdr *mh = (MgmtMessageHdr *)dequeue(mgmt_event_queue);
    ats_free(mh);
  }
  ats_free(mgmt_event_queue);

  for (entry = ink_hash_table_iterator_first(mgmt_callback_table, &iterator_state); entry != NULL;
       entry = ink_hash_table_iterator_next(mgmt_callback_table, &iterator_state)) {
    MgmtCallbackList *tmp, *cb_list = (MgmtCallbackList *)entry;

    for (tmp = cb_list->next; tmp; tmp = cb_list->next) {
      ats_free(cb_list);
      cb_list = tmp;
    }
    ats_free(cb_list);
  }

  return;
} /* End BaseManager::~BaseManager */

/*
 * registerMgmtCallback(...)
 *   Function to register callback's for various management events, such
 * as shutdown, re-init, etc. The following callbacks should be
 * registered:
 *                MGMT_EVENT_SHUTDOWN  (graceful shutdown)
 *                MGMT_EVENT_RESTART   (graceful reboot)
 *                ...
 *
 *   Returns:   -1      on error(invalid event id passed in)
 *               or     value
 */
int
BaseManager::registerMgmtCallback(int msg_id, MgmtCallback cb, void *opaque_cb_data)
{
  MgmtCallbackList *cb_list;
  InkHashTableValue hash_value;

  if (ink_hash_table_lookup(mgmt_callback_table, (InkHashTableKey)(intptr_t)msg_id, &hash_value) != 0) {
    cb_list = (MgmtCallbackList *)hash_value;
  } else {
    cb_list = NULL;
  }

  if (cb_list) {
    MgmtCallbackList *tmp;

    for (tmp = cb_list; tmp->next; tmp = tmp->next)
      ;
    tmp->next              = (MgmtCallbackList *)ats_malloc(sizeof(MgmtCallbackList));
    tmp->next->func        = cb;
    tmp->next->opaque_data = opaque_cb_data;
    tmp->next->next        = NULL;
  } else {
    cb_list              = (MgmtCallbackList *)ats_malloc(sizeof(MgmtCallbackList));
    cb_list->func        = cb;
    cb_list->opaque_data = opaque_cb_data;
    cb_list->next        = NULL;
    ink_hash_table_insert(mgmt_callback_table, (InkHashTableKey)(intptr_t)msg_id, cb_list);
  }
  return msg_id;
} /* End BaseManager::registerMgmtCallback */

/*
 * signalMgmtEntity(...)
 */
int
BaseManager::signalMgmtEntity(int msg_id, char *data_str)
{
  if (data_str) {
    return signalMgmtEntity(msg_id, data_str, strlen(data_str) + 1);
  } else {
    return signalMgmtEntity(msg_id, NULL, 0);
  }

} /* End BaseManager::signalMgmtEntity */

/*
 * signalMgmtEntity(...)
 */
int
BaseManager::signalMgmtEntity(int msg_id, char *data_raw, int data_len)
{
  MgmtMessageHdr *mh;

  if (data_raw) {
    mh           = (MgmtMessageHdr *)ats_malloc(sizeof(MgmtMessageHdr) + data_len);
    mh->msg_id   = msg_id;
    mh->data_len = data_len;
    memcpy((char *)mh + sizeof(MgmtMessageHdr), data_raw, data_len);
  } else {
    mh           = (MgmtMessageHdr *)ats_malloc(sizeof(MgmtMessageHdr));
    mh->msg_id   = msg_id;
    mh->data_len = 0;
  }
  ink_assert(enqueue(mgmt_event_queue, mh));
  return msg_id;

} /* End BaseManager::signalMgmtEntity */

void
BaseManager::executeMgmtCallback(int msg_id, char *data_raw, int data_len)
{
  InkHashTableValue hash_value;
  if (ink_hash_table_lookup(mgmt_callback_table, (InkHashTableKey)(intptr_t)msg_id, &hash_value) != 0) {
    for (MgmtCallbackList *cb_list = (MgmtCallbackList *)hash_value; cb_list; cb_list = cb_list->next) {
      (*((MgmtCallback)(cb_list->func)))(cb_list->opaque_data, data_raw, data_len);
    }
  }
}
