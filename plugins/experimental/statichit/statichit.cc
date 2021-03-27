/** @file

  Static Hit Content Serving

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

#include <cerrno>
#include <cinttypes>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <fstream>
#include <sstream>

#include <string>
#include <getopt.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include "ts/ts.h"
#include "ts/remap.h"

constexpr char PLUGIN[] = "statichit";

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

static int StatCountBytes     = -1;
static int StatCountResponses = -1;

static int StaticHitInterceptHook(TSCont contp, TSEvent event, void *edata);
static int StaticHitTxnHook(TSCont contp, TSEvent event, void *edata);

struct StaticHitConfig {
  explicit StaticHitConfig(const std::string &filePath, const std::string &mimeType) : filePath(filePath), mimeType(mimeType) {}

  ~StaticHitConfig() {}

  std::string filePath;
  std::string mimeType;

  int successCode = 200;
  int failureCode = 404;
  int maxAge      = 0;
};

struct StaticHitRequest;

union argument_type {
  void *ptr;
  intptr_t ecode;
  TSVConn vc;
  TSVIO vio;
  TSHttpTxn txn;
  StaticHitRequest *trq;

  argument_type(void *_p) : ptr(_p) {}
};

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

struct StaticHitHttpHeader {
  TSMBuffer buffer;
  TSMLoc header;
  TSHttpParser parser;

  StaticHitHttpHeader()
  {
    this->buffer = TSMBufferCreate();
    this->header = TSHttpHdrCreate(this->buffer);
    this->parser = TSHttpParserCreate();
  }

  ~StaticHitHttpHeader()
  {
    if (this->parser) {
      TSHttpParserDestroy(this->parser);
    }

    TSHttpHdrDestroy(this->buffer, this->header);
    TSHandleMLocRelease(this->buffer, TS_NULL_MLOC, this->header);
    TSMBufferDestroy(this->buffer);
  }
};

struct StaticHitRequest {
  StaticHitRequest() {}

  off_t nbytes        = 0; // Number of bytes to generate.
  unsigned maxAge     = 0; // Max age for cache responses.
  unsigned statusCode = 200;
  IOChannel readio;
  IOChannel writeio;
  StaticHitHttpHeader rqheader;

  std::string body;
  std::string mimeType;

  static StaticHitRequest *
  createStaticHitRequest(StaticHitConfig *tc)
  {
    StaticHitRequest *shr = new StaticHitRequest;
    std::ifstream ifstr;

    ifstr.open(tc->filePath);
    if (!ifstr) {
      shr->statusCode = tc->failureCode;
      return shr;
    }

    std::stringstream sstr;
    sstr << ifstr.rdbuf();
    shr->body       = sstr.str();
    shr->nbytes     = shr->body.size();
    shr->mimeType   = tc->mimeType;
    shr->statusCode = tc->successCode;
    shr->maxAge     = tc->maxAge;

    return shr;
  }

  ~StaticHitRequest() = default;
};

// Destroy a StaticHitRequest, including the per-txn continuation.
static void
StaticHitRequestDestroy(StaticHitRequest *trq, TSVIO vio, TSCont contp)
{
  if (vio) {
    TSVConnClose(TSVIOVConnGet(vio));
  }

  TSContDestroy(contp);
  delete trq;
}

// NOTE: This will always append a new "field_name: value"
static void
HeaderFieldDateSet(const StaticHitHttpHeader &http, const char *field_name, int64_t field_len, time_t value)
{
  TSMLoc field;

  TSMimeHdrFieldCreateNamed(http.buffer, http.header, field_name, field_len, &field);
  TSMimeHdrFieldValueDateSet(http.buffer, http.header, field, value);
  TSMimeHdrFieldAppend(http.buffer, http.header, field);
  TSHandleMLocRelease(http.buffer, http.header, field);
}

// NOTE: This will always append a new "field_name: value"
static void
HeaderFieldIntSet(const StaticHitHttpHeader &http, const char *field_name, int64_t field_len, int64_t value)
{
  TSMLoc field;

  TSMimeHdrFieldCreateNamed(http.buffer, http.header, field_name, field_len, &field);
  TSMimeHdrFieldValueInt64Set(http.buffer, http.header, field, -1, value);
  TSMimeHdrFieldAppend(http.buffer, http.header, field);
  TSHandleMLocRelease(http.buffer, http.header, field);
}

// NOTE: This will always append a new "field_name: value"
static void
HeaderFieldStringSet(const StaticHitHttpHeader &http, const char *field_name, int64_t field_len, const char *value)
{
  TSMLoc field;

  TSMimeHdrFieldCreateNamed(http.buffer, http.header, field_name, field_len, &field);
  TSMimeHdrFieldValueStringSet(http.buffer, http.header, field, -1, value, -1);
  TSMimeHdrFieldAppend(http.buffer, http.header, field);
  TSHandleMLocRelease(http.buffer, http.header, field);
}

static TSReturnCode
WriteResponseHeader(StaticHitRequest *trq, TSCont contp, TSHttpStatus status)
{
  StaticHitHttpHeader response;

  VDEBUG("writing response header");

  if (TSHttpHdrTypeSet(response.buffer, response.header, TS_HTTP_TYPE_RESPONSE) != TS_SUCCESS) {
    VERROR("failed to set type");
    return TS_ERROR;
  }
  if (TSHttpHdrVersionSet(response.buffer, response.header, TS_HTTP_VERSION(1, 1)) != TS_SUCCESS) {
    VERROR("failed to set HTTP version");
    return TS_ERROR;
  }
  if (TSHttpHdrStatusSet(response.buffer, response.header, status) != TS_SUCCESS) {
    VERROR("failed to set HTTP status");
    return TS_ERROR;
  }

  TSHttpHdrReasonSet(response.buffer, response.header, TSHttpHdrReasonLookup(status), -1);

  if (status == TS_HTTP_STATUS_OK) {
    // Set the Content-Length header.
    HeaderFieldIntSet(response, TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH, trq->nbytes);

    // Set the Cache-Control header.
    if (trq->maxAge > 0) {
      char buf[64];

      snprintf(buf, sizeof(buf), "max-age=%u", trq->maxAge);
      HeaderFieldStringSet(response, TS_MIME_FIELD_CACHE_CONTROL, TS_MIME_LEN_CACHE_CONTROL, buf);
      HeaderFieldDateSet(response, TS_MIME_FIELD_LAST_MODIFIED, TS_MIME_LEN_LAST_MODIFIED, time(nullptr));

    } else {
      HeaderFieldStringSet(response, TS_MIME_FIELD_CACHE_CONTROL, TS_MIME_LEN_CACHE_CONTROL, "no-cache");
    }

    HeaderFieldStringSet(response, TS_MIME_FIELD_CONTENT_TYPE, TS_MIME_LEN_CONTENT_TYPE, trq->mimeType.c_str());
  }

  // Write the header to the IO buffer. Set the VIO bytes so that we can get a WRITE_COMPLETE
  // event when this is done.
  int hdrlen = TSHttpHdrLengthGet(response.buffer, response.header);

  TSHttpHdrPrint(response.buffer, response.header, trq->writeio.iobuf);
  TSVIONBytesSet(trq->writeio.vio, hdrlen);
  TSVIOReenable(trq->writeio.vio);

  TSStatIntIncrement(StatCountBytes, hdrlen);

  return TS_SUCCESS;
}

static bool
StaticHitParseRequest(StaticHitRequest *trq)
{
  const char *path;
  int pathsz;

  // Make sure this is a GET request
  path = TSHttpHdrMethodGet(trq->rqheader.buffer, trq->rqheader.header, &pathsz);
  if (path != TS_HTTP_METHOD_GET) {
    VDEBUG("%.*s method is not supported", pathsz, path);
    return false;
  }

  return true;
}

// Handle events from TSHttpTxnServerIntercept. The intercept
// starts with TS_EVENT_NET_ACCEPT, and then continues with
// TSVConn events.
static int
StaticHitInterceptHook(TSCont contp, TSEvent event, void *edata)
{
  VDEBUG("StaticHitInterceptHook: %p ", edata);

  argument_type arg(edata);

  VDEBUG("contp=%p, event=%s (%d), edata=%p", contp, TSHttpEventNameLookup(event), event, arg.ptr);

  switch (event) {
  case TS_EVENT_NET_ACCEPT: {
    // TS_EVENT_NET_ACCEPT will be delivered when the server intercept
    // is set up by the core. We just need to allocate a statichit
    // request state and start reading the VC.
    StaticHitRequest *trq = static_cast<StaticHitRequest *>(TSContDataGet(contp));

    TSStatIntIncrement(StatCountResponses, 1);
    VDEBUG("allocated server intercept statichit trq=%p", trq);

    // This continuation was allocated in StaticHitTxnHook. Reset the
    // data to keep track of this generator request.
    TSContDataSet(contp, trq);

    // Start reading the request from the server intercept VC.
    trq->readio.read(arg.vc, contp);
    VIODEBUG(trq->readio.vio, "started reading statichit request");

    return TS_EVENT_NONE;
  }

  case TS_EVENT_NET_ACCEPT_FAILED: {
    // TS_EVENT_NET_ACCEPT_FAILED will be delivered if the
    // transaction is cancelled before we start tunnelling
    // through the server intercept. One way that this can happen
    // is if the intercept is attached early, and then we serve
    // the document out of cache.

    // There's nothing to do here except nuke the continuation
    // that was allocated in StaticHitTxnHook().

    StaticHitRequest *trq = static_cast<StaticHitRequest *>(TSContDataGet(contp));
    delete trq;

    TSContDestroy(contp);
    return TS_EVENT_NONE;
  }

  case TS_EVENT_VCONN_READ_READY: {
    argument_type cdata           = TSContDataGet(contp);
    StaticHitHttpHeader &rqheader = cdata.trq->rqheader;

    VDEBUG("reading vio=%p vc=%p, trq=%p", arg.vio, TSVIOVConnGet(arg.vio), cdata.trq);

    TSIOBufferBlock blk;
    ssize_t consumed     = 0;
    TSParseResult result = TS_PARSE_CONT;

    for (blk = TSIOBufferReaderStart(cdata.trq->readio.reader); blk; blk = TSIOBufferBlockNext(blk)) {
      const char *ptr;
      const char *end;
      int64_t nbytes;
      TSHttpStatus status = static_cast<TSHttpStatus>(cdata.trq->statusCode);

      ptr = TSIOBufferBlockReadStart(blk, cdata.trq->readio.reader, &nbytes);
      if (ptr == nullptr || nbytes == 0) {
        continue;
      }

      end    = ptr + nbytes;
      result = TSHttpHdrParseReq(rqheader.parser, rqheader.buffer, rqheader.header, &ptr, end);
      switch (result) {
      case TS_PARSE_ERROR:
        // If we got a bad request, just shut it down.
        VDEBUG("bad request on trq=%p, sending an error", cdata.trq);
        StaticHitRequestDestroy(cdata.trq, arg.vio, contp);
        return TS_EVENT_ERROR;

      case TS_PARSE_DONE:
        // Check the response.
        VDEBUG("parsed request on trq=%p, sending a response", cdata.trq);
        if (!StaticHitParseRequest(cdata.trq)) {
          status = TS_HTTP_STATUS_METHOD_NOT_ALLOWED;
        }

        // Start the vconn write.
        cdata.trq->writeio.write(TSVIOVConnGet(arg.vio), contp);
        TSVIONBytesSet(cdata.trq->writeio.vio, 0);

        if (WriteResponseHeader(cdata.trq, contp, status) != TS_SUCCESS) {
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

    if (cdata.trq->nbytes) {
      int64_t nbytes = cdata.trq->nbytes;

      VIODEBUG(arg.vio, "writing %" PRId64 " bytes for trq=%p", nbytes, cdata.trq);
      nbytes = TSIOBufferWrite(cdata.trq->writeio.iobuf, cdata.trq->body.c_str(), nbytes);

      cdata.trq->nbytes -= nbytes;
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

    VIODEBUG(arg.vio, "received EOS or ERROR for trq=%p", cdata.trq);
    StaticHitRequestDestroy(cdata.trq, arg.vio, contp);
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
    if (cdata.trq->nbytes) {
      cdata.trq->writeio.write(TSVIOVConnGet(arg.vio), contp);
      TSVIONBytesSet(cdata.trq->writeio.vio, cdata.trq->nbytes);
    } else {
      VIODEBUG(arg.vio, "TS_EVENT_VCONN_WRITE_COMPLETE %" PRId64 " todo", TSVIONTodoGet(arg.vio));
      StaticHitRequestDestroy(cdata.trq, arg.vio, contp);
    }

    return TS_EVENT_NONE;
  }

  case TS_EVENT_TIMEOUT: {
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

static void
StaticHitSetupIntercept(StaticHitConfig *cfg, TSHttpTxn txn)
{
  StaticHitRequest *req = StaticHitRequest::createStaticHitRequest(cfg);
  if (req == nullptr) {
    VERROR("could not create request for %s", cfg->filePath.c_str());
    return;
  }

  TSCont cnt = TSContCreate(StaticHitInterceptHook, TSMutexCreate());
  TSContDataSet(cnt, req);

  TSHttpTxnServerIntercept(cnt, txn);

  return;
}

// Handle events that occur on the TSHttpTxn.
static int
StaticHitTxnHook(TSCont contp, TSEvent event, void *edata)
{
  argument_type arg(edata);

  VDEBUG("contp=%p, event=%s (%d), edata=%p", contp, TSHttpEventNameLookup(event), event, edata);

  switch (event) {
  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE: {
    int method_length, status;
    TSMBuffer bufp;
    TSMLoc hdr_loc;
    const char *method;

    if (TSHttpTxnCacheLookupStatusGet(arg.txn, &status) != TS_SUCCESS) {
      VERROR("failed to get client request handle");
      goto done;
    }

    if (TSHttpTxnClientReqGet(arg.txn, &bufp, &hdr_loc) != TS_SUCCESS) {
      VERROR("Couldn't retrieve client request header");
      goto done;
    }

    method = TSHttpHdrMethodGet(bufp, hdr_loc, &method_length);
    if (NULL == method) {
      VERROR("Couldn't retrieve client request method");
      goto done;
    }

    if (status != TS_CACHE_LOOKUP_HIT_FRESH || method != TS_HTTP_METHOD_GET) {
      StaticHitSetupIntercept(static_cast<StaticHitConfig *>(TSContDataGet(contp)), arg.txn);
    }

    break;
  }

  default:
    VERROR("unexpected event %s (%d)", TSHttpEventNameLookup(event), event);
    break;
  }

done:
  TSHttpTxnReenable(arg.txn, TS_EVENT_HTTP_CONTINUE);
  return TS_EVENT_NONE;
}

TSReturnCode
TSRemapInit(TSRemapInterface * /* api_info */, char * /* errbuf */, int /* errbuf_size */)
{
  TxnHook = TSContCreate(StaticHitTxnHook, nullptr);

  if (TSStatFindName("statichit.response_bytes", &StatCountBytes) == TS_ERROR) {
    StatCountBytes = TSStatCreate("statichit.response_bytes", TS_RECORDDATATYPE_COUNTER, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  }

  if (TSStatFindName("statichit.response_count", &StatCountResponses) == TS_ERROR) {
    StatCountResponses =
      TSStatCreate("statichit.response_count", TS_RECORDDATATYPE_COUNTER, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_COUNT);
  }
  return TS_SUCCESS;
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  StaticHitConfig *cfg = static_cast<StaticHitConfig *>(ih);

  if (!cfg) {
    VERROR("No remap context available, check code / config");
    TSHttpTxnStatusSet(rh, TS_HTTP_STATUS_INTERNAL_SERVER_ERROR);
    return TSREMAP_NO_REMAP;
  }

  // Anchor to URL specified in remap
  int pathsz;
  TSUrlPathGet(rri->requestBufp, rri->requestUrl, &pathsz);
  if (pathsz > 0) {
    VERROR("Path is not an exact match. Rejecting!");
    TSHttpTxnStatusSet(rh, TS_HTTP_STATUS_NOT_FOUND);
    return TSREMAP_NO_REMAP;
  }

  if (!cfg->maxAge) {
    TSHttpTxnConfigIntSet(rh, TS_CONFIG_HTTP_CACHE_HTTP, 0);
    StaticHitSetupIntercept(static_cast<StaticHitConfig *>(ih), rh);
  } else {
    TSHttpTxnHookAdd(rh, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, TxnHook);
  }
  TSContDataSet(TxnHook, ih);

  return TSREMAP_NO_REMAP; // This plugin never rewrites anything.
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /* errbuf ATS_UNUSED */, int /* errbuf_size ATS_UNUSED */)
{
  static const struct option longopt[] = {
    {"file-path", required_argument, NULL, 'f'},    {"mime-type", required_argument, NULL, 'm'},
    {"max-age", required_argument, NULL, 'a'},      {"failure-code", required_argument, NULL, 'c'},
    {"success-code", required_argument, NULL, 's'}, {NULL, no_argument, NULL, '\0'}};

  std::string filePath;
  std::string mimeType = "text/plain";
  int maxAge = 0, failureCode = 0, successCode = 0;

  // argv contains the "to" and "from" URLs. Skip the first so that the
  // second one poses as the program name.
  --argc;
  ++argv;
  optind = 0;

  while (true) {
    int opt = getopt_long(argc, (char *const *)argv, "f:m:a:c:s:", longopt, NULL);

    switch (opt) {
    case 'f': {
      filePath = std::string(optarg);
    } break;
    case 'm': {
      mimeType = std::string(optarg);
    } break;
    case 'a': {
      maxAge = atoi(optarg);
    } break;
    case 'c': {
      failureCode = atoi(optarg);
    } break;
    case 's': {
      successCode = atoi(optarg);
    } break;
    }

    if (opt == -1) {
      break;
    }
  }

  if (filePath.size() == 0) {
    printf("Need to specify --file-path\n");
    return TS_ERROR;
  }

  if (filePath.find("/") != 0) {
    filePath = std::string(TSConfigDirGet()) + '/' + filePath;
  }

  StaticHitConfig *tc = new StaticHitConfig(filePath, mimeType);
  if (maxAge > 0) {
    tc->maxAge = maxAge;
  }
  if (failureCode > 0) {
    tc->failureCode = failureCode;
  }
  if (successCode > 0) {
    tc->successCode = successCode;
  }

  *ih = static_cast<void *>(tc);

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  StaticHitConfig *tc = static_cast<StaticHitConfig *>(ih);
  delete tc;
}
