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

   HttpUpdateSM.h

   Description:
        An HttpSM sub class for support scheduled update functionality



 ****************************************************************************/

#pragma once

#include "P_EventSystem.h"
#include "HttpSM.h"

#define HTTP_SCH_UPDATE_EVENT_WRITTEN (HTTP_SCH_UPDATE_EVENTS_START + 1)
#define HTTP_SCH_UPDATE_EVENT_UPDATED (HTTP_SCH_UPDATE_EVENTS_START + 2)
#define HTTP_SCH_UPDATE_EVENT_DELETED (HTTP_SCH_UPDATE_EVENTS_START + 3)
#define HTTP_SCH_UPDATE_EVENT_NOT_CACHED (HTTP_SCH_UPDATE_EVENTS_START + 4)
#define HTTP_SCH_UPDATE_EVENT_ERROR (HTTP_SCH_UPDATE_EVENTS_START + 5)
#define HTTP_SCH_UPDATE_EVENT_NO_ACTION (HTTP_SCH_UPDATE_EVENTS_START + 6)

class HttpUpdateSM : public HttpSM
{
public:
  HttpUpdateSM();

  static HttpUpdateSM *allocate();
  void destroy();

  Action *start_scheduled_update(Continuation *cont, HTTPHdr *req);

  //  private:
  bool cb_occured;
  Continuation *cb_cont;
  Action cb_action;
  int cb_event;

protected:
  void handle_api_return();
  void set_next_state();
  int kill_this_async_hook(int event, void *data);
};

inline HttpUpdateSM *
HttpUpdateSM::allocate()
{
  extern ClassAllocator<HttpUpdateSM> httpUpdateSMAllocator;
  return httpUpdateSMAllocator.alloc();
}

// Regression/Testing Routing
void init_http_update_test();
