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

  ICPProcessor.h


****************************************************************************/

#ifndef _ICPProcessor_h_
#define _ICPProcessor_h_
#include "P_EventSystem.h"
#include "URL.h"
#include "ICPevents.h"

//######################################################
// State definitions for "proxy.config.icp.enabled"
//######################################################
#define ICP_MODE_OFF 0
#define ICP_MODE_RECEIVE_ONLY 1
#define ICP_MODE_SEND_RECEIVE 2

extern void initialize_thread_for_icp(EThread *e);
class ICPProcessor;

//***************************************************************************
// External Interface to ICP
//   Calling Sequence:
//   =================
//      void     icpProcessor.start()
//        Returns:
//          None.
//
//      Action * icpProcessor.ICPQuery(Continuation *, URL *)
//        Returns:
//          Invokes continuation handleEvent(ICPreturn_t, struct sockaddr_in *)
//          where ICPreturn_t is ICP_LOOKUP_FOUND or ICP_LOOKUP_FAILED and
//          struct sockaddr_in (ip,port) is host containing URL data.
//***************************************************************************
class ICPProcessorExt
{
public:
  ICPProcessorExt(ICPProcessor *);
  ~ICPProcessorExt();

  // Exported interfaces
  void start();
  Action *ICPQuery(Continuation *, URL *);

private:
  ICPProcessor *_ICPpr;
};

extern ICPProcessorExt icpProcessor;

// End of ICPProcessor.h

#endif // _ICPProcessor_h_
