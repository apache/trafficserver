/*
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

// Stub implementations of TS API functions for hrw_confcmp tool.

#include "ts/ts.h"
#include "ts/remap.h"
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <openssl/ssl.h>

#ifndef TS_REMAP_PSEUDO_HOOK
constexpr int TS_REMAP_PSEUDO_HOOK = 30;
#endif

const char *TS_MIME_FIELD_COOKIE = "Cookie";
int         TS_MIME_LEN_COOKIE   = 6;

const char *TS_HTTP_METHOD_CONNECT = "CONNECT";
const char *TS_HTTP_METHOD_DELETE  = "DELETE";
const char *TS_HTTP_METHOD_GET     = "GET";
const char *TS_HTTP_METHOD_HEAD    = "HEAD";
const char *TS_HTTP_METHOD_OPTIONS = "OPTIONS";
const char *TS_HTTP_METHOD_POST    = "POST";
const char *TS_HTTP_METHOD_PURGE   = "PURGE";
const char *TS_HTTP_METHOD_PUT     = "PUT";
const char *TS_HTTP_METHOD_TRACE   = "TRACE";
const char *TS_HTTP_METHOD_PUSH    = "PUSH";

int TS_HTTP_LEN_CONNECT = 7;
int TS_HTTP_LEN_DELETE  = 6;
int TS_HTTP_LEN_GET     = 3;
int TS_HTTP_LEN_HEAD    = 4;
int TS_HTTP_LEN_OPTIONS = 7;
int TS_HTTP_LEN_POST    = 4;
int TS_HTTP_LEN_PURGE   = 5;
int TS_HTTP_LEN_PUT     = 3;
int TS_HTTP_LEN_TRACE   = 5;
int TS_HTTP_LEN_PUSH    = 4;

// Stub implementations - these won't actually work for runtime,
// but allow the code to compile and link for static analysis/comparison

TSReturnCode
TSPluginRegister(TSPluginRegistrationInfo const *)
{
  return TS_SUCCESS;
}
void
TSHttpHookAdd(TSHttpHookID, TSCont)
{
}
const char *
TSConfigDirGet()
{
  return "/tmp";
}
const char *
TSHttpHookNameLookup(TSHttpHookID hook)
{
  // Return abbreviated hook names as used in header_rewrite configs
  // These match the names in Parser::cond_is_hook()
  switch (hook) {
  case TS_HTTP_READ_REQUEST_HDR_HOOK:
    return "READ_REQUEST_HDR_HOOK";
  case TS_HTTP_SEND_REQUEST_HDR_HOOK:
    return "SEND_REQUEST_HDR_HOOK";
  case TS_HTTP_READ_RESPONSE_HDR_HOOK:
    return "READ_RESPONSE_HDR_HOOK";
  case TS_HTTP_SEND_RESPONSE_HDR_HOOK:
    return "SEND_RESPONSE_HDR_HOOK";
  case TS_HTTP_TXN_START_HOOK:
    return "TXN_START_HOOK";
  case TS_HTTP_TXN_CLOSE_HOOK:
    return "TXN_CLOSE_HOOK";
  case TS_HTTP_PRE_REMAP_HOOK:
    return "READ_REQUEST_PRE_REMAP_HOOK";
  case TS_HTTP_POST_REMAP_HOOK:
    return "POST_REMAP_HOOK";
  case TS_REMAP_PSEUDO_HOOK:
    return "REMAP_PSEUDO_HOOK";
  default: {
    static char buf[64];

    snprintf(buf, sizeof(buf), "HOOK_%d", static_cast<int>(hook));
    return buf;
  }
  }
}

TSCont
TSContCreate(TSEventFunc, TSMutex)
{
  return nullptr;
}
void
TSContDestroy(TSCont)
{
}
void
TSContDataSet(TSCont, void *)
{
}
void *
TSContDataGet(TSCont)
{
  return nullptr;
}

TSMutex
TSMutexCreate()
{
  return nullptr;
}

void
TSError(const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  va_end(args);
}

void
TSWarning(const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  va_end(args);
}

int
_TSAssert(const char *, const char *, int)
{
  return 0;
}
void
_TSReleaseAssert(const char *, const char *, int)
{
  std::exit(1);
}
char *
_TSstrdup(const char *str, int64_t, const char *)
{
  return strdup(str);
}
void
TSfree(void *ptr)
{
  free(ptr);
}

TSReturnCode
TSUserArgIndexReserve(TSUserArgType, const char *, const char *, int *idx)
{
  static int counter = 0;
  *idx               = counter++;
  return TS_SUCCESS;
}

void
TSUserArgSet(void *, int, void *)
{
}

void *
TSUserArgGet(void *, int)
{
  return nullptr;
}

TSMBuffer
TSMBufferCreate()
{
  return nullptr;
}

TSReturnCode
TSMBufferDestroy(TSMBuffer)
{
  return TS_SUCCESS;
}

TSMLoc
TSHttpHdrCreate(TSMBuffer)
{
  return nullptr;
}

TSReturnCode
TSHttpHdrTypeSet(TSMBuffer, TSMLoc, TSHttpType)
{
  return TS_SUCCESS;
}

TSReturnCode
TSHttpHdrUrlGet(TSMBuffer, TSMLoc, TSMLoc *)
{
  return TS_ERROR;
}

TSReturnCode
TSHttpHdrUrlSet(TSMBuffer, TSMLoc, TSMLoc)
{
  return TS_SUCCESS;
}

const char *
TSHttpHdrMethodGet(TSMBuffer, TSMLoc, int *length)
{
  *length = 3;
  return "GET";
}

TSHttpStatus
TSHttpHdrStatusGet(TSMBuffer, TSMLoc)
{
  return TS_HTTP_STATUS_OK;
}

TSReturnCode
TSHttpHdrStatusSet(TSMBuffer, TSMLoc, TSHttpStatus, TSHttpTxn, std::string_view)
{
  return TS_SUCCESS;
}

TSReturnCode
TSHttpHdrReasonSet(TSMBuffer, TSMLoc, const char *, int)
{
  return TS_SUCCESS;
}

const char *
TSHttpHdrReasonLookup(TSHttpStatus)
{
  return "OK";
}

TSParseResult
TSHttpHdrParseResp(TSHttpParser, TSMBuffer, TSMLoc, const char **, const char *)
{
  return TS_PARSE_ERROR;
}

TSHttpParser
TSHttpParserCreate()
{
  return nullptr;
}

void
TSHttpParserDestroy(TSHttpParser)
{
}

TSReturnCode
TSUrlCreate(TSMBuffer, TSMLoc *)
{
  return TS_ERROR;
}

TSParseResult
TSUrlParse(TSMBuffer, TSMLoc, const char **, const char *)
{
  return TS_PARSE_ERROR;
}

const char *
TSUrlSchemeGet(TSMBuffer, TSMLoc, int *length)
{
  *length = 4;
  return "http";
}

TSReturnCode
TSUrlSchemeSet(TSMBuffer, TSMLoc, const char *, int)
{
  return TS_SUCCESS;
}

const char *
TSUrlHostGet(TSMBuffer, TSMLoc, int *length)
{
  *length = 9;
  return "localhost";
}

TSReturnCode
TSUrlHostSet(TSMBuffer, TSMLoc, const char *, int)
{
  return TS_SUCCESS;
}

int
TSUrlPortGet(TSMBuffer, TSMLoc)
{
  return 80;
}

TSReturnCode
TSUrlPortSet(TSMBuffer, TSMLoc, int)
{
  return TS_SUCCESS;
}

const char *
TSUrlPathGet(TSMBuffer, TSMLoc, int *length)
{
  *length = 1;
  return "/";
}

TSReturnCode
TSUrlPathSet(TSMBuffer, TSMLoc, const char *, int)
{
  return TS_SUCCESS;
}

const char *
TSUrlHttpQueryGet(TSMBuffer, TSMLoc, int *length)
{
  *length = 0;
  return "";
}

TSReturnCode
TSUrlHttpQuerySet(TSMBuffer, TSMLoc, const char *, int)
{
  return TS_SUCCESS;
}

char *
TSUrlStringGet(TSMBuffer, TSMLoc, int *length)
{
  static char url[] = "http://localhost/";
  *length           = strlen(url);
  return strdup(url);
}

TSMLoc
TSMimeHdrFieldFind(TSMBuffer, TSMLoc, const char *, int)
{
  return nullptr;
}

TSMLoc
TSMimeHdrFieldNextDup(TSMBuffer, TSMLoc, TSMLoc)
{
  return nullptr;
}

TSReturnCode
TSMimeHdrFieldCreateNamed(TSMBuffer, TSMLoc, const char *, int, TSMLoc *)
{
  return TS_ERROR;
}

TSReturnCode
TSMimeHdrFieldAppend(TSMBuffer, TSMLoc, TSMLoc)
{
  return TS_SUCCESS;
}

TSReturnCode
TSMimeHdrFieldDestroy(TSMBuffer, TSMLoc, TSMLoc)
{
  return TS_SUCCESS;
}

const char *
TSMimeHdrFieldValueStringGet(TSMBuffer, TSMLoc, TSMLoc, int, int *length)
{
  *length = 0;
  return "";
}

TSReturnCode
TSMimeHdrFieldValueStringSet(TSMBuffer, TSMLoc, TSMLoc, int, const char *, int)
{
  return TS_SUCCESS;
}

const char *
TSMimeHdrStringToWKS(const char *, int)
{
  return nullptr;
}

TSReturnCode
TSHandleMLocRelease(TSMBuffer, TSMLoc, TSMLoc)
{
  return TS_SUCCESS;
}

TSHttpSsn
TSHttpTxnSsnGet(TSHttpTxn)
{
  return nullptr;
}

TSVConn
TSHttpSsnClientVConnGet(TSHttpSsn)
{
  return nullptr;
}

int
TSHttpSsnTransactionCount(TSHttpSsn)
{
  return 1;
}

int
TSHttpTxnServerSsnTransactionCount(TSHttpTxn)
{
  return 1;
}

TSReturnCode
TSHttpTxnClientReqGet(TSHttpTxn, TSMBuffer *, TSMLoc *)
{
  return TS_ERROR;
}

TSReturnCode
TSHttpTxnClientRespGet(TSHttpTxn, TSMBuffer *, TSMLoc *)
{
  return TS_ERROR;
}

TSReturnCode
TSHttpTxnServerReqGet(TSHttpTxn, TSMBuffer *, TSMLoc *)
{
  return TS_ERROR;
}

TSReturnCode
TSHttpTxnServerRespGet(TSHttpTxn, TSMBuffer *, TSMLoc *)
{
  return TS_ERROR;
}

TSReturnCode
TSHttpTxnPristineUrlGet(TSHttpTxn, TSMBuffer *, TSMLoc *)
{
  return TS_ERROR;
}

void
TSHttpTxnReenable(TSHttpTxn, TSEvent)
{
}

void
TSHttpTxnHookAdd(TSHttpTxn, TSHttpHookID, TSCont)
{
}

uint64_t
TSHttpTxnIdGet(TSHttpTxn)
{
  return 12345;
}

int
TSHttpTxnIsInternal(TSHttpTxn)
{
  return 0;
}

TSReturnCode
TSHttpTxnCacheLookupStatusGet(TSHttpTxn, int *status)
{
  *status = 0;
  return TS_SUCCESS;
}

void
TSHttpTxnStatusSet(TSHttpTxn, TSHttpStatus, std::string_view)
{
}

void
TSHttpTxnErrorBodySet(TSHttpTxn, char *, size_t, char *)
{
}

const sockaddr *
TSHttpTxnClientAddrGet(TSHttpTxn)
{
  return nullptr;
}

const sockaddr *
TSHttpTxnIncomingAddrGet(TSHttpTxn)
{
  return nullptr;
}

const sockaddr *
TSHttpTxnOutgoingAddrGet(TSHttpTxn)
{
  return nullptr;
}

const sockaddr *
TSHttpTxnServerAddrGet(TSHttpTxn)
{
  return nullptr;
}

TSReturnCode
TSHttpTxnVerifiedAddrGet(TSHttpTxn, const sockaddr **)
{
  return TS_ERROR;
}

TSReturnCode
TSHttpTxnVerifiedAddrSet(TSHttpTxn, const sockaddr *)
{
  return TS_SUCCESS;
}

const char *
TSHttpTxnNextHopNameGet(TSHttpTxn)
{
  return "nexthop";
}

int
TSHttpTxnNextHopPortGet(TSHttpTxn)
{
  return 8080;
}

const char *
TSHttpNextHopStrategyNameGet(const void *)
{
  return "default";
}

const void *
TSHttpTxnNextHopNamedStrategyGet(TSHttpTxn, const char *)
{
  return nullptr;
}

void
TSHttpTxnNextHopStrategySet(TSHttpTxn, const void *)
{
}

bool
TSHttpTxnCntlGet(TSHttpTxn, TSHttpCntlType)
{
  return false;
}

TSReturnCode
TSHttpTxnCntlSet(TSHttpTxn, TSHttpCntlType, bool)
{
  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnConfigFind(const char *, int, TSOverridableConfigKey *, TSRecordDataType *)
{
  return TS_ERROR;
}

TSReturnCode
TSHttpTxnConfigIntSet(TSHttpTxn, TSOverridableConfigKey, int64_t)
{
  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnConfigFloatSet(TSHttpTxn, TSOverridableConfigKey, float)
{
  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnConfigStringSet(TSHttpTxn, TSOverridableConfigKey, const char *, int)
{
  return TS_SUCCESS;
}

void
TSHttpTxnActiveTimeoutSet(TSHttpTxn, int)
{
}

void
TSHttpTxnNoActivityTimeoutSet(TSHttpTxn, int)
{
}

void
TSHttpTxnConnectTimeoutSet(TSHttpTxn, int)
{
}

void
TSHttpTxnDNSTimeoutSet(TSHttpTxn, int)
{
}

TSReturnCode
TSHttpTxnClientPacketDscpSet(TSHttpTxn, int)
{
  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnClientPacketMarkSet(TSHttpTxn, int)
{
  return TS_SUCCESS;
}

TSReturnCode
TSHttpTxnClientFdGet(TSHttpTxn, int *fd)
{
  *fd = -1;
  return TS_ERROR;
}

TSReturnCode
TSHttpTxnClientProtocolStackGet(TSHttpTxn, int, const char **, int *count)
{
  *count = 0;
  return TS_SUCCESS;
}

const char *
TSHttpTxnClientProtocolStackContains(TSHttpTxn, const char *)
{
  return nullptr;
}

TSReturnCode
TSClientRequestUuidGet(TSHttpTxn, char *buf)
{
  strcpy(buf, "uuid-1234");
  return TS_SUCCESS;
}

TSUuid
TSProcessUuidGet()
{
  return nullptr;
}

const char *
TSUuidStringGet(TSUuid)
{
  return "process-uuid";
}

TSReturnCode
TSVConnPPInfoGet(TSVConn, unsigned short, const char **, int *)
{
  return TS_ERROR;
}

TSSslConnection
TSVConnSslConnectionGet(TSVConn)
{
  return nullptr;
}

TSFetchSM
TSFetchUrl(const char *, int, const sockaddr *, TSCont, TSFetchWakeUpOptions, TSFetchEvent)
{
  return nullptr;
}

char *
TSFetchRespGet(TSHttpTxn, int *length)
{
  *length = 0;
  return nullptr;
}

int
TSStatCreate(const char *, TSRecordDataType, TSStatPersistence, TSStatSync)
{
  return -1;
}

TSReturnCode
TSStatFindName(const char *, int *id)
{
  *id = -1;
  return TS_ERROR;
}

void
TSStatIntIncrement(int, int64_t)
{
}

bool
isPluginDynamicReloadEnabled()
{
  return false;
}

int cmd_disable_pfreelist = 0;

#include "proxy/http/remap/PluginFactory.h"

void
RemapPluginInst::done()
{
}

TSRemapStatus
RemapPluginInst::doRemap(TSHttpTxn, TSRemapRequestInfo *)
{
  return TSREMAP_NO_REMAP;
}

PluginFactory::PluginFactory() {}

PluginFactory::~PluginFactory() {}

PluginFactory &
PluginFactory::addSearchDir(const fs::path &)
{
  return *this;
}

PluginFactory &
PluginFactory::setRuntimeDir(const fs::path &)
{
  return *this;
}

RemapPluginInst *
PluginFactory::getRemapPlugin(const fs::path &, int, char **, std::string &, bool)
{
  return nullptr;
}

const char *
PluginFactory::getUuid()
{
  return "stub-uuid";
}

#include "tscore/ink_config.h"

#if TS_HAS_CRIPTS
#include <string>
#include <openssl/x509.h>

namespace detail
{
class CertBase
{
public:
  class X509Value
  {
  public:
    virtual ~X509Value();
    void _load_long(long (*)(const X509 *)) const;
    void _load_name(X509_NAME *(*)(const X509 *)) const;
    void _load_time(ASN1_STRING *(*)(const X509 *)) const;
    void _load_integer(ASN1_STRING *(*)(X509 *)) const;
  };

  class Version : public X509Value
  {
  public:
    void _load() const;
  };

  class Subject : public X509Value
  {
  public:
    void _load() const;
  };

  class Issuer : public X509Value
  {
  public:
    void _load() const;
  };

  class SerialNumber : public X509Value
  {
  public:
    void _load() const;
  };

  class NotBefore : public X509Value
  {
  public:
    void _load() const;
  };

  class NotAfter : public X509Value
  {
  public:
    void _load() const;
  };

  class Certificate : public X509Value
  {
  public:
    Certificate(CertBase *);
    virtual ~Certificate();
  };

  class Signature : public X509Value
  {
  public:
    Signature(CertBase *);
    virtual ~Signature();
  };

  class SAN
  {
  public:
    class SANBase
    {
    public:
      std::string Join(const char *) const;
    };
    SANBase dns;
    SANBase ipadd;
    SANBase email;
    SANBase uri;
  };
};

CertBase::X509Value::~X509Value() {}

void
CertBase::X509Value::_load_long(long (*)(const X509 *)) const
{
}

void
CertBase::X509Value::_load_name(X509_NAME *(*)(const X509 *)) const
{
}

void
CertBase::X509Value::_load_time(ASN1_STRING *(*)(const X509 *)) const
{
}

void
CertBase::X509Value::_load_integer(ASN1_STRING *(*)(X509 *)) const
{
}

void
CertBase::Version::_load() const
{
}

void
CertBase::Subject::_load() const
{
}

void
CertBase::Issuer::_load() const
{
}

void
CertBase::SerialNumber::_load() const
{
}

void
CertBase::NotBefore::_load() const
{
}

void
CertBase::NotAfter::_load() const
{
}

CertBase::Certificate::Certificate(CertBase *) {}
CertBase::Certificate::~Certificate() {}
CertBase::Signature::Signature(CertBase *) {}
CertBase::Signature::~Signature() {}

std::string
CertBase::SAN::SANBase::Join(const char *) const
{
  return "";
}

} // namespace detail

namespace cripts
{
namespace Client
{
  class Connection
  {
  public:
    Connection();
    virtual ~Connection();
  };

  Connection::Connection() {}
  Connection::~Connection() {}
} // namespace Client

namespace Server
{
  class Connection
  {
  public:
    Connection();
    virtual ~Connection();
  };

  Connection::Connection() {}
  Connection::~Connection() {}
} // namespace Server
} // namespace cripts
#endif
