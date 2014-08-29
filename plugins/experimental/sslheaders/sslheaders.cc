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
#include <getopt.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <memory>

static void SslHdrExpand(SSL *, const SslHdrInstance::expansion_list&, TSMBuffer, TSMLoc);

static int
SslHdrExpandRequestHook(TSCont cont, TSEvent event, void * edata)
{
  const SslHdrInstance * hdr;
  TSHttpTxn txn;
  TSMBuffer mbuf;
  TSMLoc    mhdr;
  SSL *     ssl;

  txn = (TSHttpTxn)edata;
  hdr = (const SslHdrInstance *)TSContDataGet(cont);
  ssl = (SSL *)TSHttpSsnSSLConnectionGet(TSHttpTxnSsnGet(txn));

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
      ssl = NULL;
    }

    break;
  default:
    goto done;
  }

  SslHdrExpand(ssl, hdr->expansions, mbuf, mhdr);
  TSHandleMLocRelease(mbuf, TS_NULL_MLOC, mhdr);

done:
  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return TS_EVENT_NONE;
}

static void
SslHdrRemoveHeader(TSMBuffer mbuf, TSMLoc mhdr, const std::string& name)
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
SslHdrSetHeader(TSMBuffer mbuf, TSMLoc mhdr, const std::string& name, BIO * value)
{
  TSMLoc field;
  long vlen;
  char * vptr;

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

// Process SSL header expansions. If this is not an SSL connection, then we need to delete the SSL headers
// so that malicious clients cannot inject bogus information. Otherwise, we populate the header with the
// expanded value. If the value expands to something empty, we nuke the header.
static void
SslHdrExpand(SSL * ssl, const SslHdrInstance::expansion_list& expansions, TSMBuffer mbuf, TSMLoc mhdr)
{
  if (ssl == NULL) {
    for (SslHdrInstance::expansion_list::const_iterator e = expansions.begin(); e != expansions.end(); ++e) {
      SslHdrRemoveHeader(mbuf, mhdr, e->name);
    }
  } else {
    X509 * x509;
    BIO * exp = BIO_new(BIO_s_mem());

    for (SslHdrInstance::expansion_list::const_iterator e = expansions.begin(); e != expansions.end(); ++e) {
      switch (e->scope) {
      case SSL_HEADERS_SCOPE_CLIENT:
        x509 = SSL_get_peer_certificate(ssl);
        break;
      case SSL_HEADERS_SCOPE_SERVER:
        x509 = SSL_get_certificate(ssl);
        break;
      default:
        x509 = NULL;
      }

      if (x509 == NULL) {
        continue;
      }

      SslHdrExpandX509Field(exp, x509, e->field);
      if (BIO_pending(exp)) {
        SslHdrSetHeader(mbuf, mhdr, e->name, exp);
      } else {
        SslHdrRemoveHeader(mbuf, mhdr, e->name);
      }

      // Getting the peer certificate takes a reference count, but the server certificate doesn't.
      if (x509 && e->scope == SSL_HEADERS_SCOPE_CLIENT) {
        X509_free(x509);
      }
    }

    BIO_free(exp);
  }
}

static SslHdrInstance *
SslHdrParseOptions(int argc, const char ** argv)
{
  static const struct option longopt[] =
  {
    { const_cast<char *>("attach"), required_argument, 0, 'a' },
    {0, 0, 0, 0 }
  };

  std::auto_ptr<SslHdrInstance> hdr(new SslHdrInstance());

  // We might parse arguments multiple times if we are loaded as a global
  // plugin and a remap plugin. Reset optind so that getopt_long() does the
  // right thing (ie. work instead of crash).
  optind = 0;

  for (;;) {
    int opt;

    opt = getopt_long(argc, (char * const *)argv, "", longopt, NULL);
    switch (opt) {
    case 'a':
      if (strcmp(optarg, "client") == 0) {
        hdr->attach = SSL_HEADERS_ATTACH_CLIENT;
      } else if (strcmp(optarg, "server") == 0) {
        hdr->attach = SSL_HEADERS_ATTACH_SERVER;
      } else if (strcmp(optarg, "both") == 0) {
        hdr->attach = SSL_HEADERS_ATTACH_BOTH;
      } else {
        TSError("%s: invalid attach option '%s'", PLUGIN_NAME, optarg);
        return NULL;
      }

      break;
    }

    if (opt == -1) {
        break;
    }
  }

  // Pick up the remaining options as SSL header expansions.
  for (int i = optind; i < argc; ++i) {
    SslHdrExpansion exp;
    if (!SslHdrParseExpansion(argv[i], exp)) {
      // If we fail, the expansion parsing logs the error.
      return NULL;
    }

    hdr->expansions.push_back(exp);
  }

  return hdr.release();
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  SslHdrInstance * hdr;

  info.plugin_name = (char *)"sslheaders";
  info.vendor_name = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";

  if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS) {
    SslHdrError("plugin registration failed");
  }

  hdr = SslHdrParseOptions(argc, (const char **)argv);
  if (hdr) {
    switch (hdr->attach) {
    case SSL_HEADERS_ATTACH_SERVER:
      TSHttpHookAdd(TS_HTTP_SEND_REQUEST_HDR_HOOK, hdr->cont);
      break;
    case SSL_HEADERS_ATTACH_BOTH: /* fallthru */
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
TSRemapNewInstance(int argc, char * argv[], void ** instance, char * /* err */, int /* errsz */)
{
  SslHdrInstance * hdr;

  hdr = SslHdrParseOptions(argc, (const char **)argv);
  if (hdr) {
    *instance = hdr;
    return TS_SUCCESS;
  }

  return TS_ERROR;
}

void
TSRemapDeleteInstance(void * instance)
{
  SslHdrInstance * hdr = (SslHdrInstance *)instance;
  delete hdr;
}

TSRemapStatus
TSRemapDoRemap(void * instance, TSHttpTxn txn, TSRemapRequestInfo * /* rri */)
{
  SslHdrInstance * hdr = (SslHdrInstance *)instance;

  switch (hdr->attach) {
  case SSL_HEADERS_ATTACH_SERVER:
    TSHttpTxnHookAdd(txn, TS_HTTP_SEND_REQUEST_HDR_HOOK, hdr->cont);
    break;
  case SSL_HEADERS_ATTACH_BOTH: /* fallthru */
  case SSL_HEADERS_ATTACH_CLIENT:
    TSHttpTxnHookAdd(txn, TS_HTTP_READ_REQUEST_HDR_HOOK, hdr->cont);
    TSHttpTxnHookAdd(txn, TS_HTTP_SEND_REQUEST_HDR_HOOK, hdr->cont);
    break;
  }

  return TSREMAP_NO_REMAP;
}

SslHdrInstance::SslHdrInstance()
  : expansions(), attach(SSL_HEADERS_ATTACH_SERVER), cont(TSContCreate(SslHdrExpandRequestHook, NULL))
{
  TSContDataSet(cont, this);
}

SslHdrInstance::~SslHdrInstance()
{
  TSContDestroy(cont);
}

void
SslHdrInstance::register_hooks()
{

}
