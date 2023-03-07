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
#pragma once

#include "limiter.h"
#include "ip_reputation.h"
#include "ts/ts.h"

int sni_limit_cont(TSCont contp, TSEvent event, void *edata);

///////////////////////////////////////////////////////////////////////////////
// SNI based limiters, for global (pligin.config) instance(s).
//
class SniRateLimiter : public RateLimiter<TSVConn>
{
public:
  SniRateLimiter() {}

  SniRateLimiter(const SniRateLimiter &src)
  {
    limit     = src.limit;
    max_queue = src.max_queue;
    max_age   = src.max_age;
    prefix    = src.prefix;
    tag       = src.tag;
  }

  bool initialize(int argc, const char *argv[]);

  // ToDo: this ought to go into some better global IP reputation pool / settings. Waiting for YAML...
  IpReputation::SieveLru iprep;
  uint32_t iprep_permablock_count     = 0; // "Hits" limit for blocking permanently
  uint32_t iprep_permablock_threshold = 0; // Pressure threshold for permanent block

  // Calculate the pressure, which is either a negative number (ignore), or a number 0-<buckets>.
  // 0 == block only perma-blocks.
  int32_t
  pressure() const
  {
    int32_t p = ((active() / static_cast<float>(limit) * 100) - _iprep_percent) / (100 - _iprep_percent) * (_iprep_num_buckets + 1);

    return (p >= static_cast<int32_t>(_iprep_num_buckets) ? _iprep_num_buckets : p);
  }

private:
  // ToDo: These should be moved to global configurations to have one shared IP Reputation.
  // today the configuration of this is so klunky, that there is no easy way to make it "global".
  std::chrono::seconds _iprep_max_age       = std::chrono::seconds::zero(); // Max age in the SieveLRUs for regular buckets
  std::chrono::seconds _iprep_perma_max_age = std::chrono::seconds::zero(); // Max age in the SieveLRUs for perma-block buckets
  uint32_t _iprep_num_buckets               = 10;                           // Number of buckets. ToDo: leave this at 10 always
  uint32_t _iprep_percent                   = 90;                           // At what percentage of limit we start blocking
  uint32_t _iprep_size                      = 0;                            // Size of the biggest bucket; 15 == 2^15 == 32768
};
