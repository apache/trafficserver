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

#include <cstdlib>
#include <cstdio>
#include <cstdio>
#include <strings.h>
#include <sstream>
#include <cstring>
#include <atomic>
#include <memory>
#include <getopt.h>
#include <cstdint>
#include <cinttypes>
#include <string_view>
#include <unistd.h>

#include <ts/ts.h>
#include "ts/experimental.h"
#include "tscore/ink_defs.h"
#include "tscpp/util/PostScript.h"
#include "tscpp/util/TextView.h"
#include "Cleanup.h"

namespace
{
struct BodyBuilder {
  atscppapi::TSContUniqPtr transform_connp;
  atscppapi::TSIOBufferUniqPtr output_buffer;
  // It's important that output_reader comes after output_buffer so it will be deleted first.
  atscppapi::TSIOBufferReaderUniqPtr output_reader;
  TSVIO output_vio   = nullptr;
  bool wrote_prebody = false;
  bool wrote_body    = false;
  bool hdr_ready     = false;
  std::atomic_flag wrote_postbody;

  int64_t nbytes = 0;
};

struct XDebugTxnAuxData {
  std::unique_ptr<BodyBuilder> body_builder;
  unsigned xheaders = 0;
};

atscppapi::TxnAuxMgrData mgrData;

using AuxDataMgr = atscppapi::TxnAuxDataMgr<XDebugTxnAuxData, mgrData>;

} // end anonymous namespace

#include "xdebug_headers.cc"
#include "xdebug_transforms.cc"

static struct {
  const char *str;
  int len;
} xDebugHeader = {nullptr, 0};

enum {
  XHEADER_X_CACHE_KEY      = 1u << 2,
  XHEADER_X_MILESTONES     = 1u << 3,
  XHEADER_X_CACHE          = 1u << 4,
  XHEADER_X_GENERATION     = 1u << 5,
  XHEADER_X_TRANSACTION_ID = 1u << 6,
  XHEADER_X_DUMP_HEADERS   = 1u << 7,
  XHEADER_X_REMAP          = 1u << 8,
  XHEADER_X_PROBE_HEADERS  = 1u << 9,
  XHEADER_X_PSELECT_KEY    = 1u << 10,
  XHEADER_X_CACHE_INFO     = 1u << 11,
};

static TSCont XInjectHeadersCont  = nullptr;
static TSCont XDeleteDebugHdrCont = nullptr;

// Return the length of a string literal.
template <int N>
unsigned
lengthof(const char (&)[N])
{
  return N - 1;
}

static TSMLoc
FindOrMakeHdrField(TSMBuffer buffer, TSMLoc hdr, const char *name, unsigned len)
{
  TSMLoc field;

  field = TSMimeHdrFieldFind(buffer, hdr, name, len);
  if (field == TS_NULL_MLOC) {
    if (TSMimeHdrFieldCreateNamed(buffer, hdr, name, len, &field) == TS_SUCCESS) {
      TSReleaseAssert(TSMimeHdrFieldAppend(buffer, hdr, field) == TS_SUCCESS);
    }
  }

  return field;
}

static void
InjectGenerationHeader(TSHttpTxn txn, TSMBuffer buffer, TSMLoc hdr)
{
  TSMgmtInt value;
  TSMLoc dst = TS_NULL_MLOC;

  if (TSHttpTxnConfigIntGet(txn, TS_CONFIG_HTTP_CACHE_GENERATION, &value) == TS_SUCCESS) {
    dst = FindOrMakeHdrField(buffer, hdr, "X-Cache-Generation", lengthof("X-Cache-Generation"));
    if (dst != TS_NULL_MLOC) {
      TSReleaseAssert(TSMimeHdrFieldValueInt64Set(buffer, hdr, dst, -1 /* idx */, value) == TS_SUCCESS);
    }
  }

  if (dst != TS_NULL_MLOC) {
    TSHandleMLocRelease(buffer, hdr, dst);
  }
}

