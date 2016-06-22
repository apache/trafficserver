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

//-------------------------------------------------------------------------
// include files
//-------------------------------------------------------------------------

#include "ts/ink_platform.h"
#include "P_EventSystem.h"

#include "Log.h"
#include "LogCollationAccept.h"
#include "LogCollationHostSM.h"

//-------------------------------------------------------------------------
// LogCollationAccept::LogCollationAccept
//-------------------------------------------------------------------------

LogCollationAccept::LogCollationAccept(int port) : Continuation(new_ProxyMutex()), m_port(port), m_pending_event(NULL)
{
  NetProcessor::AcceptOptions opt;
  SET_HANDLER((LogCollationAcceptHandler)&LogCollationAccept::accept_event);
  // work around for iocore problem where _pre_fetch_buffer can get
  // appended to itself if multiple do_io_reads are called requesting
  // small amounts of data.  Most arguments are default except for the
  // last one which we will set to true.
  // [amc] That argument is ignored so I dropped it.
  opt.local_port     = m_port;
  opt.ip_family      = AF_INET;
  opt.accept_threads = 0;
  m_accept_action    = netProcessor.accept(this, opt);
  ink_assert(NULL != m_accept_action);
}

//-------------------------------------------------------------------------
// LogCollationAccept::~LogCollationAccept
//-------------------------------------------------------------------------

LogCollationAccept::~LogCollationAccept()
{
  Debug("log-collation", "LogCollationAccept::~LogCollationAccept");

  // stop the netProcessor
  if (m_accept_action) {
    m_accept_action->cancel();
    m_accept_action = NULL;

    Debug("log-collation", "closing Log::collation_accept_file_descriptor "
                           "(%d)",
          Log::collation_accept_file_descriptor);
    if (::close(Log::collation_accept_file_descriptor) < 0) {
      Error("error closing collate listen file descriptor [%d]: %s", Log::collation_accept_file_descriptor, strerror(errno));
    } else {
      Log::collation_accept_file_descriptor = NO_FD;
    }
  } else {
    ink_assert(!"[ERROR] m_accept_action is NULL");
  }

  // stop the eventProcessor
  // ... but what if there's more than one pending?
  if (m_pending_event && (m_pending_event != ACTION_RESULT_DONE)) {
    m_pending_event->cancel();
  }
}

//-------------------------------------------------------------------------
// LogCollationAccept::accept_event
//-------------------------------------------------------------------------

int
LogCollationAccept::accept_event(int event, NetVConnection *net_vc)
{
  LogCollationHostSM *sm;

  switch (event) {
  case NET_EVENT_ACCEPT:
    sm = new LogCollationHostSM(net_vc);
    ink_assert(NULL != sm);
    break;

  default:
    ink_assert(!"[ERROR] Unexpected Event");
  }

  return EVENT_CONT;
}
