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

#include "P_QUICNetVConnection.h"
#include "P_QUICClosedConCollector.h"
#include "I_UDPNet.h"

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

  QUICConnection *qc;
  Que(QUICConnection, closed_link) local_queue;

  while ((qc = this->_localClosedQueue.pop())) {
    QUICNetVConnection *qvc = static_cast<QUICNetVConnection *>(qc);
    if (qvc->shouldDestroy()) {
      qvc->destroy(t);
    } else {
      local_queue.push(qc);
    }
  }

  SList(QUICConnection, closed_alink) aq(this->closedQueue.popall());
  while ((qc = aq.pop())) {
    QUICNetVConnection *qvc = static_cast<QUICNetVConnection *>(qc);
    qvc->cleanup_connection();
    if (qvc->shouldDestroy()) {
      qvc->destroy(t);
    } else {
      local_queue.push(qc);
    }
  }

  this->_localClosedQueue.append(local_queue);
}
