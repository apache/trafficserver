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

#if !defined (_HttpAccept_h_)
#define _HttpAccept_h_

#include "inktomi++.h"
#include "P_EventSystem.h"
#include "HttpConfig.h"
#include "HTTP.h"
#include "IPRange.h"


/**
   The continuation mutex is NULL to allow parellel accepts in NT. The
   only state used by the handler is attr and backdoor which is setup
   at the beginning of time and never changed. No state is recorded by
   the handler. So a NULL mutex is safe.

*/

class HttpAccept: public Continuation
{
public:
 HttpAccept(int aattr, bool abackdoor = false)
   : Continuation(NULL), backdoor(abackdoor), attr(aattr)
  {
    SET_HANDLER(&HttpAccept::mainEvent);
    return;
  }

  ~HttpAccept()
  {
    return;
  }

  int mainEvent(int event, void *netvc);

private:
    bool backdoor;
  int attr;
    HttpAccept(const HttpAccept &);
    HttpAccept & operator =(const HttpAccept &);
};


#endif
