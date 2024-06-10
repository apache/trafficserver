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

#include "limiter.h"

// order must align with the above
static const char *types[] = {
  "sni",
  "remap",
};

// Order should match the enum in limiter.h
static const char *suffixes[] = {
  "queued",
  "rejected",
  "expired",
  "resumed",
};

// This function will run on the dedicated thread, until the global bucket manager is dead
void
BucketManager::refill_thread()
{
  // coverity[missing_lock]
  while (_running) {
    auto startTime = std::chrono::steady_clock::now();

    {
      std::lock_guard<std::mutex> lock(_mutex);

      for (auto &bucket : _buckets) {
        bucket->refill();
      }
    }

    auto sleepTime = std::chrono::milliseconds(BUCKET_REFILL_INTERVAL) - (std::chrono::steady_clock::now() - startTime);

    if (sleepTime > std::chrono::milliseconds(0)) {
      std::this_thread::sleep_for(sleepTime);
    }
  }
}

void
metric_helper(std::array<int, RATE_LIMITER_METRIC_MAX> &metrics, uint type, const std::string &tag, const std::string &name,
              const std::string &prefix)
{
  std::string metric_prefix = prefix;

  metric_prefix.push_back('.');
  metric_prefix.append(types[type]);

  if (!tag.empty()) {
    metric_prefix.push_back('.');
    metric_prefix.append(tag);
  } else if (!name.empty()) {
    metric_prefix.push_back('.');
    metric_prefix.append(name);
  }

  for (int i = 0; i < RATE_LIMITER_METRIC_MAX; i++) {
    size_t const metricsz = metric_prefix.length() + strlen(suffixes[i]) + 2; // padding for dot+terminator
    char *const  metric   = static_cast<char *>(TSmalloc(metricsz));

    snprintf(metric, metricsz, "%s.%s", metric_prefix.data(), suffixes[i]);
    metrics[i] = TS_ERROR;

    if (TSStatFindName(metric, &metrics[i]) == TS_ERROR) {
      metrics[i] = TSStatCreate(metric, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
    }

    if (metrics[i] != TS_ERROR) {
      Dbg(dbg_ctl, "established metric '%s' as ID %d", metric, metrics[i]);
    } else {
      TSError("failed to create metric '%s'", metric);
    }

    TSfree(metric);
  }
}
