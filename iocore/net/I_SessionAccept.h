/** @file

  SessionAccept

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

#include "I_Net.h"
#include "I_VConnection.h"

struct AclRecord;
struct HttpProxyPort;
/**
   The base class SessionAccept can not be used directly. The inherited class of
   SessionAccept (ex. HttpSessionAccept) is designed to:

     - Check IPAllow policy.
     - Create ClientSession.
     - Pass NetVC and MIOBuffer by call ClientSession::new_connection().

   nullptr mutex:

     - One specific protocol has ONLY one inherited class of SessionAccept.
     - The object of this class is shared by all incoming request / NetVC that
       identified as the protocol by ProtocolSessionProbe.
     - The inherited class of SessionAccept is non-blocking to allow parallel accepts/

   To implement a inherited class of SessionAccept:

     - No state is recorded by the handler.
     - Values are required to be set during construction and never changed.
     - Can not put into EventSystem.

   So a nullptr mutex is safe for the continuation.
*/

class SessionAccept : public Continuation
{
public:
  SessionAccept(ProxyMutex *amutex) : Continuation(amutex) { SET_HANDLER(&SessionAccept::mainEvent); }
  ~SessionAccept() {}
  /**
    Accept a new connection on this session.

    If the session accepts the connection by returning true, it
    takes ownership of all the arguments.

    If the session rejects the connection by returning false, the
    arguments are unmodified and the caller retains ownership.
    Typically in this case, the caller would simply destroy all the
    arguments.

   */
  virtual bool accept(NetVConnection *, MIOBuffer *, IOBufferReader *) = 0;
  /// The proxy port on which this session arrived.
  HttpProxyPort *proxyPort = nullptr;

private:
  virtual int mainEvent(int event, void *netvc) = 0;
};
