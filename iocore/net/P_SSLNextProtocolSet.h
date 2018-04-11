/** @file

  SSLNextProtocolSet

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

#include "ts/List.h"
#include "I_Net.h"

class Continuation;

class SSLNextProtocolSet
{
public:
  SSLNextProtocolSet();
  ~SSLNextProtocolSet();

  bool registerEndpoint(const char *, Continuation *);
  bool unregisterEndpoint(const char *, Continuation *);
  bool unregisterEndpoint(const char *proto);
  bool advertiseProtocols(const unsigned char **out, unsigned *len) const;
  SSLNextProtocolSet *clone() const;

  Continuation *findEndpoint(const unsigned char *, unsigned) const;

  struct NextProtocolEndpoint {
    // NOTE: the protocol and endpoint are NOT copied. The caller is
    // responsible for ensuring their lifetime.
    NextProtocolEndpoint(const char *protocol, Continuation *endpoint);
    ~NextProtocolEndpoint();

    const char *protocol;
    Continuation *endpoint;
    LINK(NextProtocolEndpoint, link);

    typedef DLL<NextProtocolEndpoint> list_type;
  };

  // noncopyable
  SSLNextProtocolSet(const SSLNextProtocolSet &) = delete;            // disabled
  SSLNextProtocolSet &operator=(const SSLNextProtocolSet &) = delete; // disabled

private:
  mutable unsigned char *npn;
  mutable size_t npnsz;

  NextProtocolEndpoint::list_type endpoints;
};