static void
InjectCacheKeyHeader(TSHttpTxn txn, TSMBuffer buffer, TSMLoc hdr)
{
  TSMLoc url = TS_NULL_MLOC;
  TSMLoc dst = TS_NULL_MLOC;

  struct {
    char *ptr;
    int len;
  } strval = {nullptr, 0};

  TSDebug("xdebug", "attempting to inject X-Cache-Key header");

  if (TSUrlCreate(buffer, &url) != TS_SUCCESS) {
    goto done;
  }

  if (TSHttpTxnCacheLookupUrlGet(txn, buffer, url) != TS_SUCCESS) {
    goto done;
  }

  strval.ptr = TSUrlStringGet(buffer, url, &strval.len);
  if (strval.ptr == nullptr || strval.len == 0) {
    goto done;
  }

  // Create a new response header field.
  dst = FindOrMakeHdrField(buffer, hdr, "X-Cache-Key", lengthof("X-Cache-Key"));
  if (dst == TS_NULL_MLOC) {
    goto done;
  }

  // Now copy the cache lookup URL into the response header.
  TSReleaseAssert(TSMimeHdrFieldValueStringInsert(buffer, hdr, dst, -1 /* idx */, strval.ptr, strval.len) == TS_SUCCESS);

done:
  if (dst != TS_NULL_MLOC) {
    TSHandleMLocRelease(buffer, hdr, dst);
  }

  if (url != TS_NULL_MLOC) {
    TSHandleMLocRelease(buffer, TS_NULL_MLOC, url);
  }

  TSfree(strval.ptr);
}

static void
InjectCacheInfoHeader(TSHttpTxn txn, TSMBuffer buffer, TSMLoc hdr)
{
  TSMLoc dst = TS_NULL_MLOC;
  TSMgmtInt volume;
  const char *path;

  TSDebug("xdebug", "attempting to inject X-Cache-Info header");

  if ((path = TSHttpTxnCacheDiskPathGet(txn, nullptr)) == nullptr) {
    goto done;
  }

  if (TSHttpTxnInfoIntGet(txn, TS_TXN_INFO_CACHE_VOLUME, &volume) != TS_SUCCESS) {
    goto done;
  }

  // Create a new response header field.
  dst = FindOrMakeHdrField(buffer, hdr, "X-Cache-Info", lengthof("X-Cache-Info"));
  if (dst == TS_NULL_MLOC) {
    goto done;
  }

  char value[1024];
  snprintf(value, sizeof(value), "path=%s; volume=%" PRId64, path, volume);

  // Now copy the CacheDisk info into the response header.
  TSReleaseAssert(TSMimeHdrFieldValueStringInsert(buffer, hdr, dst, -1 /* idx */, value, std::strlen(value)) == TS_SUCCESS);

done:
  if (dst != TS_NULL_MLOC) {
    TSHandleMLocRelease(buffer, hdr, dst);
  }
}

static void
InjectCacheHeader(TSHttpTxn txn, TSMBuffer buffer, TSMLoc hdr)
{
  TSMLoc dst = TS_NULL_MLOC;
  int status;

  static const char *names[] = {
    "miss",      // TS_CACHE_LOOKUP_MISS,
    "hit-stale", // TS_CACHE_LOOKUP_HIT_STALE,
    "hit-fresh", // TS_CACHE_LOOKUP_HIT_FRESH,
    "skipped"    // TS_CACHE_LOOKUP_SKIPPED
  };

  TSDebug("xdebug", "attempting to inject X-Cache header");

  // Create a new response header field.
  dst = FindOrMakeHdrField(buffer, hdr, "X-Cache", lengthof("X-Cache"));
  if (dst == TS_NULL_MLOC) {
    goto done;
  }

  if (TSHttpTxnCacheLookupStatusGet(txn, &status) == TS_ERROR) {
    // If the cache lookup hasn't happened yes, TSHttpTxnCacheLookupStatusGet will fail.
    TSReleaseAssert(TSMimeHdrFieldValueStringInsert(buffer, hdr, dst, -1 /* idx */, "none", 4) == TS_SUCCESS);
  } else {
    const char *msg = (status < 0 || status >= static_cast<int>(countof(names))) ? "unknown" : names[status];

    TSReleaseAssert(TSMimeHdrFieldValueStringInsert(buffer, hdr, dst, -1 /* idx */, msg, -1) == TS_SUCCESS);
  }

done:
  if (dst != TS_NULL_MLOC) {
    TSHandleMLocRelease(buffer, hdr, dst);
  }
}

