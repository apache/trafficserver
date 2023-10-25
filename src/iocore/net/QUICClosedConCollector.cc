/** @file
  This file implements an I/O Processor for network I/O
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

#include "P_QUICClosedConCollector.h"

QUICClosedConCollector::QUICClosedConCollector()
{
  SET_HANDLER(&QUICClosedConCollector::mainEvent);
}

int
QUICClosedConCollector::mainEvent(int event, Event *e)
{
  EThread *t = this->mutex->thread_holding;
  ink_assert(t == this_thread());

  this->_process_closed_connection(t);
  return 0;
}

void
QUICClosedConCollector::_process_closed_connection(EThread *t)
{
  ink_release_assert(t != nullptr);

  QUICNetVConnection *qvc;
  Que(QUICNetVConnection, closed_link) local_queue;

  while ((qvc = this->_localClosedQueue.pop())) {
    if (qvc->shouldDestroy()) {
      qvc->destroy(t);
    } else {
      local_queue.push(qvc);
    }
  }

  SList(QUICNetVConnection, closed_alink) aq(this->closedQueue.popall());
  while ((qvc = aq.pop())) {
    qvc->remove_connection_ids();
    if (qvc->shouldDestroy()) {
      qvc->destroy(t);
    } else {
      local_queue.push(qvc);
    }
  }

  this->_localClosedQueue.append(local_queue);
}
