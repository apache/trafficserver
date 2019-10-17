/** @file

  ALPNSupport implements common methods and members to
  support protocols for ALPN negotiation

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
#include "records/I_RecHttp.h"

class SSLNextProtocolSet;
class SSLNextProtocolAccept;
class Continuation;

class ALPNSupport
{
public:
  void registerNextProtocolSet(SSLNextProtocolSet *, const SessionProtocolSet &protos);
  void disableProtocol(int idx);
  void enableProtocol(int idx);
  void clear();
  bool setSelectedProtocol(const unsigned char *proto, unsigned int len);

  Continuation *
  endpoint() const
  {
    return npnEndpoint;
  }

  bool
  getNPN(const unsigned char **out, unsigned int *outlen) const
  {
    if (this->npn && this->npnsz) {
      *out    = this->npn;
      *outlen = this->npnsz;
      return true;
    }
    return false;
  }

  const SSLNextProtocolSet *
  getNextProtocolSet() const
  {
    return npnSet;
  }

private:
  const SSLNextProtocolSet *npnSet = nullptr;
  SessionProtocolSet protoenabled;
  // Local copies of the npn strings
  unsigned char *npn        = nullptr;
  size_t npnsz              = 0;
  Continuation *npnEndpoint = nullptr;
};
