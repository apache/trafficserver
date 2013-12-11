/** @file

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

#include "ink_config.h"
#include "P_Net.h"
#include "P_ProtocolNetAccept.h"

UnixNetVConnection *
ProtocolNetAccept::createSuitableVC(EThread *t, Connection &con)
{
  UnixNetVConnection *vc;

  if (etype == SSLNetProcessor::ET_SSL && etype) {
    // SSL protocol
    if (t)
      vc = (UnixNetVConnection *)THREAD_ALLOC(sslNetVCAllocator, t);
    else
      vc = (UnixNetVConnection *)sslNetVCAllocator.alloc();
    vc->proto_stack = (1u << TS_PROTO_TLS);
  } else {
    if (t)
      vc = THREAD_ALLOC(netVCAllocator, t);
    else
      vc = netVCAllocator.alloc();

#if TS_HAS_SPDY
    vc->probe_state = SPDY_PROBE_STATE_BEGIN;
#else
    vc->probe_state = SPDY_PROBE_STATE_NONE;
#endif

    //
    // Protocol stack may be changed by
    // following call of SpdyProbe()
    //
    vc->proto_stack = (1u << TS_PROTO_HTTP);
  }

  vc->con = con;
  return vc;
}

NetAccept *
ProtocolNetAccept::clone()
{
  NetAccept *na;
  na = NEW(new ProtocolNetAccept);
  *na = *this;
  return na;
}