struct milestone {
  TSMilestonesType mstype;
  const char *msname;
};

static void
InjectMilestonesHeader(TSHttpTxn txn, TSMBuffer buffer, TSMLoc hdr)
{
  // The set of milestones we can publish. Some milestones happen after
  // this hook, so we skip those ...
  static const milestone milestones[] = {
    {TS_MILESTONE_UA_BEGIN, "UA-BEGIN"},
    {TS_MILESTONE_UA_FIRST_READ, "UA-FIRST-READ"},
    {TS_MILESTONE_UA_READ_HEADER_DONE, "UA-READ-HEADER-DONE"},
    {TS_MILESTONE_UA_BEGIN_WRITE, "UA-BEGIN-WRITE"},
    {TS_MILESTONE_UA_CLOSE, "UA-CLOSE"},
    {TS_MILESTONE_SERVER_FIRST_CONNECT, "SERVER-FIRST-CONNECT"},
    {TS_MILESTONE_SERVER_CONNECT, "SERVER-CONNECT"},
    {TS_MILESTONE_SERVER_CONNECT_END, "SERVER-CONNECT-END"},
    {TS_MILESTONE_SERVER_BEGIN_WRITE, "SERVER-BEGIN-WRITE"},
    {TS_MILESTONE_SERVER_FIRST_READ, "SERVER-FIRST-READ"},
    {TS_MILESTONE_SERVER_READ_HEADER_DONE, "SERVER-READ-HEADER-DONE"},
    {TS_MILESTONE_SERVER_CLOSE, "SERVER-CLOSE"},
    {TS_MILESTONE_CACHE_OPEN_READ_BEGIN, "CACHE-OPEN-READ-BEGIN"},
    {TS_MILESTONE_CACHE_OPEN_READ_END, "CACHE-OPEN-READ-END"},
    {TS_MILESTONE_CACHE_OPEN_WRITE_BEGIN, "CACHE-OPEN-WRITE-BEGIN"},
    {TS_MILESTONE_CACHE_OPEN_WRITE_END, "CACHE-OPEN-WRITE-END"},
    {TS_MILESTONE_DNS_LOOKUP_BEGIN, "DNS-LOOKUP-BEGIN"},
    {TS_MILESTONE_DNS_LOOKUP_END, "DNS-LOOKUP-END"},
    // SM_START is deliberately excluded because as all the times are printed relative to it
    // it would always be zero.
    {TS_MILESTONE_SM_FINISH, "SM-FINISH"},
    {TS_MILESTONE_PLUGIN_ACTIVE, "PLUGIN-ACTIVE"},
    {TS_MILESTONE_PLUGIN_TOTAL, "PLUGIN-TOTAL"},
  };

  TSMLoc dst = TS_NULL_MLOC;
  TSHRTime epoch;

  // TS_MILESTONE_SM_START is stamped when the HTTP transaction is born. The slow
  // log feature publishes the other times as seconds relative to this epoch. Let's
  // do the same.
  TSHttpTxnMilestoneGet(txn, TS_MILESTONE_SM_START, &epoch);

  // Create a new response header field.
  dst = FindOrMakeHdrField(buffer, hdr, "X-Milestones", lengthof("X-Milestones"));
  if (dst == TS_NULL_MLOC) {
    goto done;
  }

  for (unsigned i = 0; i < countof(milestones); ++i) {
    TSHRTime time = 0;
    char hdrval[64];

    // If we got a milestone (it's in nanoseconds), convert it to seconds relative to
    // the start of the transaction. We don't get milestone values for portions of the
    // state machine the request doesn't traverse.
    TSHttpTxnMilestoneGet(txn, milestones[i].mstype, &time);
    if (time > 0) {
      double elapsed = static_cast<double>(time - epoch) / 1000000000.0;
      int len        = snprintf(hdrval, sizeof(hdrval), "%s=%1.9lf", milestones[i].msname, elapsed);

      TSReleaseAssert(TSMimeHdrFieldValueStringInsert(buffer, hdr, dst, -1 /* idx */, hdrval, len) == TS_SUCCESS);
    }
  }

done:
  if (dst != TS_NULL_MLOC) {
    TSHandleMLocRelease(buffer, hdr, dst);
  }
}

