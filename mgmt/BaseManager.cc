/** @file

  Member function definitions for Base Manager class.

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

#include "tscore/ink_memory.h"
#include "BaseManager.h"

BaseManager::BaseManager()
{
  /* Setup the event queue and callback tables */
  mgmt_event_queue = create_queue();

} /* End BaseManager::BaseManager */

BaseManager::~BaseManager()
{
  while (!queue_is_empty(mgmt_event_queue)) {
    MgmtMessageHdr *mh = (MgmtMessageHdr *)dequeue(mgmt_event_queue);
    ats_free(mh);
  }
  ats_free(mgmt_event_queue);

  for (auto &&it : mgmt_callback_table) {
    MgmtCallbackList *tmp, *cb_list = it.second;

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

  if (auto it = mgmt_callback_table.find(msg_id); it != mgmt_callback_table.end()) {
    cb_list = it->second;
  } else {
    cb_list = nullptr;
  }

  if (cb_list) {
    MgmtCallbackList *tmp;

    for (tmp = cb_list; tmp->next; tmp = tmp->next) {
      ;
    }
    tmp->next              = (MgmtCallbackList *)ats_malloc(sizeof(MgmtCallbackList));
    tmp->next->func        = cb;
    tmp->next->opaque_data = opaque_cb_data;
    tmp->next->next        = nullptr;
  } else {
    cb_list              = (MgmtCallbackList *)ats_malloc(sizeof(MgmtCallbackList));
    cb_list->func        = cb;
    cb_list->opaque_data = opaque_cb_data;
    cb_list->next        = nullptr;
    mgmt_callback_table.emplace(msg_id, cb_list);
  }
  return msg_id;
} /* End BaseManager::registerMgmtCallback */

void
BaseManager::executeMgmtCallback(int msg_id, char *data_raw, int data_len)
{
  if (auto it = mgmt_callback_table.find(msg_id); it != mgmt_callback_table.end()) {
    for (MgmtCallbackList *cb_list = it->second; cb_list; cb_list = cb_list->next) {
      (*((MgmtCallback)(cb_list->func)))(cb_list->opaque_data, data_raw, data_len);
    }
  }
}
