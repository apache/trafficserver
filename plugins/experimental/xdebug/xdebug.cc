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

#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <getopt.h>

#include "ts/ts.h"
#include "ts/ink_defs.h"

static struct {
  const char *str;
  int len;
} xDebugHeader = {NULL, 0};

#define XHEADER_X_CACHE_KEY 0x0004u
#define XHEADER_X_MILESTONES 0x0008u
#define XHEADER_X_CACHE 0x0010u
#define XHEADER_X_GENERATION 0x0020u

static int XArgIndex             = 0;
static TSCont XInjectHeadersCont = NULL;

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
  } strval = {NULL, 0};

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
  TSReleaseAssert(TSMimeHdrFieldValueStringInsert(buffer, hdr, dst, 0 /* idx */, strval.ptr, strval.len) == TS_SUCCESS);

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
    TSReleaseAssert(TSMimeHdrFieldValueStringInsert(buffer, hdr, dst, 0 /* idx */, "none", 4) == TS_SUCCESS);
  } else {
    const char *msg = (status < 0 || status >= (int)countof(names)) ? "unknown" : names[status];

    TSReleaseAssert(TSMimeHdrFieldValueStringInsert(buffer, hdr, dst, 0 /* idx */, msg, -1) == TS_SUCCESS);
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
      double elapsed = (double)(time - epoch) / 1000000000.0;
      int len        = (int)snprintf(hdrval, sizeof(hdrval), "%s=%1.9lf", milestones[i].msname, elapsed);

      TSReleaseAssert(TSMimeHdrFieldValueStringInsert(buffer, hdr, dst, 0 /* idx */, hdrval, len) == TS_SUCCESS);
    }
  }

done:
  if (dst != TS_NULL_MLOC) {
    TSHandleMLocRelease(buffer, hdr, dst);
  }
}

static int
XInjectResponseHeaders(TSCont /* contp */, TSEvent event, void *edata)
{
  TSHttpTxn txn     = (TSHttpTxn)edata;
  intptr_t xheaders = 0;
  TSMBuffer buffer;
  TSMLoc hdr;

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

  if (xheaders & XHEADER_X_CACHE) {
    InjectCacheHeader(txn, buffer, hdr);
  }

  if (xheaders & XHEADER_X_MILESTONES) {
    InjectMilestonesHeader(txn, buffer, hdr);
  }

  if (xheaders & XHEADER_X_GENERATION) {
    InjectGenerationHeader(txn, buffer, hdr);
  }

done:
  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return TS_EVENT_NONE;
}

// Scan the client request headers and determine which debug headers they
// want in the response.
static int
XScanRequestHeaders(TSCont /* contp */, TSEvent event, void *edata)
{
  TSHttpTxn txn     = (TSHttpTxn)edata;
  intptr_t xheaders = 0;
  TSMLoc field, next;
  TSMBuffer buffer;
  TSMLoc hdr;

  TSReleaseAssert(event == TS_EVENT_HTTP_READ_REQUEST_HDR);

  if (TSHttpTxnClientReqGet(txn, &buffer, &hdr) == TS_ERROR) {
    goto done;
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
      if (value == NULL || vsize == 0) {
        continue;
      }

#define header_field_eq(name, vptr, vlen) (((int)lengthof(name) == vlen) && (strncasecmp(name, vptr, vlen) == 0))

      if (header_field_eq("x-cache-key", value, vsize)) {
        xheaders |= XHEADER_X_CACHE_KEY;
      } else if (header_field_eq("x-milestones", value, vsize)) {
        xheaders |= XHEADER_X_MILESTONES;
      } else if (header_field_eq("x-cache", value, vsize)) {
        xheaders |= XHEADER_X_CACHE;
      } else if (header_field_eq("x-cache-generation", value, vsize)) {
        xheaders |= XHEADER_X_GENERATION;
      } else if (header_field_eq("via", value, vsize)) {
        // If the client requests the Via header, enable verbose Via debugging for this transaction.
        TSHttpTxnConfigIntSet(txn, TS_CONFIG_HTTP_INSERT_RESPONSE_VIA_STR, 3);
      } else if (header_field_eq("diags", value, vsize)) {
        // Enable diagnostics for DebugTxn()'s only
        TSHttpTxnDebugSet(txn, 1);
      } else {
        TSDebug("xdebug", "ignoring unrecognized debug tag '%.*s'", vsize, value);
      }
    }

#undef header_field_eq

    // Get the next duplicate.
    next = TSMimeHdrFieldNextDup(buffer, hdr, field);

    // Destroy the current field that we have. We don't want this to go through and potentially confuse the origin.
    TSMimeHdrFieldDestroy(buffer, hdr, field);

    // Now release our reference.
    TSHandleMLocRelease(buffer, hdr, field);

    // And go to the next field.
    field = next;
  }

  if (xheaders) {
    TSDebug("xdebug", "adding response hook for header mask %p", (void *)xheaders);
    TSHttpTxnHookAdd(txn, TS_HTTP_SEND_RESPONSE_HDR_HOOK, XInjectHeadersCont);
    TSHttpTxnArgSet(txn, XArgIndex, (void *)xheaders);
  }

done:
  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return TS_EVENT_NONE;
}

void
TSPluginInit(int argc, const char *argv[])
{
  static const struct option longopt[] = {{const_cast<char *>("header"), required_argument, NULL, 'h'},
                                          {NULL, no_argument, NULL, '\0'}};
  TSPluginRegistrationInfo info;

  info.plugin_name   = (char *)"xdebug";
  info.vendor_name   = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[xdebug] Plugin registration failed");
  }

  optind = 0;

  // Parse the arguments
  while (true) {
    int opt = getopt_long(argc, (char *const *)argv, "", longopt, NULL);

    switch (opt) {
    case 'h':
      xDebugHeader.str = TSstrdup(optarg);
      break;
    }

    if (opt == -1) {
      break;
    }
  }

  if (NULL == xDebugHeader.str) {
    xDebugHeader.str = TSstrdup("X-Debug"); // We malloc this, for consistency for future plugin unload events
  }
  xDebugHeader.len = strlen(xDebugHeader.str);

  // Setup the global hook
  TSReleaseAssert(TSHttpArgIndexReserve("xdebug", "xdebug header requests", &XArgIndex) == TS_SUCCESS);
  TSReleaseAssert(XInjectHeadersCont = TSContCreate(XInjectResponseHeaders, NULL));
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(XScanRequestHeaders, NULL));
}

// vim: set ts=2 sw=2 et :
