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
    TSDebug(PLUGIN_TAG, "timeout is set to: %zu", timeout);
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
  Instance *instance = new Instance;

  if (argc > 2) {
    std::copy(argv + 2, argv + argc, std::back_inserter(instance->origins));
  }

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
  TSMLoc location;

  CHECK(TSHttpTxnClientReqGet(t, &buffer, &location));

  assert(buffer != nullptr);
  assert(location != nullptr);

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

  int length;
  const char *const method = TSHttpHdrMethodGet(buffer, location, &length);

  TSDebug(PLUGIN_TAG, "Method is %s.", std::string(method, length).c_str());

  if (length == TS_HTTP_LEN_POST && memcmp(TS_HTTP_METHOD_POST, method, TS_HTTP_LEN_POST) == 0) {
    const TSVConn vconnection = TSTransformCreate(handlePost, t);
    assert(vconnection != nullptr);
    TSContDataSet(vconnection, new PostState(requests));
    assert(requests.empty());
    TSHttpTxnHookAdd(t, TS_HTTP_REQUEST_TRANSFORM_HOOK, vconnection);
  } else {
    dispatch(requests, timeout);
  }

  TSHandleMLocRelease(buffer, TS_NULL_MLOC, location);

  TSStatIntIncrement(statistics.requests, 1);
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
    TSDebug(PLUGIN_TAG, "Skipping transaction %p", t);
  }

  return TSREMAP_NO_REMAP;
}
