/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "sslheaders.h"
#include "tscore/ink_memory.h"

#include <getopt.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

static void SslHdrExpand(SSL *, const SslHdrInstance::expansion_list &, TSMBuffer, TSMLoc);

static int
SslHdrExpandRequestHook(TSCont cont, TSEvent event, void *edata)
{
  const SslHdrInstance *hdr;
  TSHttpTxn txn;
  TSMBuffer mbuf;
  TSMLoc mhdr;

  txn                 = static_cast<TSHttpTxn>(edata);
  hdr                 = static_cast<const SslHdrInstance *>(TSContDataGet(cont));
  TSVConn vconn       = TSHttpSsnClientVConnGet(TSHttpTxnSsnGet(txn));
  TSSslConnection ssl = TSVConnSslConnectionGet(vconn);

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    if (TSHttpTxnClientReqGet(txn, &mbuf, &mhdr) != TS_SUCCESS) {
      goto done;
    }

    break;
  case TS_EVENT_HTTP_SEND_REQUEST_HDR:
    if (TSHttpTxnServerReqGet(txn, &mbuf, &mhdr) != TS_SUCCESS) {
      goto done;
    }

    // If we are only attaching to the client request, NULL the SSL context in order to
    // nuke the SSL headers from the server request.
    if (hdr->attach == SSL_HEADERS_ATTACH_CLIENT) {
      ssl = nullptr;
    }

    break;
  default:
    goto done;
  }

  SslHdrExpand(reinterpret_cast<SSL *>(ssl), hdr->expansions, mbuf, mhdr);
  TSHandleMLocRelease(mbuf, TS_NULL_MLOC, mhdr);

done:
  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return TS_EVENT_NONE;
}

static void
SslHdrRemoveHeader(TSMBuffer mbuf, TSMLoc mhdr, const std::string &name)
{
  TSMLoc field;
  TSMLoc next;

  field = TSMimeHdrFieldFind(mbuf, mhdr, name.c_str(), name.size());
  for (; field != TS_NULL_MLOC; field = next) {
    next = TSMimeHdrFieldNextDup(mbuf, mhdr, field);
    TSMimeHdrFieldDestroy(mbuf, mhdr, field);
    TSHandleMLocRelease(mbuf, mhdr, field);
  }
}

static void
SslHdrSetHeader(TSMBuffer mbuf, TSMLoc mhdr, const std::string &name, BIO *value)
{
  TSMLoc field;
  long vlen;
  char *vptr;

  vlen = BIO_get_mem_data(value, &vptr);

  SslHdrDebug("SSL header '%s'", name.c_str());

  field = TSMimeHdrFieldFind(mbuf, mhdr, name.c_str(), name.size());
  if (field == TS_NULL_MLOC) {
    TSMimeHdrFieldCreateNamed(mbuf, mhdr, name.c_str(), name.size(), &field);
    TSMimeHdrFieldValueStringSet(mbuf, mhdr, field, -1, vptr, vlen);
    TSMimeHdrFieldAppend(mbuf, mhdr, field);
    TSHandleMLocRelease(mbuf, mhdr, field);
  } else {
    TSMLoc next;

    // Overwrite the first value.
    TSMimeHdrFieldValueStringSet(mbuf, mhdr, field, -1, vptr, vlen);
    next = TSMimeHdrFieldNextDup(mbuf, mhdr, field);
    TSHandleMLocRelease(mbuf, mhdr, field);

    for (field = next; field != TS_NULL_MLOC; field = next) {
      next = TSMimeHdrFieldNextDup(mbuf, mhdr, field);
      TSMimeHdrFieldDestroy(mbuf, mhdr, field);
      TSHandleMLocRelease(mbuf, mhdr, field);
    }
  }
}

namespace
{
template <bool IsClient> class WrapX509
{
public:
  WrapX509(SSL *ssl) : _ssl(ssl), _x509(_nonNullInvalidValue()) {}

  X509 *
  get()
  {
    if (_x509 == _nonNullInvalidValue()) {
      _set();
    }

    return _x509;
  }

  ~WrapX509()
  {
    if (IsClient && (_x509 != _nonNullInvalidValue()) && (_x509 != nullptr)) {
      X509_free(_x509);
    }
  }

private:
  SSL *_ssl;
  X509 *_x509;

  // The address of this object can not be a valid X509 structure address.
  X509 *
  _nonNullInvalidValue() const
  {
    return reinterpret_cast<X509 *>(const_cast<WrapX509 *>(this));
  }

  void
  _set()
  {
    _x509 = (IsClient ? SSL_get_peer_certificate : SSL_get_certificate)(_ssl);
  }
};
} // end anonymous namespace

