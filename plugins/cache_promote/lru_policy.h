/*
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
#pragma once

#include "tscore/ink_defs.h"

#include <openssl/sha.h>
#ifndef HAVE_SHA1
#include <openssl/evp.h>
#endif
#include <cstring>
#include <unordered_map>
#include <list>
#include <tuple>

#include "policy.h"

#define MINIMUM_BUCKET_SIZE 10

//////////////////////////////////////////////////////////////////////////////////////////////
// The LRU based policy keeps track of <bucket> number of URLs, with a counter for each slot.
// Objects are not promoted unless the counter reaches <hits> before it gets evicted. An
// optional <chance> parameter can be used to sample hits, this can reduce contention and
// churning in the LRU as well.
//
class LRUHash
{
  friend struct LRUHashHasher;

public:
  LRUHash() { TSDebug(PLUGIN_NAME, "LRUHash() CTOR"); }
  ~LRUHash() { TSDebug(PLUGIN_NAME, "~LRUHash() DTOR"); }

  LRUHash(const LRUHash &h)
  {
    TSDebug(PLUGIN_NAME, "Copy CTOR an LRUHash object");
    memcpy(_hash, h._hash, sizeof(_hash));
  }

  LRUHash &
  operator=(const LRUHash &h)
  {
    TSDebug(PLUGIN_NAME, "copying an LRUHash object");
    if (this != &h) {
      memcpy(_hash, h._hash, sizeof(_hash));
    }
    return *this;
  }

  // Initialize the hash key from the TXN's URL
  bool initFromUrl(TSHttpTxn txnp);

private:
  u_char _hash[SHA_DIGEST_LENGTH];
};

struct LRUHashHasher {
  bool
  operator()(const LRUHash *s1, const LRUHash *s2) const
  {
    return 0 == memcmp(s1->_hash, s2->_hash, sizeof(s2->_hash));
  }

  size_t
  operator()(const LRUHash *s) const
  {
    return *(reinterpret_cast<const size_t *>(s->_hash)) ^ *(reinterpret_cast<const size_t *>(s->_hash + 9));
  }
};

using LRUEntry = std::tuple<LRUHash, unsigned, int64_t>;
using LRUList  = std::list<LRUEntry>;
using LRUMap   = std::unordered_map<const LRUHash *, LRUList::iterator, LRUHashHasher, LRUHashHasher>;

class LRUPolicy : public PromotionPolicy
{
public:
  LRUPolicy() : PromotionPolicy(), _lock(TSMutexCreate()) {}
  ~LRUPolicy() override;

  bool parseOption(int opt, char *optarg) override;
  bool doPromote(TSHttpTxn txnp) override;
  bool stats_add(const char *remap_id) override;
  void addBytes(TSHttpTxn txnp) override;

  bool
  countBytes() const override
  {
    return _bytes > 0;
  }

  void
  usage() const override
  {
    TSError("[%s] Usage: @plugin=%s.so @pparam=--policy=lru @pparam=--buckets=<m> --hits=<n> --bytes=<o> --sample=<p>", PLUGIN_NAME,
            PLUGIN_NAME);
  }

  const char *
  policyName() const override
  {
    return "LRU";
  }

  const std::string
  id() const override
  {
    return _label + ";LRU=b:" + std::to_string(_buckets) + ",h:" + std::to_string(_hits) + ",B:" + std::to_string(_bytes) +
           ",i:" + std::to_string(_internal_enabled);
  }

  void
  cleanup(TSHttpTxn txnp) override
  {
    LRUHash *hash = static_cast<LRUHash *>(TSUserArgGet(txnp, TXN_ARG_IDX));

    // Delete the hash, and remove the pointer from the TXN user arg slot (to be safe)
    if (hash) {
      delete hash;
      TSUserArgSet(txnp, TXN_ARG_IDX, nullptr);
    }
  }

private:
  unsigned _buckets  = 1000;
  unsigned _hits     = 10;
  int64_t _bytes     = 0;
  std::string _label = "";

  // For the LRU. Note that we keep track of the List sizes, because some versions fo STL have broken
  // implementations of size(), making them obsessively slow on calling ::size().
  TSMutex _lock;
  LRUMap _map;
  LRUList _list, _freelist;
  size_t _list_size = 0, _freelist_size = 0;

  // internal stats ids
  int _freelist_size_id = -1;
  int _lru_size_id      = -1;
  int _lru_hit_id       = -1;
  int _lru_miss_id      = -1;
  int _lru_vacated_id   = -1;
  int _promoted_id      = -1;
};
