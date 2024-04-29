/** @file

  Multiplexes request to other origins.

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
#include <algorithm>
#include <ts/ts.h>
#include <ts/remap.h>

#include <cinttypes>

#include "dispatch.h"
#include "fetcher.h"
#include "original-request.h"
#include "post.h"

#ifndef PLUGIN_TAG
#error Please define a PLUGIN_TAG before including this file.
#endif

using multiplexer_ns::dbg_ctl;

// 1s
const size_t DEFAULT_TIMEOUT = 1000000000000;

Statistics statistics;

TSReturnCode
TSRemapInit(TSRemapInterface *, char *, int)
{
  {
    timeout                      = 0;
    const char *const timeoutEnv = getenv(PLUGIN_TAG "__timeout");
    if (timeoutEnv != nullptr) {
      timeout = atol(timeoutEnv);
    }
    if (timeout < 1) {
      timeout = DEFAULT_TIMEOUT;
    }
    Dbg(dbg_ctl, "timeout is set to: %zu", timeout);
  }

  statistics.failures = TSStatCreate(PLUGIN_TAG ".failures", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_COUNT);

  statistics.hits = TSStatCreate(PLUGIN_TAG ".hits", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_COUNT);

  statistics.time = TSStatCreate(PLUGIN_TAG ".time", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_AVG);

  statistics.requests = TSStatCreate(PLUGIN_TAG ".requests", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_COUNT);

  statistics.timeouts = TSStatCreate(PLUGIN_TAG ".timeouts", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_COUNT);

  statistics.size = TSStatCreate(PLUGIN_TAG ".size", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_AVG);

  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char **argv, void **i, char *, int)
{
  assert(i != nullptr);
  Instance *instance    = new Instance;
  instance->skipPostPut = false;

  if (argc > 2) {
    std::copy_if(argv + 2, argv + argc, std::back_inserter(instance->origins), [&](const std::string &s) {
      if (s == "proxy.config.multiplexer.skip_post_put=1") {
        instance->skipPostPut = true;
        return false;
      }
      return true;
    });
  }
  Dbg(dbg_ctl, "skipPostPut is %s", (instance->skipPostPut ? "true" : "false"));

  *i = static_cast<void *>(instance);

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *i)
{
  assert(i != nullptr);
  delete static_cast<Instance *>(i);
}

void
DoRemap(const Instance &i, TSHttpTxn t)
{
  assert(t != nullptr);
  /*
  if (POST || PUT) {
    transformRequest
  }
  */
  TSMBuffer buffer;
  TSMLoc    location;

  CHECK(TSHttpTxnClientReqGet(t, &buffer, &location));

  assert(buffer != nullptr);
  assert(location != nullptr);

  int               method_length;
  const char *const method = TSHttpHdrMethodGet(buffer, location, &method_length);

  Dbg(dbg_ctl, "Method is %s.", std::string(method, method_length).c_str());

  // A value of -1 is used to indicate there was no Content-Length header.
  int content_length = -1;
  // Retrieve the value of the Content-Length header.
  auto field_loc = TSMimeHdrFieldFind(buffer, location, TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH);
  if (field_loc != TS_NULL_MLOC) {
    content_length = TSMimeHdrFieldValueUintGet(buffer, location, field_loc, -1);
    TSHandleMLocRelease(buffer, location, field_loc);
  }
  bool const is_post_or_put = (method_length == TS_HTTP_LEN_POST && memcmp(TS_HTTP_METHOD_POST, method, TS_HTTP_LEN_POST) == 0) ||
                              (method_length == TS_HTTP_LEN_PUT && memcmp(TS_HTTP_METHOD_PUT, method, TS_HTTP_LEN_PUT) == 0);
  if (i.skipPostPut && is_post_or_put) {
    Dbg(dbg_ctl, "skip_post_put: skipping a POST or PUT request.");
  } else if (content_length < 0 && is_post_or_put) {
    // HttpSM would need an update for POST request transforms to support
    // chunked request bodies. It currently does not support this.
    Dbg(dbg_ctl, "Skipping a non-Content-Length POST or PUT request.");
  } else {
    {
      TSMLoc field;

      CHECK(TSMimeHdrFieldCreateNamed(buffer, location, "X-Multiplexer", 13, &field));
      assert(field != nullptr);

      CHECK(TSMimeHdrFieldValueStringSet(buffer, location, field, -1, "original", 8));

      CHECK(TSMimeHdrFieldAppend(buffer, location, field));

      CHECK(TSHandleMLocRelease(buffer, location, field));
    }

    Requests requests;
    generateRequests(i.origins, buffer, location, requests);
    assert(requests.size() == i.origins.size());

    if (is_post_or_put) {
      const TSVConn vconnection = TSTransformCreate(handlePost, t);
      assert(vconnection != nullptr);
      PostState *state = new PostState(requests, content_length);
      TSContDataSet(vconnection, state);
      assert(requests.empty());
      TSHttpTxnHookAdd(t, TS_HTTP_REQUEST_TRANSFORM_HOOK, vconnection);
    } else {
      dispatch(requests, timeout);
    }

    TSStatIntIncrement(statistics.requests, 1);
  }
  TSHandleMLocRelease(buffer, TS_NULL_MLOC, location);
}

TSRemapStatus
TSRemapDoRemap(void *i, TSHttpTxn t, TSRemapRequestInfo *r)
{
  assert(i != nullptr);
  assert(t != nullptr);
  const Instance *const instance = static_cast<const Instance *>(i);

  if (!instance->origins.empty() && !TSHttpTxnIsInternal(t)) {
    DoRemap(*instance, t);
  } else {
    Dbg(dbg_ctl, "Skipping transaction %p", t);
  }

  return TSREMAP_NO_REMAP;
}