// Process SSL header expansions. If this is not an SSL connection, then we need to delete the SSL headers
// so that malicious clients cannot inject bogus information. Otherwise, we populate the header with the
// expanded value. If the value expands to something empty, we nuke the header.
static void
SslHdrExpand(SSL *ssl, const SslHdrInstance::expansion_list &expansions, TSMBuffer mbuf, TSMLoc mhdr)
{
  if (ssl == nullptr) {
    for (const auto &expansion : expansions) {
      SslHdrRemoveHeader(mbuf, mhdr, expansion.name);
    }
  } else {
    WrapX509<true> clientX509(ssl);
    WrapX509<false> serverX509(ssl);
    X509 *x509;

    BIO *exp = BIO_new(BIO_s_mem());

    for (const auto &expansion : expansions) {
      switch (expansion.scope) {
      case SSL_HEADERS_SCOPE_CLIENT:
        x509 = clientX509.get();
        if (x509 == nullptr) {
          SslHdrRemoveHeader(mbuf, mhdr, expansion.name);
          continue;
        }
        break;
      case SSL_HEADERS_SCOPE_SERVER:
        x509 = serverX509.get();
        if (x509 == nullptr) {
          continue;
        }
        break;
      default:
        continue;
      }

      SslHdrExpandX509Field(exp, x509, expansion.field);
      if (BIO_pending(exp)) {
        SslHdrSetHeader(mbuf, mhdr, expansion.name, exp);
      } else {
        SslHdrRemoveHeader(mbuf, mhdr, expansion.name);
      }
    }

    BIO_free(exp);
  }
}

static SslHdrInstance *
SslHdrParseOptions(int argc, const char **argv)
{
  static const struct option longopt[] = {
    {const_cast<char *>("attach"), required_argument, nullptr, 'a'},
    {nullptr, 0, nullptr, 0},
  };

  std::unique_ptr<SslHdrInstance> hdr(new SslHdrInstance());

  for (;;) {
    int opt;

    opt = getopt_long(argc, const_cast<char *const *>(argv), "", longopt, nullptr);
    switch (opt) {
    case 'a':
      if (strcmp(optarg, "client") == 0) {
        hdr->attach = SSL_HEADERS_ATTACH_CLIENT;
      } else if (strcmp(optarg, "server") == 0) {
        hdr->attach = SSL_HEADERS_ATTACH_SERVER;
      } else if (strcmp(optarg, "both") == 0) {
        hdr->attach = SSL_HEADERS_ATTACH_BOTH;
      } else {
        TSError("[%s] Invalid attach option '%s'", PLUGIN_NAME, optarg);
        return nullptr;
      }

      break;
    }

    if (opt == -1) {
      break;
    }
  }

  // Pick up the remaining options as SSL header expansions.
  hdr->expansions.resize(argc - optind);
  for (int i = optind; i < argc; ++i) {
    if (!SslHdrParseExpansion(argv[i], hdr->expansions[i - optind])) {
      // If we fail, the expansion parsing logs the error.
      return nullptr;
    }
  }

  return hdr.release();
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  SslHdrInstance *hdr;

  info.plugin_name   = (char *)"sslheaders";
  info.vendor_name   = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    SslHdrError("plugin registration failed");
  }

  hdr = SslHdrParseOptions(argc, static_cast<const char **>(argv));
  if (hdr) {
    switch (hdr->attach) {
    case SSL_HEADERS_ATTACH_SERVER:
      TSHttpHookAdd(TS_HTTP_SEND_REQUEST_HDR_HOOK, hdr->cont);
      break;
    case SSL_HEADERS_ATTACH_BOTH: /* fallthrough */
    case SSL_HEADERS_ATTACH_CLIENT:
      TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, hdr->cont);
      TSHttpHookAdd(TS_HTTP_SEND_REQUEST_HDR_HOOK, hdr->cont);
      break;
    }
  }
}

TSReturnCode
TSRemapInit(TSRemapInterface * /* api */, char * /* err */, int /* errsz */)
{
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **instance, char * /* err */, int /* errsz */)
{
  SslHdrInstance *hdr;

  hdr = SslHdrParseOptions(argc, (const char **)argv);
  if (hdr) {
    *instance = hdr;
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

void
TSRemapDeleteInstance(void *instance)
{
  SslHdrInstance *hdr = static_cast<SslHdrInstance *>(instance);
  delete hdr;
}

TSRemapStatus
TSRemapDoRemap(void *instance, TSHttpTxn txn, TSRemapRequestInfo * /* rri */)
{
  SslHdrInstance *hdr = static_cast<SslHdrInstance *>(instance);

  switch (hdr->attach) {
  case SSL_HEADERS_ATTACH_SERVER:
    TSHttpTxnHookAdd(txn, TS_HTTP_SEND_REQUEST_HDR_HOOK, hdr->cont);
    break;
  case SSL_HEADERS_ATTACH_BOTH: /* fallthrough */
  case SSL_HEADERS_ATTACH_CLIENT:
    TSHttpTxnHookAdd(txn, TS_HTTP_READ_REQUEST_HDR_HOOK, hdr->cont);
    TSHttpTxnHookAdd(txn, TS_HTTP_SEND_REQUEST_HDR_HOOK, hdr->cont);
    break;
  }

  return TSREMAP_NO_REMAP;
}

SslHdrInstance::SslHdrInstance() : expansions(), cont(TSContCreate(SslHdrExpandRequestHook, nullptr))
{
  TSContDataSet(cont, this);
}

SslHdrInstance::~SslHdrInstance()
{
  TSContDestroy(cont);
}