static const char NotFound[] = "Not-Found";

// The returned string must be freed with TSfree() unless it is equal to the constant NotFound.
//
static const char *
getRemapUrlStr(TSHttpTxn txnp, TSReturnCode (*remapUrlGetFunc)(TSHttpTxn, TSMLoc *), int &urlStrLen)
{
  TSMLoc urlLoc;

  TSReturnCode rc = remapUrlGetFunc(txnp, &urlLoc);
  if (rc != TS_SUCCESS) {
    urlStrLen = sizeof(NotFound) - 1;
    return NotFound;
  }

  char *urlStr = TSUrlStringGet(nullptr, urlLoc, &urlStrLen);

  // Be defensive.
  if ((urlStrLen == 0) and urlStr) {
    TSError("[xdebug] non-null remap URL string with zero length");
    TSfree(urlStr);
    urlStr = nullptr;
  }

  if (!urlStr) {
    urlStrLen = sizeof(NotFound) - 1;
    return NotFound;
  }

  return urlStr;
}

static void
InjectRemapHeader(TSHttpTxn txn, TSMBuffer buffer, TSMLoc hdr)
{
  TSMLoc dst = FindOrMakeHdrField(buffer, hdr, "X-Remap", lengthof("X-Remap"));

  if (TS_NULL_MLOC != dst) {
    int fromUrlStrLen, toUrlStrLen;
    const char *fromUrlStr = getRemapUrlStr(txn, TSRemapFromUrlGet, fromUrlStrLen);
    const char *toUrlStr   = getRemapUrlStr(txn, TSRemapToUrlGet, toUrlStrLen);

    char buf[2048];
    int len = snprintf(buf, sizeof(buf), "from=%*s, to=%*s", fromUrlStrLen, fromUrlStr, toUrlStrLen, toUrlStr);

    if (fromUrlStr != NotFound) {
      TSfree(const_cast<char *>(fromUrlStr));
    }
    if (toUrlStr != NotFound) {
      TSfree(const_cast<char *>(toUrlStr));
    }

    TSReleaseAssert(TSMimeHdrFieldValueStringInsert(buffer, hdr, dst, -1 /* idx */, buf, len) == TS_SUCCESS);
    TSHandleMLocRelease(buffer, hdr, dst);
  }
}

