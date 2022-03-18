/** @file

  QUICNextProtocolAccept

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

#include "P_QUICNetVConnection.h"
#include "P_SSLNextProtocolSet.h"
#include "I_IOBuffer.h"

class QUICNextProtocolAccept : public SessionAccept
{
public:
  QUICNextProtocolAccept();
  ~QUICNextProtocolAccept();

  bool accept(NetVConnection *, MIOBuffer *, IOBufferReader *) override;

  // Register handler as an endpoint for the specified protocol. Neither
  // handler nor protocol are copied, so the caller must guarantee their
  // lifetime is at least as long as that of the acceptor.
  bool registerEndpoint(const char *protocol, Continuation *handler);

  void enableProtocols(const SessionProtocolSet &protos);

  SLINK(QUICNextProtocolAccept, link);
  SSLNextProtocolSet *getProtoSet();

  // noncopyable
  QUICNextProtocolAccept(const QUICNextProtocolAccept &) = delete;            // disabled
  QUICNextProtocolAccept &operator=(const QUICNextProtocolAccept &) = delete; // disabled

private:
  int mainEvent(int event, void *netvc) override;

  SSLNextProtocolSet protoset;
  SessionProtocolSet protoenabled;

  friend struct QUICNextProtocolTrampoline;
};
