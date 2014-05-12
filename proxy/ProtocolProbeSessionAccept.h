/** @file

  ProtocolProbeSessionAccept

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

#ifndef ProtocolProbeSessionAccept_H_
#define ProtocolProbeSessionAccept_H_

#include "I_SessionAccept.h"

class ProtocolProbeSessionAccept: public SessionAccept
{
public:
  ProtocolProbeSessionAccept(): SessionAccept(NULL)
  {
    memset(endpoint, 0, sizeof(endpoint));
    SET_HANDLER(&ProtocolProbeSessionAccept::mainEvent);
  }
  ~ProtocolProbeSessionAccept() {}

  void registerEndpoint(TSProtoType proto_type, SessionAccept * ap);

  void accept(NetVConnection *, MIOBuffer *, IOBufferReader*);

private:
  int mainEvent(int event, void * netvc);
  ProtocolProbeSessionAccept(const ProtocolProbeSessionAccept &); // disabled
  ProtocolProbeSessionAccept& operator =(const ProtocolProbeSessionAccept&); // disabled

  SessionAccept * endpoint[sizeof(TSClientProtoStack) * CHAR_BIT];

friend struct ProtocolProbeTrampoline;
};

#endif /* ProtocolProbeSessionAccept_H_ */