static void
InjectOriginalContentTypeHeader(TSHttpTxn txn, TSMBuffer buffer, TSMLoc hdr)
{
  TSMLoc ct_field = TSMimeHdrFieldFind(buffer, hdr, TS_MIME_FIELD_CONTENT_TYPE, TS_MIME_LEN_CONTENT_TYPE);
  if (TS_NULL_MLOC != ct_field) {
    int original_content_type_len     = 0;
    const char *original_content_type = TSMimeHdrFieldValueStringGet(buffer, hdr, ct_field, -1, &original_content_type_len);
    if (original_content_type != nullptr) {
      TSMLoc dst = FindOrMakeHdrField(buffer, hdr, "X-Original-Content-Type", lengthof("X-Original-Content-Type"));
      TSReleaseAssert(TS_NULL_MLOC != dst);
      TSReleaseAssert(TSMimeHdrFieldValueStringInsert(buffer, hdr, dst, -1 /* idx */, original_content_type,
                                                      original_content_type_len) == TS_SUCCESS);
    }
  } else {
    if (TSMimeHdrFieldCreateNamed(buffer, hdr, TS_MIME_FIELD_CONTENT_TYPE, TS_MIME_LEN_CONTENT_TYPE, &ct_field) == TS_SUCCESS) {
      TSReleaseAssert(TSMimeHdrFieldAppend(buffer, hdr, ct_field) == TS_SUCCESS);
    }
  }

  TSMimeHdrFieldValuesClear(buffer, hdr, ct_field);
  TSReleaseAssert(TSMimeHdrFieldValueStringSet(buffer, hdr, ct_field, -1, "text/plain", lengthof("text/plain")) == TS_SUCCESS);
}

static void
InjectTxnUuidHeader(TSHttpTxn txn, TSMBuffer buffer, TSMLoc hdr)
{
  TSMLoc dst = FindOrMakeHdrField(buffer, hdr, "X-Transaction-ID", lengthof("X-Transaction-ID"));

  if (TS_NULL_MLOC != dst) {
    char buf[TS_UUID_STRING_LEN + 22]; // Padded for int64_t (20) + 1 ('-') + 1 ('\0')
    TSUuid uuid = TSProcessUuidGet();
    int len     = snprintf(buf, sizeof(buf), "%s-%" PRIu64 "", TSUuidStringGet(uuid), TSHttpTxnIdGet(txn));

    TSReleaseAssert(TSMimeHdrFieldValueStringInsert(buffer, hdr, dst, -1 /* idx */, buf, len) == TS_SUCCESS);
    TSHandleMLocRelease(buffer, hdr, dst);
  }
}

static void
InjectParentSelectionKeyHeader(TSHttpTxn txn, TSMBuffer buffer, TSMLoc hdr)
{
  TSMLoc url = TS_NULL_MLOC;
  TSMLoc dst = TS_NULL_MLOC;

  struct {
    char *ptr;
    int len;
  } strval = {nullptr, 0};

  TSDebug("xdebug", "attempting to inject X-ParentSelection-Key header");

  if (TSUrlCreate(buffer, &url) != TS_SUCCESS) {
    goto done;
  }

  if (TSHttpTxnParentSelectionUrlGet(txn, buffer, url) != TS_SUCCESS) {
    goto done;
  }

  strval.ptr = TSUrlStringGet(buffer, url, &strval.len);
  if (strval.ptr == nullptr || strval.len == 0) {
    goto done;
  }

  // Create a new response header field.
  dst = FindOrMakeHdrField(buffer, hdr, "X-ParentSelection-Key", lengthof("X-ParentSelection-Key"));
  if (dst == TS_NULL_MLOC) {
    goto done;
  }

  // Now copy the parent selection lookup URL into the response header.
  TSReleaseAssert(TSMimeHdrFieldValueStringInsert(buffer, hdr, dst, -1 /* idx */, strval.ptr, strval.len) == TS_SUCCESS);

done:
  if (dst != TS_NULL_MLOC) {
    TSHandleMLocRelease(buffer, hdr, dst);
  }

  if (url != TS_NULL_MLOC) {
    TSHandleMLocRelease(buffer, TS_NULL_MLOC, url);
  }

  TSfree(strval.ptr);
}

