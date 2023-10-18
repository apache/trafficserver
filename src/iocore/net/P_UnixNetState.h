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

/****************************************************************************

  UnixNetState.h


   NetState

   State information for a particular channel of a NetVConnection
   This information is private to the Net module.   It is only here
   because the C++ compiler needs it to define NetVConnection.


 ****************************************************************************/
#pragma once

#include "tscore/List.h"
#include "I_VIO.h"

class Event;
class NetEvent;

struct NetState {
  int enabled = 0;
  VIO vio;
  Link<NetEvent> ready_link;
  SLink<NetEvent> enable_link;
  int in_enabled_list = 0;
  int triggered       = 0;

  NetState() : vio(VIO::NONE) {}
};
