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

#pragma once

#include "I_SessionAccept.h"

struct ProtocolProbeSessionAcceptEnums {
  /// Enumeration for related groups of protocols.
  /// There is a child acceptor for each group which
  /// handles finer grained stuff.
  enum ProtoGroupKey {
    PROTO_HTTP,    ///< HTTP group (0.9-1.1)
    PROTO_HTTP2,   ///< HTTP 2 group
    N_PROTO_GROUPS ///< Size value.
  };
};

class ProtocolProbeSessionAccept : public SessionAccept, public ProtocolProbeSessionAcceptEnums
{
public:
  ProtocolProbeSessionAccept() : SessionAccept(nullptr)
  {
    memset(endpoint, 0, sizeof(endpoint));
    SET_HANDLER(&ProtocolProbeSessionAccept::mainEvent);
  }
  ~ProtocolProbeSessionAccept() override {}
  void registerEndpoint(ProtoGroupKey key, SessionAccept *ap);

  bool accept(NetVConnection *, MIOBuffer *, IOBufferReader *) override;

  // noncopyable
  ProtocolProbeSessionAccept(const ProtocolProbeSessionAccept &) = delete;            // disabled
  ProtocolProbeSessionAccept &operator=(const ProtocolProbeSessionAccept &) = delete; // disabled

  IpMap *proxy_protocol_ipmap = nullptr;

private:
  int mainEvent(int event, void *netvc) override;

  /** Child acceptors, index by @c ProtoGroupKey

      We pass on the actual accept to one of these after doing protocol sniffing.
      We make it one larger and leave the last entry NULL so we don't have to
      do range checks on the enum value.
   */
  SessionAccept *endpoint[N_PROTO_GROUPS + 1];

  friend struct ProtocolProbeTrampoline;
};
