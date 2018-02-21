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

#pragma once

#include "P_QUICNetVConnection.h"

class QUICClosedConCollector : public Continuation
{
public:
  QUICClosedConCollector();

  int mainEvent(int event, Event *e);

  ASLL(QUICNetVConnection, closed_alink) closedQueue;

private:
  Que(QUICNetVConnection, closed_link) _localClosedQueue;

  void _process_closed_connection(EThread *t);
};
