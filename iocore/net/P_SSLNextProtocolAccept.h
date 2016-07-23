/** @file

  SSLNextProtocolAccept

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

#ifndef P_SSLNextProtocolAccept_H_
#define P_SSLNextProtocolAccept_H_

#include "P_Net.h"
#include "P_EventSystem.h"
#include "P_UnixNet.h"
#include "P_SSLNetVConnection.h"
#include "P_SSLNextProtocolSet.h"
#include "I_IOBuffer.h"

class SSLNextProtocolAccept : public SessionAccept
{
public:
  SSLNextProtocolAccept(Continuation *, bool);
  ~SSLNextProtocolAccept();

  bool accept(NetVConnection *, MIOBuffer *, IOBufferReader *);

  // Register handler as an endpoint for the specified protocol. Neither
  // handler nor protocol are copied, so the caller must guarantee their
  // lifetime is at least as long as that of the acceptor.
  bool registerEndpoint(const char *protocol, Continuation *handler);

  // Unregister the handler. Returns false if this protocol is not registered
  // or if it is not registered for the specified handler.
  bool unregisterEndpoint(const char *protocol, Continuation *handler);

  SLINK(SSLNextProtocolAccept, link);

private:
  int mainEvent(int event, void *netvc);
  SSLNextProtocolAccept(const SSLNextProtocolAccept &);            // disabled
  SSLNextProtocolAccept &operator=(const SSLNextProtocolAccept &); // disabled

  MIOBuffer *buffer; // XXX do we really need this?
  Continuation *endpoint;
  SSLNextProtocolSet protoset;
  bool transparent_passthrough;

  friend struct SSLNextProtocolTrampoline;
};

#endif /* P_SSLNextProtocolAccept_H_ */
