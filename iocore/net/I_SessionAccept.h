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

#ifndef I_SessionAccept_H_
#define I_SessionAccept_H_

#include "I_Net.h"
#include "I_VConnection.h"

struct AclRecord;

class SessionAccept : public Continuation
{
public:
  SessionAccept(ProxyMutex *amutex) : Continuation(amutex) { SET_HANDLER(&SessionAccept::mainEvent); }
  ~SessionAccept() {}
  virtual void accept(NetVConnection *, MIOBuffer *, IOBufferReader *) = 0;

  /* Returns NULL if the specified client_ip is not allowed by ip_allow
   * Returns a pointer to the relevant IP policy for later processing otherwise */
  static const AclRecord *testIpAllowPolicy(sockaddr const *client_ip);

private:
  virtual int mainEvent(int event, void *netvc) = 0;
};

#endif /* I_SessionAccept_H_ */