static int
XInjectResponseHeaders(TSCont /* contp */, TSEvent event, void *edata)
{
  TSHttpTxn txn = static_cast<TSHttpTxn>(edata);
  TSMBuffer buffer;
  TSMLoc hdr;

  TSReleaseAssert(event == TS_EVENT_HTTP_SEND_RESPONSE_HDR);

  unsigned xheaders = AuxDataMgr::data(txn).xheaders;
  if (xheaders == 0) {
    goto done;
  }

  if (TSHttpTxnClientRespGet(txn, &buffer, &hdr) == TS_ERROR) {
    goto done;
  }

  if (xheaders & XHEADER_X_CACHE_KEY) {
    InjectCacheKeyHeader(txn, buffer, hdr);
  }

  if (xheaders & XHEADER_X_CACHE_INFO) {
    InjectCacheInfoHeader(txn, buffer, hdr);
  }

  if (xheaders & XHEADER_X_CACHE) {
    InjectCacheHeader(txn, buffer, hdr);
  }

  if (xheaders & XHEADER_X_MILESTONES) {
    InjectMilestonesHeader(txn, buffer, hdr);
  }

  if (xheaders & XHEADER_X_GENERATION) {
    InjectGenerationHeader(txn, buffer, hdr);
  }

  if (xheaders & XHEADER_X_TRANSACTION_ID) {
    InjectTxnUuidHeader(txn, buffer, hdr);
  }

  if (xheaders & XHEADER_X_REMAP) {
    InjectRemapHeader(txn, buffer, hdr);
  }

  // intentionally placed after all injected headers.

  if (xheaders & XHEADER_X_DUMP_HEADERS) {
    log_headers(txn, buffer, hdr, "ClientResponse");
  }

  if (xheaders & XHEADER_X_PROBE_HEADERS) {
    InjectOriginalContentTypeHeader(txn, buffer, hdr);
    BodyBuilder *data = AuxDataMgr::data(txn).body_builder.get();
    TSDebug("xdebug_transform", "XInjectResponseHeaders(): client resp header ready");
    if (data == nullptr) {
      TSHttpTxnReenable(txn, TS_EVENT_HTTP_ERROR);
      return TS_ERROR;
    }
    data->hdr_ready = true;
    writePostBody(txn, data);
  }

  if (xheaders & XHEADER_X_PSELECT_KEY) {
    InjectParentSelectionKeyHeader(txn, buffer, hdr);
  }

done:
  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return TS_EVENT_NONE;
}

static bool
isFwdFieldValue(std::string_view value, intmax_t &fwdCnt)
{
  static const ts::TextView paramName("fwd");

  if (value.size() < paramName.size()) {
    return false;
  }

  ts::TextView tvVal(value);

  if (strcasecmp(paramName, tvVal.prefix(paramName.size())) != 0) {
    return false;
  }

  tvVal.remove_prefix(paramName.size());

  if (tvVal.size() == 0) {
    // Value is 'fwd' with no '=<count>'.
    fwdCnt = -1;
    return true;
  }

  const char httpSpace[] = " \t";

  tvVal.ltrim(httpSpace);

  if (tvVal[0] != '=') {
    return false;
  }

  tvVal.remove_prefix(1);
  tvVal.ltrim(httpSpace);

  size_t sz  = tvVal.size();
  intmax_t i = ts::svtoi(tvVal, &tvVal);

  if ((tvVal.size() != sz) or (i < 0)) {
    // There were crud characters after the number, or the number was negative.
    return false;
  }

  fwdCnt = i;

  return true;
}

