/** @file

  ALPNSupport.cc provides implmentations for ALPNSupport methods

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

#include "tscore/ink_sock.h"
#include "tscore/ink_inet.h"
#include "I_EventSystem.h"

#include <memory>

class UDP2Connection;

struct UDP2Packet {
  UDP2Packet() = default;
  UDP2Packet(const IpEndpoint &from, const IpEndpoint &to, Ptr<IOBufferBlock> &chain) : from(from), to(to), chain(chain) {}
  UDP2Packet(sockaddr const *from, sockaddr *to, Ptr<IOBufferBlock> &chain) : chain(chain)
  {
    ats_ip_copy(&this->from, from);
    ats_ip_copy(&this->to, to);
  }

  ~UDP2Packet() { this->chain = nullptr; }
  IpEndpoint from{};
  IpEndpoint to{};
  Ptr<IOBufferBlock> chain;

  SLINK(UDP2Packet, in_link);
  SLINK(UDP2Packet, out_link);
  LINK(UDP2Packet, link);
};

using UDP2PacketUPtr = std::unique_ptr<UDP2Packet>;
