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

#include <ts/ts.h>
#include <stdlib.h>
#include <strings.h>

// The name of the debug request header. This should probably be configurable.
#define X_DEBUG_HEADER "X-Debug"

#define XHEADER_X_CACHE_KEY   0x0004u

static int XArgIndex = 0;
static TSCont XInjectHeadersCont = NULL;

// Return the length of a string literal.
template <int N> unsigned
lengthof(const char (&)[N]) {
  return N - 1;
}

static TSMLoc
FindOrMakeHdrField(TSMBuffer buffer, TSMLoc hdr, const char * name, unsigned len)
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
InjectCacheKeyHeader(TSHttpTxn txn, TSMBuffer buffer, TSMLoc hdr)
{
  TSMLoc url = TS_NULL_MLOC;
  TSMLoc dst = TS_NULL_MLOC;

  struct { char * ptr; int len; } strval = { NULL, 0 };

  TSDebug("xdebug", "attempting to inject X-Cache-Key header");

  if (TSUrlCreate(buffer, &url) != TS_SUCCESS) {
    goto done;
  }

  if (TSHttpTxnCacheLookupUrlGet(txn, buffer, url) != TS_SUCCESS) {
    goto done;
  }

  strval.ptr = TSUrlStringGet(buffer, url, &strval.len);
  if (strval.ptr == NULL || strval.len == 0) {
    goto done;
  }

  // Create a new response header field.
  dst = FindOrMakeHdrField(buffer, hdr, "X-Cache-Key", lengthof("X-Cache-Key"));
  if (dst == TS_NULL_MLOC) {
    goto done;
  }

  // Now copy the cache lookup URL into the response header.
  TSReleaseAssert(
    TSMimeHdrFieldValueStringInsert(buffer, hdr, dst, 0 /* idx */, strval.ptr, strval.len) == TS_SUCCESS
  );

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
XInjectResponseHeaders(TSCont /* contp */, TSEvent event, void * edata)
{
  TSHttpTxn   txn = (TSHttpTxn)edata;
  intptr_t    xheaders = 0;
  TSMBuffer   buffer;
  TSMLoc      hdr;

  TSReleaseAssert(event == TS_EVENT_HTTP_SEND_RESPONSE_HDR);

  xheaders = (intptr_t)TSHttpTxnArgGet(txn, XArgIndex);
  if (xheaders == 0) {
    goto done;
  }

  if (TSHttpTxnClientRespGet(txn, &buffer, &hdr) == TS_ERROR) {
    goto done;
  }

  if (xheaders & XHEADER_X_CACHE_KEY) {
    InjectCacheKeyHeader(txn, buffer, hdr);
  }

done:
  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return TS_EVENT_NONE;
}

// Scan the client request headers and determine which debug headers they
// want in the response.
static int
XScanRequestHeaders(TSCont /* contp */, TSEvent event, void * edata)
{
  TSHttpTxn   txn = (TSHttpTxn)edata;
  intptr_t    xheaders = 0;
  TSMLoc      field, next;
  TSMBuffer   buffer;
  TSMLoc      hdr;

  TSReleaseAssert(event == TS_EVENT_HTTP_READ_REQUEST_HDR);

  if (TSHttpTxnClientReqGet(txn, &buffer, &hdr) == TS_ERROR) {
    goto done;
  }

  TSDebug("xdebug", "scanning for %s header values", X_DEBUG_HEADER);

  // Walk the X-Debug header values and determine what to inject into the response.
  field = TSMimeHdrFieldFind(buffer, hdr, X_DEBUG_HEADER, lengthof(X_DEBUG_HEADER));
  while (field != TS_NULL_MLOC) {
    int count = TSMimeHdrFieldValuesCount(buffer, hdr, field);

    for (int i = 0; i < count; ++i) {
      const char * value;
      int vsize;

      value = TSMimeHdrFieldValueStringGet(buffer, hdr, field, i, &vsize);
      if (value == NULL || vsize == 0) {
        continue;
      }

      if (strncasecmp("x-cache-key", value, vsize) == 0) {
        xheaders |= XHEADER_X_CACHE_KEY;
      } else if (strncasecmp("via", value, vsize) == 0) {
        // If the client requests the Via header, enable verbose Via debugging for this transaction.
        TSHttpTxnConfigIntSet(txn, TS_CONFIG_HTTP_INSERT_RESPONSE_VIA_STR, 3);
      } else {
        TSDebug("xdebug", "ignoring unrecognized debug tag '%.*s'", vsize, value);
      }
    }

    // Get the next duplicate.
    next = TSMimeHdrFieldNextDup(buffer, hdr, field);

    // Destroy the current field that we have. We don't want this to go through and potentially confuse the origin.
    TSMimeHdrFieldRemove(buffer, hdr, field);
    TSMimeHdrFieldDestroy(buffer, hdr, field);

    // Now release our reference.
    TSHandleMLocRelease(buffer, hdr, field);

    // And go to the next field.
    field = next;
  }

  if (xheaders) {
    TSHttpTxnHookAdd(txn, TS_HTTP_SEND_RESPONSE_HDR_HOOK, XInjectHeadersCont);
    TSHttpTxnArgSet(txn, XArgIndex, (void *)xheaders);
  }

done:
  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return TS_EVENT_NONE;
}

void
TSPluginInit(int /* argc */, const char * /*argv */ [])
{
  TSPluginRegistrationInfo info;

  info.plugin_name = (char *)"xdebug";
  info.vendor_name = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";

  if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS) {
    TSError("xdebug plugin registration failed");
  }

  TSReleaseAssert(
    TSHttpArgIndexReserve("xdebug", "xdebug header requests" , &XArgIndex) == TS_SUCCESS
  );

  TSReleaseAssert(
    XInjectHeadersCont = TSContCreate(XInjectResponseHeaders, NULL)
  );

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(XScanRequestHeaders, NULL));
}

// vim: set ts=2 sw=2 et :