// Scan the client request headers and determine which debug headers they
// want in the response.
static int
XScanRequestHeaders(TSCont /* contp */, TSEvent event, void *edata)
{
  TSHttpTxn txn     = static_cast<TSHttpTxn>(edata);
  unsigned xheaders = 0;
  intmax_t fwdCnt   = 0;
  TSMLoc field, next;
  TSMBuffer buffer;
  TSMLoc hdr;

  // Make sure TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE) is called before exiting function.
  //
  ts::PostScript ps([=]() -> void { TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE); });

  TSReleaseAssert(event == TS_EVENT_HTTP_READ_REQUEST_HDR);

  if (TSHttpTxnClientReqGet(txn, &buffer, &hdr) == TS_ERROR) {
    return TS_EVENT_NONE;
  }

  TSDebug("xdebug", "scanning for %s header values", xDebugHeader.str);

  // Walk the X-Debug header values and determine what to inject into the response.
  field = TSMimeHdrFieldFind(buffer, hdr, xDebugHeader.str, xDebugHeader.len);
  while (field != TS_NULL_MLOC) {
    int count = TSMimeHdrFieldValuesCount(buffer, hdr, field);

    for (int i = 0; i < count; ++i) {
      const char *value;
      int vsize;

      value = TSMimeHdrFieldValueStringGet(buffer, hdr, field, i, &vsize);
      if (value == nullptr || vsize == 0) {
        continue;
      }

#define header_field_eq(name, vptr, vlen) (((int)lengthof(name) == vlen) && (strncasecmp(name, vptr, vlen) == 0))

      if (header_field_eq("x-cache-key", value, vsize)) {
        xheaders |= XHEADER_X_CACHE_KEY;
      } else if (header_field_eq("x-cache-info", value, vsize)) {
        xheaders |= XHEADER_X_CACHE_INFO;
      } else if (header_field_eq("x-milestones", value, vsize)) {
        xheaders |= XHEADER_X_MILESTONES;
      } else if (header_field_eq("x-cache", value, vsize)) {
        xheaders |= XHEADER_X_CACHE;
      } else if (header_field_eq("x-cache-generation", value, vsize)) {
        xheaders |= XHEADER_X_GENERATION;
      } else if (header_field_eq("x-transaction-id", value, vsize)) {
        xheaders |= XHEADER_X_TRANSACTION_ID;
      } else if (header_field_eq("x-remap", value, vsize)) {
        xheaders |= XHEADER_X_REMAP;
      } else if (header_field_eq("via", value, vsize)) {
        // If the client requests the Via header, enable verbose Via debugging for this transaction.
        TSHttpTxnConfigIntSet(txn, TS_CONFIG_HTTP_INSERT_RESPONSE_VIA_STR, 3);
      } else if (header_field_eq("diags", value, vsize)) {
        // Enable diagnostics for DebugTxn()'s only
        TSHttpTxnDebugSet(txn, 1);

      } else if (header_field_eq("probe", value, vsize)) {
        xheaders |= XHEADER_X_PROBE_HEADERS;

        auto &auxData = AuxDataMgr::data(txn);

        // prefix request headers and postfix response headers
        BodyBuilder *data = new BodyBuilder();
        auxData.body_builder.reset(data);

        TSVConn connp = TSTransformCreate(body_transform, txn);
        data->transform_connp.reset(connp);
        TSContDataSet(connp, txn);
        TSHttpTxnHookAdd(txn, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);

        // disable writing to cache because we are injecting data into the body.
        TSHttpTxnReqCacheableSet(txn, 0);
        TSHttpTxnRespCacheableSet(txn, 0);
        TSHttpTxnServerRespNoStoreSet(txn, 1);
        TSHttpTxnTransformedRespCache(txn, 0);
        TSHttpTxnUntransformedRespCache(txn, 0);

      } else if (header_field_eq("x-parentselection-key", value, vsize)) {
        xheaders |= XHEADER_X_PSELECT_KEY;

      } else if (isFwdFieldValue(std::string_view(value, vsize), fwdCnt)) {
        if (fwdCnt > 0) {
          // Decrement forward count in X-Debug header.
          char newVal[128];
          snprintf(newVal, sizeof(newVal), "fwd=%" PRIiMAX, fwdCnt - 1);
          TSMimeHdrFieldValueStringSet(buffer, hdr, field, i, newVal, std::strlen(newVal));
        }
      } else {
        TSDebug("xdebug", "ignoring unrecognized debug tag '%.*s'", vsize, value);
      }
    }

#undef header_field_eq

    // Get the next duplicate.
    next = TSMimeHdrFieldNextDup(buffer, hdr, field);

    // Now release our reference.
    TSHandleMLocRelease(buffer, hdr, field);

    // And go to the next field.
    field = next;
  }

  if (xheaders) {
    TSDebug("xdebug", "adding response hook for header mask %p and forward count %" PRIiMAX, reinterpret_cast<void *>(xheaders),
            fwdCnt);
    TSHttpTxnHookAdd(txn, TS_HTTP_SEND_RESPONSE_HDR_HOOK, XInjectHeadersCont);
    AuxDataMgr::data(txn).xheaders = xheaders;

    if (fwdCnt == 0) {
      // X-Debug header has to be deleted, but not too soon for other plugins to see it.
      TSHttpTxnHookAdd(txn, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, XDeleteDebugHdrCont);
    }
  }

  return TS_EVENT_NONE;
}

