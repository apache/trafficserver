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

class SniSelector;

///////////////////////////////////////////////////////////////////////////////
// SNI based limiters, for global (plugin.config) instance(s).
//
class SniRateLimiter : public RateLimiter<TSVConn>
{
public:
  SniRateLimiter() = delete;
  SniRateLimiter(std::string_view sni, SniSelector *sel) : selector(sel) { name = sni; }

  // Calculate the pressure, which is either a negative number (ignore), or a number 0-<buckets>.
  // 0 == block only perma-blocks.
  int32_t
  pressure() const
  {
    int32_t p = ((active() / static_cast<float>(limit) * 100) - iprep->percentage()) / (100 - iprep->percentage()) *
                (iprep->numBuckets() + 1);

    return (p >= static_cast<int32_t>(iprep->numBuckets()) ? iprep->numBuckets() : p);
  }

  void
  addIPReputation(IpReputation::SieveLru *iprep)
  {
    this->iprep = iprep;
  }

  SniSelector *selector         = nullptr; // The selector we belong to
  IpReputation::SieveLru *iprep = nullptr; // IP reputation for this SNI (if any)
};
