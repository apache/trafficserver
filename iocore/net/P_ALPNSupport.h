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
#include <openssl/ssl.h>

class SSLNextProtocolSet;
class SSLNextProtocolAccept;
class Continuation;

class ALPNSupport
{
public:
  virtual ~ALPNSupport() = default;

  static void initialize();
  static ALPNSupport *getInstance(SSL *ssl);
  static void bind(SSL *ssl, ALPNSupport *alpns);
  static void unbind(SSL *ssl);

  void registerNextProtocolSet(SSLNextProtocolSet *, const SessionProtocolSet &protos);
  void disableProtocol(int idx);
  void enableProtocol(int idx);
  void clear();
  bool setSelectedProtocol(const unsigned char *proto, unsigned int len);

  int advertise_next_protocol(SSL *ssl, const unsigned char **out, unsigned *outlen);
  int select_next_protocol(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in, unsigned inlen);

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

  void set_negotiated_protocol_id(const ts::TextView &proto);
  int get_negotiated_protocol_id() const;

private:
  static int _ex_data_index;

  const SSLNextProtocolSet *npnSet = nullptr;
  SessionProtocolSet protoenabled;
  // Local copies of the npn strings
  unsigned char *npn        = nullptr;
  size_t npnsz              = 0;
  Continuation *npnEndpoint = nullptr;
  int _negotiated_proto_id  = SessionProtocolNameRegistry::INVALID;
};

//
// Inline functions
//

inline void
ALPNSupport::set_negotiated_protocol_id(const ts::TextView &proto)
{
  _negotiated_proto_id = globalSessionProtocolNameRegistry.indexFor(proto);
}

inline int
ALPNSupport::get_negotiated_protocol_id() const
{
  return _negotiated_proto_id;
}
