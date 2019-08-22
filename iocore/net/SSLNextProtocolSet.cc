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

#include "tscore/ink_config.h"
#include "ts/apidefs.h"
#include "tscore/ink_platform.h"
#include "P_SSLNextProtocolSet.h"
#include "tscpp/util/TextView.h"

// For currently defined protocol strings, see
// http://technotes.googlecode.com/git/nextprotoneg.html. The OpenSSL
// documentation tells us to return a string in "wire format". The
// draft NPN RFC helpfully refuses to document the wire format. The
// above link says we need to send length-prefixed strings, but does
// not say how many bytes the length is. For the record, it's 1.

unsigned char *
append_protocol(const char *proto, unsigned char *buf)
{
  size_t sz = strlen(proto);
  *buf++    = static_cast<unsigned char>(sz);
  memcpy(buf, proto, sz);
  return buf + sz;
}

bool
SSLNextProtocolSet::create_npn_advertisement(const SessionProtocolSet &enabled, unsigned char **npn, size_t *len) const
{
  const SSLNextProtocolSet::NextProtocolEndpoint *ep;
  unsigned char *advertised;

  if (*npn) {
    ats_free(*npn);
    *npn = nullptr;
  }
  *len = 0;

  for (ep = endpoints.head; ep != nullptr; ep = endpoints.next(ep)) {
    ink_release_assert((strlen(ep->protocol) > 0));
    *len += (strlen(ep->protocol) + 1);
  }

  *npn = advertised = static_cast<unsigned char *>(ats_malloc(*len));
  if (!(*npn)) {
    goto fail;
  }

  for (ep = endpoints.head; ep != nullptr; ep = endpoints.next(ep)) {
    if (enabled.contains(globalSessionProtocolNameRegistry.toIndex(ts::TextView{ep->protocol, strlen(ep->protocol)}))) {
      Debug("ssl", "advertising protocol %s, %p", ep->protocol, ep->endpoint);
      advertised = append_protocol(ep->protocol, advertised);
    }
  }

  return true;

fail:
  ats_free(*npn);
  *npn = nullptr;
  *len = 0;
  return false;
}

bool
SSLNextProtocolSet::registerEndpoint(const char *proto, Continuation *ep)
{
  size_t len = strlen(proto);

  // Both ALPN and NPN only allow 255 bytes of protocol name.
  if (len > 255) {
    return false;
  }

  if (!findEndpoint(reinterpret_cast<const unsigned char *>(proto), len)) {
    this->endpoints.push(new NextProtocolEndpoint(proto, ep));
    return true;
  }

  return false;
}

Continuation *
SSLNextProtocolSet::findEndpoint(const unsigned char *proto, unsigned len) const
{
  for (const NextProtocolEndpoint *ep = this->endpoints.head; ep != nullptr; ep = this->endpoints.next(ep)) {
    size_t sz = strlen(ep->protocol);
    if (sz == len && memcmp(ep->protocol, proto, len) == 0) {
      return ep->endpoint;
    }
  }
  return nullptr;
}

SSLNextProtocolSet::SSLNextProtocolSet() {}

SSLNextProtocolSet::~SSLNextProtocolSet()
{
  for (NextProtocolEndpoint *ep; (ep = this->endpoints.pop());) {
    delete ep;
  }
}

SSLNextProtocolSet::NextProtocolEndpoint::NextProtocolEndpoint(const char *_proto, Continuation *_ep)
  : protocol(_proto), endpoint(_ep)
{
}

SSLNextProtocolSet::NextProtocolEndpoint::~NextProtocolEndpoint() {}
