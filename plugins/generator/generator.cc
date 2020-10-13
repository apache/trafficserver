/** @file

  Traffic generator intercept plugin

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

#include <ts/ts.h>
#include <ts/remap.h>
#include <cerrno>
#include <cinttypes>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>

// Generator plugin
//
// The incoming URL must consist of 2 or more path components. The first
// component indicates cacheability, the second the number of bytes in the
// response body. Subsequent path components are ignored, so they can be used
// to uniqify cache keys (assuming that caching is enabled).
//
// Examples,
//
// /cache/100/6b1e2b1fa555b52124cb4e511acbae2a
//      -> return 100 bytes, cached
//
// /cache/21474836480/large/response
//      -> return 20G bytes, cached
//
// TODO Add query parameter options.
//
// It would be pretty useful to add support for query parameters to tweak how the response
// is handled. The following parameters seem useful:
//
//  - delay before sending the response headers
//  - delay before sending the response body
//  - turn off local caching of the response (ie. even for /cache/* URLs)
//  - force chunked encoding by omitting Content-Length
//  - specify the Cache-Control max-age
//
// We ought to scale the IO buffer size in proportion to the size of the response we are generating.

#define PLUGIN "generator"

#define VDEBUG(fmt, ...) TSDebug(PLUGIN, fmt, ##__VA_ARGS__)

#if DEBUG
#define VERROR(fmt, ...) TSDebug(PLUGIN, fmt, ##__VA_ARGS__)
#else
#define VERROR(fmt, ...) TSError("[%s] %s: " fmt, PLUGIN, __FUNCTION__, ##__VA_ARGS__)
#endif

#define VIODEBUG(vio, fmt, ...)                                                                                              \
  VDEBUG("vio=%p vio.cont=%p, vio.cont.data=%p, vio.vc=%p " fmt, (vio), TSVIOContGet(vio), TSContDataGet(TSVIOContGet(vio)), \
         TSVIOVConnGet(vio), ##__VA_ARGS__)

static TSCont TxnHook;
static uint8_t GeneratorData[32 * 1024];

static int StatCountBytes     = -1;
static int StatCountResponses = -1;

static int GeneratorInterceptHook(TSCont contp, TSEvent event, void *edata);
static int GeneratorTxnHook(TSCont contp, TSEvent event, void *edata);

struct GeneratorRequest;

union argument_type {
  void *ptr;
  intptr_t ecode;
  TSVConn vc;
  TSVIO vio;
  TSHttpTxn txn;
  GeneratorRequest *grq;

  argument_type(void *_p) : ptr(_p) {}
};

// Return the length of a string literal (without the trailing NUL).
template <unsigned N>
unsigned
lengthof(const char (&)[N])
{
  return N - 1;
}

// This structure represents the state of a streaming I/O request. It
// is directional (ie. either a read or a write). We need two of these
// for each TSVConn; one to push data into the TSVConn and one to pull
// data out.
struct IOChannel {
  TSVIO vio = nullptr;
  TSIOBuffer iobuf;
  TSIOBufferReader reader;

  IOChannel() : iobuf(TSIOBufferSizedCreate(TS_IOBUFFER_SIZE_INDEX_32K)), reader(TSIOBufferReaderAlloc(iobuf)) {}
  ~IOChannel()
  {
    if (this->reader) {
      TSIOBufferReaderFree(this->reader);
    }

    if (this->iobuf) {
      TSIOBufferDestroy(this->iobuf);
    }
  }

  void
  read(TSVConn vc, TSCont contp)
  {
    this->vio = TSVConnRead(vc, contp, this->iobuf, INT64_MAX);
  }

  void
  write(TSVConn vc, TSCont contp)
  {
    this->vio = TSVConnWrite(vc, contp, this->reader, INT64_MAX);
  }
};

struct GeneratorHttpHeader {
  TSMBuffer buffer;
  TSMLoc header;
  TSHttpParser parser;

  GeneratorHttpHeader()
  {
    this->buffer = TSMBufferCreate();
    this->header = TSHttpHdrCreate(this->buffer);
    this->parser = TSHttpParserCreate();
  }

  ~GeneratorHttpHeader()
  {
    if (this->parser) {
      TSHttpParserDestroy(this->parser);
    }

    TSHttpHdrDestroy(this->buffer, this->header);
    TSHandleMLocRelease(this->buffer, TS_NULL_MLOC, this->header);
    TSMBufferDestroy(this->buffer);
  }
};

struct GeneratorRequest {
  off_t nbytes   = 0; // Number of bytes to generate.
  unsigned flags = 0;
  unsigned delay = 0; // Milliseconds to delay before sending a response.
  unsigned maxage;    // Max age for cache responses.
  IOChannel readio;
  IOChannel writeio;
  GeneratorHttpHeader rqheader;

  enum {
    CACHEABLE = 0x0001,
    ISHEAD    = 0x0002,
  };

  GeneratorRequest() : maxage(60 * 60 * 24) {}
  ~GeneratorRequest() = default;
};

// Destroy a generator request, including the per-txn continuation.
static void
GeneratorRequestDestroy(GeneratorRequest *grq, TSVIO vio, TSCont contp)
{
  if (vio) {
    TSVConnClose(TSVIOVConnGet(vio));
  }

  TSContDestroy(contp);
  delete grq;
}

static off_t
GeneratorParseByteCount(const char *ptr, const char *end)
{
  off_t nbytes = 0;

  for (; ptr < end; ++ptr) {
    switch (*ptr) {
    case '0':
      nbytes = nbytes * 10 + 0;
      break;
    case '1':
      nbytes = nbytes * 10 + 1;
      break;
    case '2':
      nbytes = nbytes * 10 + 2;
      break;
    case '3':
      nbytes = nbytes * 10 + 3;
      break;
    case '4':
      nbytes = nbytes * 10 + 4;
      break;
    case '5':
      nbytes = nbytes * 10 + 5;
      break;
    case '6':
      nbytes = nbytes * 10 + 6;
      break;
    case '7':
      nbytes = nbytes * 10 + 7;
      break;
    case '8':
      nbytes = nbytes * 10 + 8;
      break;
    case '9':
      nbytes = nbytes * 10 + 9;
      break;
    default:
      return -1;
    }
  }

  return nbytes;
}

static void
HeaderFieldDateSet(GeneratorHttpHeader &http, const char *field_name, int64_t field_len, time_t value)
{
  TSMLoc field;

  TSMimeHdrFieldCreateNamed(http.buffer, http.header, field_name, field_len, &field);
  TSMimeHdrFieldValueDateSet(http.buffer, http.header, field, value);
  TSMimeHdrFieldAppend(http.buffer, http.header, field);
  TSHandleMLocRelease(http.buffer, http.header, field);
}

static void
HeaderFieldIntSet(GeneratorHttpHeader &http, const char *field_name, int64_t field_len, int64_t value)
{
  TSMLoc field;

  TSMimeHdrFieldCreateNamed(http.buffer, http.header, field_name, field_len, &field);
  TSMimeHdrFieldValueInt64Set(http.buffer, http.header, field, -1, value);
  TSMimeHdrFieldAppend(http.buffer, http.header, field);
  TSHandleMLocRelease(http.buffer, http.header, field);
}

static void
HeaderFieldStringSet(GeneratorHttpHeader &http, const char *field_name, int64_t field_len, const char *value)
{
  TSMLoc field;

  TSMimeHdrFieldCreateNamed(http.buffer, http.header, field_name, field_len, &field);
  TSMimeHdrFieldValueStringSet(http.buffer, http.header, field, -1, value, -1);
  TSMimeHdrFieldAppend(http.buffer, http.header, field);
  TSHandleMLocRelease(http.buffer, http.header, field);
}

static int64_t
GeneratorGetRequestHeader(GeneratorHttpHeader &request, const char *field_name, int64_t field_len, int64_t default_value)
{
  TSMLoc field;

  field = TSMimeHdrFieldFind(request.buffer, request.header, field_name, field_len);
  if (field != TS_NULL_MLOC) {
    default_value = TSMimeHdrFieldValueInt64Get(request.buffer, request.header, field, -1);
  }

  TSHandleMLocRelease(request.buffer, request.header, field);
  return default_value;
}

static TSReturnCode
GeneratorWriteResponseHeader(GeneratorRequest *grq, TSCont contp)
{
  GeneratorHttpHeader response;

  VDEBUG("writing response header");

  if (TSHttpHdrTypeSet(response.buffer, response.header, TS_HTTP_TYPE_RESPONSE) != TS_SUCCESS) {
    VERROR("failed to set type");
    return TS_ERROR;
  }
  if (TSHttpHdrVersionSet(response.buffer, response.header, TS_HTTP_VERSION(1, 1)) != TS_SUCCESS) {
    VERROR("failed to set HTTP version");
    return TS_ERROR;
  }
  if (TSHttpHdrStatusSet(response.buffer, response.header, TS_HTTP_STATUS_OK) != TS_SUCCESS) {
    VERROR("failed to set HTTP status");
    return TS_ERROR;
  }

  TSHttpHdrReasonSet(response.buffer, response.header, TSHttpHdrReasonLookup(TS_HTTP_STATUS_OK), -1);

  // Set the Content-Length header.
  HeaderFieldIntSet(response, TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH, grq->nbytes);

  // Set the Cache-Control header.
  if (grq->flags & GeneratorRequest::CACHEABLE) {
    char buf[64];

    snprintf(buf, sizeof(buf), "max-age=%u, public", grq->maxage);
    HeaderFieldStringSet(response, TS_MIME_FIELD_CACHE_CONTROL, TS_MIME_LEN_CACHE_CONTROL, buf);
    HeaderFieldDateSet(response, TS_MIME_FIELD_LAST_MODIFIED, TS_MIME_LEN_LAST_MODIFIED, time(nullptr));
  } else {
    HeaderFieldStringSet(response, TS_MIME_FIELD_CACHE_CONTROL, TS_MIME_LEN_CACHE_CONTROL, "private");
  }

  // Write the header to the IO buffer. Set the VIO bytes so that we can get a WRITE_COMPLETE
  // event when this is done.
  int hdrlen = TSHttpHdrLengthGet(response.buffer, response.header);

  TSHttpHdrPrint(response.buffer, response.header, grq->writeio.iobuf);
  TSVIONBytesSet(grq->writeio.vio, hdrlen);
  TSVIOReenable(grq->writeio.vio);

  TSStatIntIncrement(StatCountBytes, hdrlen);

  return TS_SUCCESS;
}

static bool
GeneratorParseRequest(GeneratorRequest *grq)
{
  TSMLoc url;
  const char *path;
  const char *end;
  int pathsz;
  unsigned count = 0;

  // First, make sure this is a GET request.
  path = TSHttpHdrMethodGet(grq->rqheader.buffer, grq->rqheader.header, &pathsz);
  if (path != TS_HTTP_METHOD_GET && path != TS_HTTP_METHOD_HEAD) {
    VDEBUG("%.*s method is not supported", pathsz, path);
    return false;
  }

  if (path == TS_HTTP_METHOD_HEAD) {
    grq->flags |= GeneratorRequest::ISHEAD;
  }

  grq->delay  = GeneratorGetRequestHeader(grq->rqheader, "Generator-Delay", lengthof("Generator-Delay"), grq->delay);
  grq->maxage = GeneratorGetRequestHeader(grq->rqheader, "Generator-MaxAge", lengthof("Generator-MaxAge"), grq->maxage);

  // Next, parse our parameters out of the URL.
  if (TSHttpHdrUrlGet(grq->rqheader.buffer, grq->rqheader.header, &url) != TS_SUCCESS) {
    VERROR("failed to get URI handle");
    return false;
  }

  path = TSUrlPathGet(grq->rqheader.buffer, url, &pathsz);
  if (!path) {
    VDEBUG("empty path");
    return false;
  }

  VDEBUG("requested path is %.*s", pathsz, path);

  end = path + pathsz;
  while (path < end) {
    const char *sep = path;
    size_t nbytes;

    while (*sep != '/' && sep < end) {
      ++sep;
    }

    nbytes = std::distance(path, sep);
    if (nbytes) {
      VDEBUG("path component is %.*s", (int)nbytes, path);

      switch (count) {
      case 0:
        // First path component is "cache" or "nocache".
        if (memcmp(path, "cache", 5) == 0) {
          grq->flags |= GeneratorRequest::CACHEABLE;
        } else if (memcmp(path, "nocache", 7) == 0) {
          grq->flags &= ~GeneratorRequest::CACHEABLE;
        } else {
          VDEBUG("first component is %.*s, expecting 'cache' or 'nocache'", (int)nbytes, path);
          goto fail;
        }

        break;

      case 1:
        // Second path component is a byte count.
        grq->nbytes = GeneratorParseByteCount(path, sep);
        VDEBUG("generator byte count is %lld", (long long)grq->nbytes);
        if (grq->nbytes >= 0) {
          // We don't care about any other path components.
          TSHandleMLocRelease(grq->rqheader.buffer, grq->rqheader.header, url);
          return true;
        }

        goto fail;
      }

      ++count;
    }

    path = sep + 1;
  }

fail:
  TSHandleMLocRelease(grq->rqheader.buffer, grq->rqheader.header, url);
  return false;
}

// Handle events from TSHttpTxnServerIntercept. The intercept
// starts with TS_EVENT_NET_ACCEPT, and then continues with
// TSVConn events.
static int
GeneratorInterceptHook(TSCont contp, TSEvent event, void *edata)
{
  argument_type arg(edata);

  VDEBUG("contp=%p, event=%s (%d), edata=%p", contp, TSHttpEventNameLookup(event), event, arg.ptr);

  switch (event) {
  case TS_EVENT_NET_ACCEPT: {
    // TS_EVENT_NET_ACCEPT will be delivered when the server intercept
    // is set up by the core. We just need to allocate a generator
    // request state and start reading the VC.
    GeneratorRequest *grq = new GeneratorRequest();

    TSStatIntIncrement(StatCountResponses, 1);
    VDEBUG("allocated server intercept generator grq=%p", grq);

    // This continuation was allocated in GeneratorTxnHook. Reset the
    // data to keep track of this generator request.
    TSContDataSet(contp, grq);

    // Start reading the request from the server intercept VC.
    grq->readio.read(arg.vc, contp);
    VIODEBUG(grq->readio.vio, "started reading generator request");

    return TS_EVENT_NONE;
  }

  case TS_EVENT_NET_ACCEPT_FAILED: {
    // TS_EVENT_NET_ACCEPT_FAILED will be delivered if the
    // transaction is cancelled before we start tunnelling
    // through the server intercept. One way that this can happen
    // is if the intercept is attached early, and then we server
    // the document out of cache.
    argument_type cdata(TSContDataGet(contp));

    // There's nothing to do here except nuke the continuation
    // that was allocated in GeneratorTxnHook().
    VDEBUG("cancelling server intercept request for txn=%p", cdata.txn);

    TSContDestroy(contp);
    return TS_EVENT_NONE;
  }

  case TS_EVENT_VCONN_READ_READY: {
    argument_type cdata           = TSContDataGet(contp);
    GeneratorHttpHeader &rqheader = cdata.grq->rqheader;

    VDEBUG("reading vio=%p vc=%p, grq=%p", arg.vio, TSVIOVConnGet(arg.vio), cdata.grq);

    TSIOBufferBlock blk;
    ssize_t consumed     = 0;
    TSParseResult result = TS_PARSE_CONT;

    for (blk = TSIOBufferReaderStart(cdata.grq->readio.reader); blk; blk = TSIOBufferBlockNext(blk)) {
      const char *ptr;
      const char *end;
      int64_t nbytes;

      ptr = TSIOBufferBlockReadStart(blk, cdata.grq->readio.reader, &nbytes);
      if (ptr == nullptr || nbytes == 0) {
        continue;
      }

      end    = ptr + nbytes;
      result = TSHttpHdrParseReq(rqheader.parser, rqheader.buffer, rqheader.header, &ptr, end);
      switch (result) {
      case TS_PARSE_ERROR:
        // If we got a bad request, just shut it down.
        VDEBUG("bad request on grq=%p, sending an error", cdata.grq);
        GeneratorRequestDestroy(cdata.grq, arg.vio, contp);
        return TS_EVENT_ERROR;

      case TS_PARSE_DONE:
        // Check the response.
        VDEBUG("parsed request on grq=%p, sending a response ", cdata.grq);
        if (!GeneratorParseRequest(cdata.grq)) {
          // We got a syntactically bad URL. It would be graceful to send
          // a 400 response, but we are graceless and just fail the
          // transaction.
          GeneratorRequestDestroy(cdata.grq, arg.vio, contp);
          return TS_EVENT_ERROR;
        }

        // If this is a HEAD request, we don't need to send any bytes.
        if (cdata.grq->flags & GeneratorRequest::ISHEAD) {
          cdata.grq->nbytes = 0;
        }

        // Start the vconn write.
        cdata.grq->writeio.write(TSVIOVConnGet(arg.vio), contp);
        TSVIONBytesSet(cdata.grq->writeio.vio, 0);

        if (cdata.grq->delay > 0) {
          VDEBUG("delaying response by %ums", cdata.grq->delay);
          TSContScheduleOnPool(contp, cdata.grq->delay, TS_THREAD_POOL_NET);
          return TS_EVENT_NONE;
        }

        if (GeneratorWriteResponseHeader(cdata.grq, contp) != TS_SUCCESS) {
          VERROR("failure writing response");
          return TS_EVENT_ERROR;
        }
        return TS_EVENT_NONE;

      case TS_PARSE_CONT:
        // We consumed the buffer we got minus the remainder.
        consumed += (nbytes - std::distance(ptr, end));
      }
    }

    TSReleaseAssert(result == TS_PARSE_CONT);

    // Reenable the read VIO to get more events.
    TSVIOReenable(arg.vio);
    return TS_EVENT_NONE;
  }

  case TS_EVENT_VCONN_WRITE_READY: {
    argument_type cdata = TSContDataGet(contp);

    if (cdata.grq->nbytes) {
      int64_t nbytes;

      if (cdata.grq->nbytes >= static_cast<ssize_t>(sizeof(GeneratorData))) {
        nbytes = sizeof(GeneratorData);
      } else {
        nbytes = cdata.grq->nbytes % sizeof(GeneratorData);
      }

      VIODEBUG(arg.vio, "writing %" PRId64 " bytes for grq=%p", nbytes, cdata.grq);
      nbytes = TSIOBufferWrite(cdata.grq->writeio.iobuf, GeneratorData, nbytes);

      cdata.grq->nbytes -= nbytes;
      TSStatIntIncrement(StatCountBytes, nbytes);

      // Update the number of bytes to write.
      TSVIONBytesSet(arg.vio, TSVIONBytesGet(arg.vio) + nbytes);
      TSVIOReenable(arg.vio);
    }

    return TS_EVENT_NONE;
  }

  case TS_EVENT_ERROR:
  case TS_EVENT_VCONN_EOS: {
    argument_type cdata = TSContDataGet(contp);

    VIODEBUG(arg.vio, "received EOS or ERROR for grq=%p", cdata.grq);
    GeneratorRequestDestroy(cdata.grq, arg.vio, contp);
    return event == TS_EVENT_ERROR ? TS_EVENT_ERROR : TS_EVENT_NONE;
  }

  case TS_EVENT_VCONN_READ_COMPLETE:
    // We read data forever, so we should never get a READ_COMPLETE.
    VIODEBUG(arg.vio, "unexpected TS_EVENT_VCONN_READ_COMPLETE");
    return TS_EVENT_NONE;

  case TS_EVENT_VCONN_WRITE_COMPLETE: {
    argument_type cdata = TSContDataGet(contp);

    // If we still have bytes to write, kick off a new write operation, otherwise
    // we are done and we can shut down the VC.
    if (cdata.grq->nbytes) {
      cdata.grq->writeio.write(TSVIOVConnGet(arg.vio), contp);
      TSVIONBytesSet(cdata.grq->writeio.vio, cdata.grq->nbytes);
    } else {
      VIODEBUG(arg.vio, "TS_EVENT_VCONN_WRITE_COMPLETE %" PRId64 " todo", TSVIONTodoGet(arg.vio));
      GeneratorRequestDestroy(cdata.grq, arg.vio, contp);
    }

    return TS_EVENT_NONE;
  }

  case TS_EVENT_TIMEOUT: {
    // Our response delay expired, so write the headers now, which
    // will also trigger the read+write event flow.
    argument_type cdata = TSContDataGet(contp);
    if (GeneratorWriteResponseHeader(cdata.grq, contp) != TS_SUCCESS) {
      VERROR("failure writing response");
      return TS_EVENT_ERROR;
    }
    return TS_EVENT_NONE;
  }

  case TS_EVENT_VCONN_INACTIVITY_TIMEOUT:
    VERROR("unexpected event %s (%d) edata=%p", TSHttpEventNameLookup(event), event, arg.ptr);
    return TS_EVENT_ERROR;

  default:
    VERROR("unexpected event %s (%d) edata=%p", TSHttpEventNameLookup(event), event, arg.ptr);
    return TS_EVENT_ERROR;
  }
}

// Little helper function, to turn off the cache on requests which aren't cacheable to begin with.
// This helps performance, a lot.
static void
CheckCacheable(TSHttpTxn txnp, TSMLoc url, TSMBuffer bufp)
{
  int pathsz       = 0;
  const char *path = TSUrlPathGet(bufp, url, &pathsz);

  if (path && (pathsz >= 8) && (0 == memcmp(path, "nocache/", 8))) {
    // It's not cacheable, so, turn off the cache. This avoids major serialization and performance issues.
    VDEBUG("turning off the cache, uncacehable");
    TSHttpTxnConfigIntSet(txnp, TS_CONFIG_HTTP_CACHE_HTTP, 0);
  }
}

// Handle events that occur on the TSHttpTxn.
static int
GeneratorTxnHook(TSCont contp, TSEvent event, void *edata)
{
  argument_type arg(edata);

  VDEBUG("contp=%p, event=%s (%d), edata=%p", contp, TSHttpEventNameLookup(event), event, edata);

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR: {
    TSMBuffer recp;
    TSMLoc url_loc;
    TSMLoc hdr_loc;

    if (TSHttpTxnClientReqGet(arg.txn, &recp, &hdr_loc) != TS_SUCCESS) {
      VERROR("failed to get client request handle");
      break;
    }
    if (TSHttpHdrUrlGet(recp, hdr_loc, &url_loc) != TS_SUCCESS) {
      VERROR("failed to get URI handle");
      break;
    }
    CheckCacheable(arg.txn, url_loc, recp);
    TSHandleMLocRelease(recp, hdr_loc, url_loc);
    TSHandleMLocRelease(recp, TS_NULL_MLOC, hdr_loc);
    break;
  }

  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE: {
    int status;

    if (TSHttpTxnCacheLookupStatusGet(arg.txn, &status) == TS_SUCCESS && status != TS_CACHE_LOOKUP_HIT_FRESH) {
      // This transaction is going to be a cache miss, so intercept it.
      VDEBUG("intercepting origin server request for txn=%p", arg.txn);
      TSHttpTxnServerIntercept(TSContCreate(GeneratorInterceptHook, TSMutexCreate()), arg.txn);
    }
    break;
  }

  default:
    VERROR("unexpected event %s (%d)", TSHttpEventNameLookup(event), event);
    break;
  }

  TSHttpTxnReenable(arg.txn, TS_EVENT_HTTP_CONTINUE);
  return TS_EVENT_NONE;
}

static void
GeneratorInitialize()
{
  TxnHook = TSContCreate(GeneratorTxnHook, nullptr);
  memset(GeneratorData, 'x', sizeof(GeneratorData));

  if (TSStatFindName("generator.response_bytes", &StatCountBytes) == TS_ERROR) {
    StatCountBytes = TSStatCreate("generator.response_bytes", TS_RECORDDATATYPE_COUNTER, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  }

  if (TSStatFindName("generator.response_count", &StatCountResponses) == TS_ERROR) {
    StatCountResponses =
      TSStatCreate("generator.response_count", TS_RECORDDATATYPE_COUNTER, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_COUNT);
  }
}

void
TSPluginInit(int /* argc */, const char * /* argv */[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = (char *)PLUGIN;
  info.vendor_name   = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    VERROR("plugin registration failed\n");
  }

  GeneratorInitialize();

  // We want to check early on if the request is cacheable or not, and if it's not cacheable,
  // we benefit signifciantly from turning off the cache completely.
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TxnHook);

  // Wait until after the cache lookup to decide whether to
  // intercept a request. For cache hits we will never intercept.
  TSHttpHookAdd(TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, TxnHook);
}

TSReturnCode
TSRemapInit(TSRemapInterface * /* api_info */, char * /* errbuf */, int /* errbuf_size */)
{
  GeneratorInitialize();
  return TS_SUCCESS;
}

TSRemapStatus
TSRemapDoRemap(void * /* ih */, TSHttpTxn txn, TSRemapRequestInfo *rri)
{
  // Check if we should turn off the cache before doing anything else ...
  CheckCacheable(txn, rri->requestUrl, rri->requestBufp);
  TSHttpTxnHookAdd(txn, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, TxnHook);
  return TSREMAP_NO_REMAP; // This plugin never rewrites anything.
}

TSReturnCode
TSRemapNewInstance(int /* argc */, char * /* argv */[], void **ih, char * /* errbuf ATS_UNUSED */, int /* errbuf_size ATS_UNUSED */)
{
  *ih = nullptr;
  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void * /* ih */)
{
}
