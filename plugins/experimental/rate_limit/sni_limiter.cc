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
#include <unistd.h>
#include <getopt.h>
#include <cstdlib>

#include "sni_limiter.h"

///////////////////////////////////////////////////////////////////////////////
// These continuations are "helpers" to the SNI limiter object. Putting them
// outside the class implementation is just cleaner.
//
int
sni_limit_cont(TSCont contp, TSEvent event, void *edata)
{
  SniRateLimiter *limiter = static_cast<SniRateLimiter *>(TSContDataGet(contp));

  switch (event) {
  case TS_EVENT_SSL_CLIENT_HELLO: {
    TSVConn vc                = static_cast<TSVConn>(edata);
    TSSslConnection ssl_conn  = TSVConnSslConnectionGet(vc);
    SSL *ssl                  = reinterpret_cast<SSL *>(ssl_conn);
    std::string_view sni_name = getSNI(ssl);

    TSDebug(PLUGIN_NAME, "CLIENT_HELLO on %.*s", static_cast<int>(sni_name.length()), sni_name.data());

    if (!limiter->reserve()) {
      if (!limiter->max_queue || limiter->full()) {
        // We are running at limit, and the queue has reached max capacity, give back an error and be done.
        TSVConnReenableEx(vc, TS_EVENT_ERROR);
        TSDebug(PLUGIN_NAME, "Rejecting connection, we're at capacity and queue is full");

        // Here we have to mark the VConn user-data as handled, so that we don't decrement
        // again.
        TSUserArgSet(vc, limiter->vc_idx, reinterpret_cast<void *>(0));

        return TS_ERROR;
      } else {
        TSUserArgSet(vc, limiter->vc_idx, reinterpret_cast<void *>(1));
        limiter->push(vc, contp);
        TSDebug(PLUGIN_NAME, "Queueing the VC, we are at capacity");
      }
    } else {
      // Not at limit on the handshake, we can re-enable
      TSUserArgSet(vc, limiter->vc_idx, reinterpret_cast<void *>(1));
      TSVConnReenable(vc);
    }
    break;
  }

  case TS_EVENT_VCONN_CLOSE: {
    TSVConn vc = static_cast<TSVConn>(edata);

    if (static_cast<int>(reinterpret_cast<intptr_t>(TSUserArgGet(vc, limiter->vc_idx)))) {
      limiter->release();
    }
    TSVConnReenable(vc);
    break;
  }

  case TS_EVENT_HTTP_SSN_CLOSE:
    limiter->release();
    TSHttpSsnReenable(static_cast<TSHttpSsn>(edata), TS_EVENT_HTTP_CONTINUE);
    break;
  default:
    TSDebug(PLUGIN_NAME, "Unknown event %d", static_cast<int>(event));
    TSError("Unknown event in %s", PLUGIN_NAME);
    break;
  }

  return TS_EVENT_CONTINUE;
}

static int
sni_queue_cont(TSCont cont, TSEvent event, void *edata)
{
  SniRateLimiter *limiter = static_cast<SniRateLimiter *>(TSContDataGet(cont));
  QueueTime now           = std::chrono::system_clock::now(); // Only do this once per "loop"

  // Try to enable some queued VCs (if any) if there are slots available
  while (limiter->size() > 0 && limiter->reserve()) {
    auto [vc, contp, start_time]    = limiter->pop();
    std::chrono::milliseconds delay = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);

    TSDebug(PLUGIN_NAME, "Enabling queued VC after %ldms", static_cast<long>(delay.count()));
    TSVConnReenable(vc);
  }

  // Kill any queued VCs if they are too old
  if (limiter->size() > 0 && limiter->max_age > std::chrono::milliseconds::zero()) {
    now = std::chrono::system_clock::now(); // Update the "now", for some extra accuracy

    while (limiter->size() > 0 && limiter->hasOldEntity(now)) {
      // The oldest object on the queue is too old on the queue, so "kill" it.
      auto [vc, contp, start_time]  = limiter->pop();
      std::chrono::milliseconds age = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);

      TSDebug(PLUGIN_NAME, "Queued VC is too old (%ldms), erroring out", static_cast<long>(age.count()));
      TSVConnReenableEx(vc, TS_EVENT_ERROR);
    }
  }

  return TS_EVENT_NONE;
}

///////////////////////////////////////////////////////////////////////////////
// Parse the configurations for the TXN limiter.
//
bool
SniRateLimiter::initialize(int argc, const char *argv[])
{
  static const struct option longopt[] = {
    {const_cast<char *>("limit"), required_argument, nullptr, 'l'},
    {const_cast<char *>("queue"), required_argument, nullptr, 'q'},
    {const_cast<char *>("maxage"), required_argument, nullptr, 'm'},
    // EOF
    {nullptr, no_argument, nullptr, '\0'},
  };

  while (true) {
    int opt = getopt_long(argc, (char *const *)argv, "", longopt, nullptr);

    switch (opt) {
    case 'l':
      this->limit = strtol(optarg, nullptr, 10);
      break;
    case 'q':
      this->max_queue = strtol(optarg, nullptr, 10);
      break;
    case 'm':
      this->max_age = std::chrono::milliseconds(strtol(optarg, nullptr, 10));
      break;
    }
    if (opt == -1) {
      break;
    }
  }

  if (this->max_queue > 0) {
    _queue_cont = TSContCreate(sni_queue_cont, TSMutexCreate());
    TSReleaseAssert(_queue_cont);
    TSContDataSet(_queue_cont, this);
    _action = TSContScheduleEveryOnPool(_queue_cont, QUEUE_DELAY_TIME.count(), TS_THREAD_POOL_TASK);
  }

  return true;
}
