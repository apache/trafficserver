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

#include "strategy.h"
#include "healthstatus.h"

class PLNextHopSelectionStrategy;

constexpr const unsigned int PL_NH_MAX_GROUP_RINGS = 5;

enum PLNHHashKeyType {
  PL_NH_URL_HASH_KEY = 0,
  PL_NH_HOSTNAME_HASH_KEY,
  PL_NH_PATH_HASH_KEY, // default, consistent hash uses the request url path
  PL_NH_PATH_QUERY_HASH_KEY,
  PL_NH_PATH_FRAGMENT_HASH_KEY,
  PL_NH_CACHE_HASH_KEY
};

// The transaction state needed by PLNextHopConsistentHash about the last parent found,
// which is needed to find the next parent.
// Does not include the data already in ResponseAction.
//
// TODO initialize? zero? -1?
struct PLNextHopConsistentHashTxn {
  PLNHParentResultType result            = PL_NH_PARENT_UNDEFINED;
  bool chash_init[PL_NH_MAX_GROUP_RINGS] = {false};
  TSHostStatus first_choice_status       = TSHostStatus::TS_HOST_STATUS_INIT;
  int line_number                        = -1;
  uint32_t last_parent;
  uint32_t start_parent;
  uint32_t last_group;
  bool wrap_around;
  bool mapWrapped[2];
  int last_lookup;
  ATSConsistentHashIter chashIter[PL_NH_MAX_GROUP_RINGS];
  const char *hostname = "";
  size_t hostname_len  = 0;
  in_port_t port       = 0;
  bool retry           = false;
  bool no_cache        = false;
};

class PLNextHopConsistentHash : public PLNextHopSelectionStrategy
{
  std::vector<std::shared_ptr<ATSConsistentHash>> rings;
  uint64_t getHashKey(uint64_t sm_id, TSMBuffer reqp, TSMLoc url, TSMLoc parent_selection_url, ATSHash64 *h);

  std::shared_ptr<PLHostRecord> chashLookup(const std::shared_ptr<ATSConsistentHash> &ring, uint32_t cur_ring,
                                            PLNextHopConsistentHashTxn *state, bool *wrapped, uint64_t sm_id, TSMBuffer reqp,
                                            TSMLoc url, TSMLoc parent_selection_url);

public:
  const uint32_t LineNumberPlaceholder = 99999;

  PLNHHashKeyType hash_key = PL_NH_PATH_HASH_KEY;

  PLNextHopConsistentHash() = delete;
  PLNextHopConsistentHash(const std::string_view name, const YAML::Node &n);
  ~PLNextHopConsistentHash();
  void next(TSHttpTxn txnp, void *strategyTxn, const char **out_hostname, size_t *out_hostname_len, in_port_t *out_port,
            bool *out_retry, bool *out_no_cache, time_t now = 0) override;
  void mark(TSHttpTxn txnp, void *strategyTxn, const char *hostname, const size_t hostname_len, const in_port_t port,
            const PLNHCmd status, const time_t now) override;
  void *newTxn() override;
  void deleteTxn(void *txn) override;
};
