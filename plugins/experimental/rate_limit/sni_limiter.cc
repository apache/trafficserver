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
#include "tscore/ink_config.h"

// Needs special OpenSSL APIs as a global plugin for early CLIENT_HELLO inspection
#if TS_USE_HELLO_CB

#include <unistd.h>
#include <getopt.h>
#include <cstdlib>

#include "sni_selector.h"
#include "sni_limiter.h"

// This holds the VC user arg index for the SNI limiters.
int gVCIdx = -1;

///////////////////////////////////////////////////////////////////////////////
// These continuations are "helpers" to the SNI limiter object. Putting them
// outside the class implementation is just cleaner.
//
int
sni_limit_cont(TSCont contp, TSEvent event, void *edata)
{
  TSVConn vc            = static_cast<TSVConn>(edata);
  SniSelector *selector = static_cast<SniSelector *>(TSContDataGet(contp));
  TSReleaseAssert(selector);

  switch (event) {
  case TS_EVENT_SSL_CLIENT_HELLO: {
    TSSslConnection ssl_conn  = TSVConnSslConnectionGet(vc);
    SSL *ssl                  = reinterpret_cast<SSL *>(ssl_conn);
    std::string_view sni_name = getSNI(ssl);

    if (!sni_name.empty()) { // This should likely always succeed, but without it we can't do anything
      SniRateLimiter *limiter = selector->find(sni_name);

      TSDebug(PLUGIN_NAME, "CLIENT_HELLO on %.*s", static_cast<int>(sni_name.length()), sni_name.data());
      if (limiter && !limiter->reserve()) {
        if (!limiter->max_queue || limiter->full()) {
          // We are running at limit, and the queue has reached max capacity, give back an error and be done.
          TSVConnReenableEx(vc, TS_EVENT_ERROR);
          TSDebug(PLUGIN_NAME, "Rejecting connection, we're at capacity and queue is full");
          TSUserArgSet(vc, gVCIdx, nullptr);

          return TS_ERROR;
        } else {
          TSUserArgSet(vc, gVCIdx, reinterpret_cast<void *>(limiter));
          limiter->push(vc, contp);
          TSDebug(PLUGIN_NAME, "Queueing the VC, we are at capacity");
        }
      } else {
        // Not at limit on the handshake, we can re-enable
        TSUserArgSet(vc, gVCIdx, reinterpret_cast<void *>(limiter));
        TSVConnReenable(vc);
      }
    } else {
      TSVConnReenable(vc);
    }

    break;
  }

  case TS_EVENT_VCONN_CLOSE: {
    SniRateLimiter *limiter = static_cast<SniRateLimiter *>(TSUserArgGet(vc, gVCIdx));

    if (limiter) {
      TSUserArgSet(vc, gVCIdx, nullptr);
      limiter->release();
    }
    TSVConnReenable(vc);
    break;
  }

  default:
    TSDebug(PLUGIN_NAME, "Unknown event %d", static_cast<int>(event));
    TSError("Unknown event in %s", PLUGIN_NAME);
    break;
  }

  return TS_EVENT_CONTINUE;
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

  return true;
}

#endif
