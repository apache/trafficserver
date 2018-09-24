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

   HttpPages.h

   Description:
       Data structurs and stat page generators for http info


 ****************************************************************************/

#pragma once

#include "tscore/ink_platform.h"
#include "P_EventSystem.h"
#include "tscore/DynArray.h"
#include "HTTP.h"
#include "StatPages.h"
#include "HttpSM.h"

class HttpSM;

const int HTTP_LIST_BUCKETS = 63;
const int HTTP_LIST_RETRY   = HRTIME_MSECONDS(10);

struct HttpSMListBucket {
  Ptr<ProxyMutex> mutex;
  DList(HttpSM, debug_link) sm_list;
};

extern HttpSMListBucket HttpSMList[];

class HttpPagesHandler : public BaseStatPagesHandler
{
public:
  HttpPagesHandler(Continuation *cont, HTTPHdr *header);
  ~HttpPagesHandler() override;

  int handle_smlist(int event, void *edata);
  int handle_smdetails(int event, void *edata);
  int handle_callback(int event, void *edata);
  Action action;

private:
  int64_t extract_id(const char *query);
  void dump_hdr(HTTPHdr *hdr, const char *desc);
  void dump_tunnel_info(HttpSM *sm);
  void dump_history(HttpSM *sm);
  int dump_sm(HttpSM *sm);

  Arena arena;
  char *request;
  int list_bucket;

  enum HP_State_t {
    HP_INIT,
    HP_RUN,
  };
  HP_State_t state;

  // Info for SM details
  int64_t sm_id;
};

void http_pages_init();
