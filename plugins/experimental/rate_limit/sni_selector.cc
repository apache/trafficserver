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

#include <cstring>

#include "sni_limiter.h"
#include "sni_selector.h"

///////////////////////////////////////////////////////////////////////////////
// YAML parser for the global YAML configuration (via plugin.config)
//
bool
SniSelector::yamlParser(std::string yaml_file)
{
  // Parsed the IP Reputations, so add those.
  // ToDo: Obviously need the YAML values here ...
  std::string iprep_name = "main";
  uint32_t num_buckets   = 10;
  uint32_t size          = 15;
  uint32_t percentage    = 90;
  std::chrono::seconds max_age(60);

  if (nullptr != findIpRep(iprep_name)) {
    TSError("[%s] Duplicate IP-Reputation names being added (%.*s)", PLUGIN_NAME, static_cast<int>(iprep_name.size()),
            iprep_name.data());
    return false;
  }

  auto iprep = new IpReputation::SieveLru(iprep_name, num_buckets, size, percentage, max_age);

  std::chrono::seconds perma_max_age(1800);

  iprep->initializePerma(100, 1, perma_max_age);
  addIPReputation(iprep);

  // ToDo: Add the IP lists

  // ToDo: Iterate over all SNIs.
  std::string sni = "hel.ogre.com";

  if (nullptr != findSNI(sni)) {
    TSError("[%s] Duplicate SNIs being added (%.*s)", PLUGIN_NAME, static_cast<int>(sni.size()), sni.data());
    return false;
  }

  auto limiter = new SniRateLimiter(sni, this);
  auto iprep2  = findIpRep("main");

  TSReleaseAssert(limiter);

  std::chrono::seconds queue_max_age(60);

  limiter->initialize(5);
  limiter->initializeQueue(0, queue_max_age);
  limiter->initializeMetrics(RATE_LIMITER_TYPE_SNI, sni);
  if (iprep2) {
    limiter->addIPReputation(iprep2);
  }

  addLimiter(limiter);

  return true;
}

///////////////////////////////////////////////////////////////////////////////
// This is the queue management continuation, which gets called periodically
//
static int
sni_queue_cont(TSCont cont, TSEvent event, void *edata)
{
  SniSelector *selector = static_cast<SniSelector *>(TSContDataGet(cont));

  for (const auto &[key, limiter] : selector->limiters()) {
    QueueTime now = std::chrono::system_clock::now(); // Only do this once per limiter

    // Try to enable some queued VCs (if any) if there are slots available
    while (limiter->size() > 0 && limiter->reserve()) {
      auto [vc, contp, start_time]    = limiter->pop();
      std::chrono::milliseconds delay = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);

      (void)contp; // Ugly, but silences some compilers.
      Dbg(dbg_ctl, "SNI=%s: Enabling queued VC after %ldms", key.data(), static_cast<long>(delay.count()));
      TSVConnReenable(vc);
      limiter->incrementMetric(RATE_LIMITER_METRIC_RESUMED);
    }

    // Kill any queued VCs if they are too old
    if (limiter->size() > 0 && limiter->max_age > std::chrono::milliseconds::zero()) {
      now = std::chrono::system_clock::now(); // Update the "now", for some extra accuracy

      while (limiter->size() > 0 && limiter->hasOldEntity(now)) {
        // The oldest object on the queue is too old on the queue, so "kill" it.
        auto [vc, contp, start_time]  = limiter->pop();
        std::chrono::milliseconds age = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);

        (void)contp;
        Dbg(dbg_ctl, "Queued VC is too old (%ldms), erroring out", static_cast<long>(age.count()));
        TSVConnReenableEx(vc, TS_EVENT_ERROR);
        limiter->incrementMetric(RATE_LIMITER_METRIC_EXPIRED);
      }
    }
  }

  return TS_EVENT_NONE;
}

SniRateLimiter *
SniSelector::findSNI(std::string_view sni)
{
  if (sni.empty()) { // Likely shouldn't happen, but we can shortcircuit
    return nullptr;
  }

  auto limiter = _limiters.find(sni);

  if (limiter != _limiters.end()) {
    return limiter->second;
  }
  return nullptr;
}

IpReputation::SieveLru *
SniSelector::findIpRep(std::string_view name)
{
  auto it = std::find_if(_reputations.begin(), _reputations.end(),
                         [&name](const IpReputation::SieveLru *iprep) { return iprep->name() == name; });

  if (it != _reputations.end()) {
    return *it;
  }

  return nullptr;
}

///////////////////////////////////////////////////////////////////////////////
// If needed, create the queue continuation that needs to run for this selector.
//
void
SniSelector::setupQueueCont()
{
  if (_needs_queue_cont && !_queue_cont) {
    _queue_cont = TSContCreate(sni_queue_cont, TSMutexCreate());
    TSReleaseAssert(_queue_cont);
    TSContDataSet(_queue_cont, this);
    _action = TSContScheduleEveryOnPool(_queue_cont, QUEUE_DELAY_TIME.count(), TS_THREAD_POOL_TASK);
  }
}
