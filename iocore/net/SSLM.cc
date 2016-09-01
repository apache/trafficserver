/** @file

  A brief file description

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

#include "I_SSLM.h"
#include "P_SSLUtils.h"

SSLM::SSLM()
  : ssl(NULL),
    hookOpRequested(SSL_HOOK_OP_DEFAULT),
    transparentPassThrough(false),
    sslHandShakeComplete(false),
    sslClientRenegotiationAbort(false),
    sslSessionCacheHit(false),
    npnSet(NULL),
    npnEndpoint(NULL),
    error_code(0)
{
}

SSLM::~SSLM()
{
  return;
}

void
SSLM::clear()
{
  if (ssl != NULL) {
    SSL_free(ssl);
    ssl = NULL;
  }

  sslHandShakeComplete        = false;
  transparentPassThrough      = false;
  hookOpRequested             = SSL_HOOK_OP_DEFAULT;
  sslClientRenegotiationAbort = false;
  sslSessionCacheHit          = false;
  npnSet                      = NULL;
  npnEndpoint                 = NULL;
  error_code                  = 0;
}

void
SSLM::registerNextProtocolSet(const SSLNextProtocolSet *s)
{
  ink_release_assert(this->npnSet == NULL);
  this->npnSet = s;
}

// NextProtocolNegotiation TLS extension callback. The NPN extension
// allows the client to select a preferred protocol, so all we have
// to do here is tell them what out protocol set is.
int
SSLM::advertise_next_protocol(SSL *ssl, const unsigned char **out, unsigned int *outlen, void * /*arg ATS_UNUSED */)
{
  NetProfileSM *profile_sm = (NetProfileSM *)SSLProfileSMAccess(ssl);
  SSLM *sslm               = dynamic_cast<SSLM *>(profile_sm);

  ink_release_assert(sslm != NULL);

  if (sslm->npnSet && sslm->npnSet->advertiseProtocols(out, outlen)) {
    // Successful return tells OpenSSL to advertise.
    return SSL_TLSEXT_ERR_OK;
  }

  return SSL_TLSEXT_ERR_NOACK;
}

// ALPN TLS extension callback. Given the client's set of offered
// protocols, we have to select a protocol to use for this session.
int
SSLM::select_next_protocol(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in ATS_UNUSED,
                           unsigned inlen ATS_UNUSED, void *)
{
  NetProfileSM *profile_sm = (NetProfileSM *)SSLProfileSMAccess(ssl);
  SSLM *sslm               = dynamic_cast<SSLM *>(profile_sm);
  const unsigned char *npn = NULL;
  unsigned npnsz           = 0;

  ink_release_assert(sslm != NULL);

  if (sslm->npnSet && sslm->npnSet->advertiseProtocols(&npn, &npnsz)) {
// SSL_select_next_proto chooses the first server-offered protocol that appears in the clients protocol set, ie. the
// server selects the protocol. This is a n^2 search, so it's preferable to keep the protocol set short.

#if HAVE_SSL_SELECT_NEXT_PROTO
    if (SSL_select_next_proto((unsigned char **)out, outlen, npn, npnsz, in, inlen) == OPENSSL_NPN_NEGOTIATED) {
      Debug("ssl", "selected ALPN protocol %.*s", (int)(*outlen), *out);
      return SSL_TLSEXT_ERR_OK;
    }
#endif /* HAVE_SSL_SELECT_NEXT_PROTO */
  }

  *out    = NULL;
  *outlen = 0;
  return SSL_TLSEXT_ERR_NOACK;
}
