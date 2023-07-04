/** @file

  ALPNSupport.cc provides implementations for ALPNSupport methods

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

#include "P_ALPNSupport.h"
#include "P_SSLNextProtocolSet.h"
#include "records/I_RecHttp.h"

int ALPNSupport::_ex_data_index = -1;

void
ALPNSupport::initialize()
{
  ink_assert(_ex_data_index == -1);
  if (_ex_data_index == -1) {
    _ex_data_index = SSL_get_ex_new_index(0, (void *)"ALPNSupport index", nullptr, nullptr, nullptr);
  }
}

ALPNSupport *
ALPNSupport::getInstance(SSL *ssl)
{
  return static_cast<ALPNSupport *>(SSL_get_ex_data(ssl, _ex_data_index));
}

void
ALPNSupport::bind(SSL *ssl, ALPNSupport *alpns)
{
  SSL_set_ex_data(ssl, _ex_data_index, alpns);
}

void
ALPNSupport::unbind(SSL *ssl)
{
  SSL_set_ex_data(ssl, _ex_data_index, nullptr);
}

void
ALPNSupport::clear()
{
  ats_free(npn);
  npn   = nullptr;
  npnsz = 0;

  npnSet      = nullptr;
  npnEndpoint = nullptr;
}

bool
ALPNSupport::setSelectedProtocol(const unsigned char *proto, unsigned int len)
{
  // If there's no NPN set, we should not have done this negotiation.
  ink_assert(this->npnSet != nullptr);

  this->npnEndpoint = this->npnSet->findEndpoint(proto, static_cast<unsigned>(len));
  this->npnSet      = nullptr;

  if (this->npnEndpoint == nullptr) {
    Error("failed to find registered SSL endpoint for '%.*s'", len, proto);
    return false;
  }
  return true;
}

int
ALPNSupport::advertise_next_protocol(SSL *ssl, const unsigned char **out, unsigned *outlen)
{
  if (this->getNPN(out, outlen)) {
    // Successful return tells OpenSSL to advertise.
    return SSL_TLSEXT_ERR_OK;
  }
  return SSL_TLSEXT_ERR_NOACK;
}

int
ALPNSupport::select_next_protocol(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in,
                                  unsigned inlen)
{
  const unsigned char *npnptr = nullptr;
  unsigned int npnsize        = 0;
  int retval                  = SSL_TLSEXT_ERR_ALERT_FATAL;

  if (this->getNPN(&npnptr, &npnsize) && npnsize > 0) {
    // SSL_select_next_proto chooses the first server-offered protocol that appears in the clients protocol set, ie. the
    // server selects the protocol. This is a n^2 search, so it's preferable to keep the protocol set short.
    if (SSL_select_next_proto(const_cast<unsigned char **>(out), outlen, npnptr, npnsize, in, inlen) == OPENSSL_NPN_NEGOTIATED) {
      Debug("ssl", "selected ALPN protocol %.*s", (int)(*outlen), *out);
      retval = SSL_TLSEXT_ERR_OK;
    } else {
      *out    = nullptr;
      *outlen = 0;
    }
  } else {
    *out    = nullptr;
    *outlen = 0;
    retval  = SSL_TLSEXT_ERR_NOACK;
  }
  return retval;
}

void
ALPNSupport::disableProtocol(int idx)
{
  this->protoenabled.markOut(idx);
  // Update the npn string
  if (npnSet) {
    npnSet->create_npn_advertisement(protoenabled, &npn, &npnsz);
  }
}

void
ALPNSupport::enableProtocol(int idx)
{
  this->protoenabled.markIn(idx);
  // Update the npn string
  if (npnSet) {
    npnSet->create_npn_advertisement(protoenabled, &npn, &npnsz);
  }
}

void
ALPNSupport::registerNextProtocolSet(SSLNextProtocolSet *s, const SessionProtocolSet &protos)
{
  this->protoenabled = protos;
  this->npnSet       = s;
  npnSet->create_npn_advertisement(protoenabled, &npn, &npnsz);
}
