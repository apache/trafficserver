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

bool
SniRateLimiter::parseYaml(const YAML::Node &node)
{
  if (node["limit"]) {
    limit = node["limit"].as<uint32_t>();
  } else {
    // ToDo: Should we require the limit ?
  }

  if (node["ip-rep"]) {
    std::string ipr_name = node["ip-rep"].as<std::string>();

    if (!(iprep = selector->findIpRep(ipr_name))) {
      TSError("[%s] IP Reputation name (%s) not found for SNI=%s", PLUGIN_NAME, ipr_name.c_str(), name.c_str());
      return false;
    }
  }

  const YAML::Node &queue = node["queue"];

  // If enabled, we default to UINT32_MAX, but the object default is still 0 (no queue)
  if (queue) {
    max_queue = queue["size"] ? queue["size"].as<uint32_t>() : UINT32_MAX;

    if (queue["max_age"]) {
      max_age = std::chrono::seconds(queue["max_age"].as<uint32_t>());
    }

    const YAML::Node &metrics = node["metrics"];

    if (metrics) {
      std::string prefix = metrics["prefix"] ? metrics["prefix"].as<std::string>() : RATE_LIMITER_METRIC_PREFIX;
      std::string tag    = metrics["tag"] ? metrics["tag"].as<std::string>() : name;

      Dbg(dbg_ctl, "Metrics for selector rule: %s(%s, %s)", name.c_str(), prefix.c_str(), tag.c_str());
      initializeMetrics(RATE_LIMITER_TYPE_SNI, prefix, tag);
    }
  }

  Dbg(dbg_ctl, "Loaded selector rule: %s(%u, %u, %ld)", name.c_str(), limit, max_queue, static_cast<long>(max_age.count()));

  return true;
}

///////////////////////////////////////////////////////////////////////////////
// These continuations are "helpers" to the SNI limiter object. Putting them
// outside the class implementation is just cleaner.
//
int
sni_limit_cont(TSCont contp, TSEvent event, void *edata)
{
  TSVConn vc = static_cast<TSVConn>(edata);

  switch (event) {
  case TS_EVENT_SSL_CLIENT_HELLO: {
    int len;
    const char *server_name = TSVConnSslSniGet(vc, &len);
    const std::string sni_name(server_name, len);
    SniSelector *selector   = SniSelector::instance();
    SniRateLimiter *limiter = selector->findSNI(sni_name);

    if (limiter) {
      // Check if we have an IP reputation for this SNI, and if we should block
      if (limiter->iprep && limiter->iprep->initialized()) {
        const sockaddr *sock = TSNetVConnRemoteAddrGet(vc);
        int32_t pressure     = limiter->pressure();

        Dbg(dbg_ctl, "CLIENT_HELLO on %s, pressure=%d", sni_name.c_str(), pressure);

        // Dbg(dbg_ctl, "IP Reputation: pressure is currently %d", pressure);

        if (pressure >= 0) { // When pressure is < 0, we're not yet at a level of pressure to be concerned about
          char client_ip[INET6_ADDRSTRLEN] = "[unknown]";
          auto [bucket, cur_cnt]           = limiter->iprep->increment(sock);

          // Get the client IP string if debug is enabled
          if (dbg_ctl.on()) {
            if (sock->sa_family == AF_INET) {
              inet_ntop(AF_INET, &(((struct sockaddr_in *)sock)->sin_addr), client_ip, INET_ADDRSTRLEN);
            } else if (sock->sa_family == AF_INET6) {
              inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sock)->sin6_addr), client_ip, INET6_ADDRSTRLEN);
            }
          }

          if (cur_cnt > limiter->iprep->permablock_count() &&
              bucket <= limiter->iprep->permablock_threshold()) { // Mark for long-term blocking
            Dbg(dbg_ctl, "Marking IP=%s for perma-blocking", client_ip);
            bucket = limiter->iprep->block(sock);
          }

          if (static_cast<uint32_t>(pressure) > bucket) { // Remember the perma-block bucket is always 0, and we are >=0 already
            // Block this IP from finishing the handshake
            Dbg(dbg_ctl, "Rejecting connection from IP=%s, we're at pressure and IP was chosen to be blocked", client_ip);
            TSUserArgSet(vc, gVCIdx, nullptr);
            selector->release();
            TSVConnReenableEx(vc, TS_EVENT_ERROR);

            return TS_ERROR;
          }
        }
      } else {
        Dbg(dbg_ctl, "CLIENT_HELLO on %.*s, no IP reputation", static_cast<int>(sni_name.length()), sni_name.data());
      }

      // If we passed the IP reputation filter, continue rate limiting these connections
      if (!limiter->reserve()) {
        if (!limiter->max_queue || limiter->full()) {
          // We are running at limit, and the queue has reached max capacity, give back an error and be done.
          Dbg(dbg_ctl, "Rejecting connection, we're at capacity and queue is full");
          TSUserArgSet(vc, gVCIdx, nullptr);
          limiter->incrementMetric(RATE_LIMITER_METRIC_REJECTED);
          selector->release();
          TSVConnReenableEx(vc, TS_EVENT_ERROR);

          return TS_ERROR;
        } else {
          TSUserArgSet(vc, gVCIdx, reinterpret_cast<void *>(limiter));
          limiter->push(vc, contp);
          Dbg(dbg_ctl, "Queueing the VC, we are at capacity");
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
      limiter->free();
      limiter->selector->release(); // Release the selector, such that it can be deleted later
    }
    TSVConnReenable(vc);
    break;
  }

  default:
    Dbg(dbg_ctl, "Unknown event %d", static_cast<int>(event));
    TSError("Unknown event in %s", PLUGIN_NAME);
    break;
  }

  return TS_EVENT_CONTINUE;
}