// Continuation function to delete the x-debug header.
//
static int
XDeleteDebugHdr(TSCont /* contp */, TSEvent event, void *edata)
{
  TSHttpTxn txn = static_cast<TSHttpTxn>(edata);
  TSMLoc hdr, field;
  TSMBuffer buffer;

  // Make sure TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE) is called before exiting function.
  //
  ts::PostScript ps([=]() -> void { TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE); });

  TSReleaseAssert(event == TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE);

  if (TSHttpTxnClientReqGet(txn, &buffer, &hdr) == TS_ERROR) {
    return TS_EVENT_NONE;
  }

  field = TSMimeHdrFieldFind(buffer, hdr, xDebugHeader.str, xDebugHeader.len);
  if (field == TS_NULL_MLOC) {
    return TS_EVENT_NONE;
  }

  if (TSMimeHdrFieldDestroy(buffer, hdr, field) == TS_ERROR) {
    TSError("Failure destroying %s header", xDebugHeader.str);
  }

  TSHandleMLocRelease(buffer, hdr, field);

  return TS_EVENT_NONE;
}

void
TSPluginInit(int argc, const char *argv[])
{
  static const struct option longopt[] = {{const_cast<char *>("header"), required_argument, nullptr, 'h'},
                                          {nullptr, no_argument, nullptr, '\0'}};
  TSPluginRegistrationInfo info;

  info.plugin_name   = (char *)"xdebug";
  info.vendor_name   = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[xdebug] Plugin registration failed");
  }

  // Parse the arguments
  while (true) {
    int opt = getopt_long(argc, const_cast<char *const *>(argv), "", longopt, nullptr);

    switch (opt) {
    case 'h':
      xDebugHeader.str = TSstrdup(optarg);
      break;
    }

    if (opt == -1) {
      break;
    }
  }

  if (nullptr == xDebugHeader.str) {
    xDebugHeader.str = TSstrdup("X-Debug"); // We malloc this, for consistency for future plugin unload events
  }
  xDebugHeader.len = strlen(xDebugHeader.str);

  // Make xDebugHeader available to other plugins, as a C-style string.
  //
  int idx = -1;
  TSReleaseAssert(TSUserArgIndexReserve(TS_USER_ARGS_GLB, "XDebugHeader", "XDebug header name", &idx) == TS_SUCCESS);
  TSReleaseAssert(idx >= 0);
  TSUserArgSet(nullptr, idx, const_cast<char *>(xDebugHeader.str));

  AuxDataMgr::init("xdebug");

  // Setup the global hook
  TSReleaseAssert(XInjectHeadersCont = TSContCreate(XInjectResponseHeaders, nullptr));
  TSReleaseAssert(XDeleteDebugHdrCont = TSContCreate(XDeleteDebugHdr, nullptr));
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(XScanRequestHeaders, nullptr));

  gethostname(Hostname, 1024);
}
