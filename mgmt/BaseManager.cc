/** @file

  Member function definitions for Base Manager class.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
  See the NOTICE file distributed with this work for additional information regarding copyright
  ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance with the License.  You may obtain a
  copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software distributed under the License
  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
  or implied. See the License for the specific language governing permissions and limitations under
  the License.
 */

#include "tscore/ink_memory.h"
#include "tscore/ink_mutex.h"
#include "BaseManager.h"

BaseManager::BaseManager()
{
  ink_sem_init(&q_sem, 0);
}

BaseManager::~BaseManager()
{
  while (!queue.empty()) {
    ats_free(queue.front());
    queue.pop();
  }
}

void
BaseManager::enqueue(MgmtMessageHdr *mh)
{
  std::lock_guard lock(q_mutex);
  queue.emplace(mh);
  ink_sem_post(&q_sem);
}

bool
BaseManager::queue_empty()
{
  std::lock_guard lock(q_mutex);
  return queue.empty();
}

MgmtMessageHdr *
BaseManager::dequeue()
{
  MgmtMessageHdr *msg{nullptr};

  ink_sem_wait(&q_sem);
  {
    std::lock_guard lock(q_mutex);
    msg = queue.front();
    queue.pop();
  }
  return msg;
}

int
BaseManager::registerMgmtCallback(int msg_id, MgmtCallback const &cb)
{
  auto &cb_list{mgmt_callback_table[msg_id]};
  cb_list.emplace_back(cb);
  return msg_id;
}

void
BaseManager::executeMgmtCallback(int msg_id, ts::MemSpan span)
{
  if (auto it = mgmt_callback_table.find(msg_id); it != mgmt_callback_table.end()) {
    for (auto &&cb : it->second) {
      cb(span);
    }
  }
}
