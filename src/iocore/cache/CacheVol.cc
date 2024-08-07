/** @file

  A brief file description

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

#include "P_Cache.h"

DEF_DBG(cache_scan_truss);

#define SCAN_BUF_SIZE              RECOVERY_SIZE
#define SCAN_WRITER_LOCK_MAX_RETRY 5

Action *
Cache::scan(Continuation *cont, const char *hostname, int host_len, int KB_per_second)
{
  Dbg(get_dbg_cache_scan_truss(), "inside scan");
  if (!CacheProcessor::IsCacheReady(CACHE_FRAG_TYPE_HTTP)) {
    cont->handleEvent(CACHE_EVENT_SCAN_FAILED, nullptr);
    return ACTION_RESULT_DONE;
  }

  CacheVC *c = new_CacheVC(cont);
  c->stripe  = nullptr;
  /* do we need to make a copy */
  c->hostname        = const_cast<char *>(hostname);
  c->host_len        = host_len;
  c->op_type         = static_cast<int>(CacheOpType::Scan);
  c->buf             = new_IOBufferData(BUFFER_SIZE_FOR_XMALLOC(SCAN_BUF_SIZE), MEMALIGNED);
  c->scan_msec_delay = (SCAN_BUF_SIZE / KB_per_second);
  c->offset          = 0;
  SET_CONTINUATION_HANDLER(c, &CacheVC::scanStripe);
  eventProcessor.schedule_in(c, HRTIME_MSECONDS(c->scan_msec_delay));
  cont->handleEvent(CACHE_EVENT_SCAN, c);
  return &c->_action;
}
