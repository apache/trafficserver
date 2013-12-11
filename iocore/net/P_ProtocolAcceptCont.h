/** @file

  ProtocolAcceptCont

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

#ifndef P_ProtocolAcceptCont_H_
#define P_ProtocolAcceptCont_H_

#include "I_AcceptCont.h"

class ProtocolAcceptCont: public AcceptCont
{
public:
  ProtocolAcceptCont(): AcceptCont(NULL)
  {
    memset(endpoint, 0, TS_PROTO_MAX * sizeof(AcceptCont *));
    SET_HANDLER(&ProtocolAcceptCont::mainEvent);
  }
  ~ProtocolAcceptCont() {}

  void *createNetAccept();
  void registerEndpoint(TSProtoType type, Continuation *ep);

private:
  int mainEvent(int event, void * netvc);
  ProtocolAcceptCont(const ProtocolAcceptCont &); // disabled
  ProtocolAcceptCont& operator =(const ProtocolAcceptCont&); // disabled

  Continuation *endpoint[TS_PROTO_MAX];
};

#endif /* P_ProtocolAcceptCont_H_ */
