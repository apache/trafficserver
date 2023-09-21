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
    int len;
    const char *server_name = TSVConnSslSniGet(vc, &len);
    std::string_view sni_name(server_name, len);
    SniRateLimiter *limiter = selector->find(sni_name);

    if (limiter) {
      // Check if we have an IP reputation for this SNI, and if we should block
      if (limiter->iprep.initialized()) {
        const sockaddr *sock = TSNetVConnRemoteAddrGet(vc);
        int pressure         = limiter->pressure();

        TSDebug(PLUGIN_NAME, "CLIENT_HELLO on %.*s, pressure=%d", static_cast<int>(sni_name.length()), sni_name.data(), pressure);

        // TSDebug(PLUGIN_NAME, "IP Reputation: pressure is currently %d", pressure);

        if (pressure >= 0) { // When pressure is < 0, we're not yet at a level of pressure to be concerned about
          char client_ip[INET6_ADDRSTRLEN] = "[unknown]";
          auto [bucket, cur_cnt]           = limiter->iprep.increment(sock);

          // Get the client IP string if debug is enabled
          if (TSIsDebugTagSet(PLUGIN_NAME)) {
            if (sock->sa_family == AF_INET) {
              inet_ntop(AF_INET, &(((struct sockaddr_in *)sock)->sin_addr), client_ip, INET_ADDRSTRLEN);
            } else if (sock->sa_family == AF_INET6) {
              inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sock)->sin6_addr), client_ip, INET6_ADDRSTRLEN);
            }
          }

          if (cur_cnt > limiter->iprep_permablock_count &&
              bucket <= limiter->iprep_permablock_threshold) { // Mark for long-term blocking
            TSDebug(PLUGIN_NAME, "Marking IP=%s for perma-blocking", client_ip);
            bucket = limiter->iprep.block(sock);
          }

          if (static_cast<uint32_t>(pressure) > bucket) { // Remember the perma-block bucket is always 0, and we are >=0 already
            // Block this IP from finishing the handshake
            TSDebug(PLUGIN_NAME, "Rejecting connection from IP=%s, we're at pressure and IP was chosen to be blocked", client_ip);
            TSUserArgSet(vc, gVCIdx, nullptr);
            TSVConnReenableEx(vc, TS_EVENT_ERROR);

            return TS_ERROR;
          }
        }
      } else {
        TSDebug(PLUGIN_NAME, "CLIENT_HELLO on %.*s, no IP reputation", static_cast<int>(sni_name.length()), sni_name.data());
      }

      // If we passed the IP reputation filter, continue rate limiting these connections
      if (!limiter->reserve()) {
        if (!limiter->max_queue || limiter->full()) {
          // We are running at limit, and the queue has reached max capacity, give back an error and be done.
          TSDebug(PLUGIN_NAME, "Rejecting connection, we're at capacity and queue is full");
          TSUserArgSet(vc, gVCIdx, nullptr);
          limiter->incrementMetric(RATE_LIMITER_METRIC_REJECTED);
          TSVConnReenableEx(vc, TS_EVENT_ERROR);

          return TS_ERROR;
        } else {
          TSUserArgSet(vc, gVCIdx, reinterpret_cast<void *>(limiter));
          limiter->push(vc, contp);
          TSDebug(PLUGIN_NAME, "Queueing the VC, we are at capacity");
          limiter->incrementMetric(RATE_LIMITER_METRIC_QUEUED);
        }
      } else {
        // Not at limit on the handshake, we can re-enable
        TSUserArgSet(vc, gVCIdx, reinterpret_cast<void *>(limiter));
        TSVConnReenable(vc);
      }
    } else {
      // No limiter for this SNI at all, clear the args etc. just in case
      TSUserArgSet(vc, gVCIdx, nullptr);
      TSVConnReenable(vc);
    }
  } break;

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
    {const_cast<char *>("prefix"), required_argument, nullptr, 'p'},
    {const_cast<char *>("tag"), required_argument, nullptr, 't'},
    // These are all for the IP reputation system. ToDo: These should be global rather than per SNI ?
    {const_cast<char *>("iprep_maxage"), required_argument, nullptr, 'a'},
    {const_cast<char *>("iprep_buckets"), required_argument, nullptr, 'B'},
    {const_cast<char *>("iprep_bucketsize"), required_argument, nullptr, 'S'},
    {const_cast<char *>("iprep_percentage"), required_argument, nullptr, 'C'},
    {const_cast<char *>("iprep_permablock_limit"), required_argument, nullptr, 'L'},
    {const_cast<char *>("iprep_permablock_pressure"), required_argument, nullptr, 'P'},
    {const_cast<char *>("iprep_permablock_maxage"), required_argument, nullptr, 'A'},
    // EOF
    {nullptr, no_argument, nullptr, '\0'},
  };
  optind = 1;

  TSDebug(PLUGIN_NAME, "Initializing an SNI Rate Limiter");

  while (true) {
    int opt = getopt_long(argc, const_cast<char *const *>(argv), "", longopt, nullptr);

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
    case 'p':
      this->prefix = std::string(optarg);
      break;
    case 't':
      this->tag = std::string(optarg);
      break;
    case 'a':
      this->_iprep_max_age = std::chrono::seconds(strtol(optarg, nullptr, 10));
      break;
    case 'B':
      this->_iprep_num_buckets = strtol(optarg, nullptr, 10);
      if (this->_iprep_num_buckets >= 100) {
        TSError("sni_limiter: iprep_num_buckets must be in the range 1 .. 99, IP reputation disabled");
        this->_iprep_num_buckets = 0;
      }
      break;
    case 'S':
      this->_iprep_size = strtol(optarg, nullptr, 10);
      break;
    case 'C':
      this->_iprep_percent = strtol(optarg, nullptr, 10);
      break;
    case 'L':
      this->iprep_permablock_count = strtol(optarg, nullptr, 10);
      break;
    case 'P':
      this->iprep_permablock_threshold = strtol(optarg, nullptr, 10);
      break;
    case 'A':
      this->_iprep_perma_max_age = std::chrono::seconds(strtol(optarg, nullptr, 10));
      break;
    }
    if (opt == -1) {
      break;
    }
  }

  // Enable and initialize the IP reputation if asked for
  if (this->_iprep_num_buckets > 0 && this->_iprep_size > 0) {
    TSDebug(PLUGIN_NAME, "Calling and _initialized is %d\n", this->iprep.initialized());
    this->iprep.initialize(this->_iprep_num_buckets, this->_iprep_size);
    TSDebug(PLUGIN_NAME, "IP-reputation enabled with %u buckets, max size is 2^%u", this->_iprep_num_buckets, this->_iprep_size);

    TSDebug(PLUGIN_NAME, "Called and _initialized is %d\n", this->iprep.initialized());

    // These settings are optional
    if (this->_iprep_max_age != std::chrono::seconds::zero()) {
      this->iprep.maxAge(this->_iprep_max_age);
    }
    if (this->_iprep_perma_max_age != std::chrono::seconds::zero()) {
      this->iprep.permaMaxAge(this->_iprep_perma_max_age);
    }
  }

  return true;
}
