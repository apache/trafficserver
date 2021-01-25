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

/**
 * @file fetch_policy_lru.h
 * @brief LRU fetch policy (header file).
 */

#pragma once

#include "tscore/ink_defs.h"
#include "fetch_policy.h"
#include <openssl/sha.h>
#ifndef HAVE_SHA1
#include <openssl/evp.h>
#endif

/* Here reusing some of the classes used in cache_promote plugin.
 * @todo: this was done in interest of time, see if LRU is what we really need, can we do it differently / better? */
class LruHash
{
  friend struct LruHashHasher;

public:
  LruHash() {}
  ~LruHash() {}
  LruHash &
  operator=(const LruHash &h)
  {
    memcpy(_hash, h._hash, sizeof(_hash));
    return *this;
  }

  void
  init(const char *data, int len)
  {
    // SHA1() is deprecated on OpenSSL 3, but it's faster than its replacement.
#ifdef HAVE_SHA1
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    SHA1(reinterpret_cast<const unsigned char *>(data), len, _hash);
#pragma GCC diagnostic pop
#else
    EVP_Digest(data, len, _hash, nullptr, EVP_sha1(), nullptr);
#endif
  }

private:
  u_char _hash[SHA_DIGEST_LENGTH];
};

struct LruHashHasher {
  bool
  operator()(const LruHash *s1, const LruHash *s2) const
  {
    return 0 == memcmp(s1->_hash, s2->_hash, sizeof(s2->_hash));
  }

  size_t
  operator()(const LruHash *s) const
  {
    return *((size_t *)s->_hash) ^ *((size_t *)(s->_hash + 9));
  }
};

typedef LruHash LruEntry;
typedef std::list<LruEntry> LruList;
typedef std::unordered_map<const LruHash *, LruList::iterator, LruHashHasher, LruHashHasher> LruMap;
typedef LruMap::iterator LruMapIterator;

static LruEntry NULL_LRU_ENTRY; // Used to create an "empty" new LRUEntry

/**
 * @brief Fetch policy that allows fetches only for not-"hot" objects.
 *
 * Trying to identify "hot" object by keeping track of most recently used objects and
 * allows fetches only when a URL is not found in the most recently used set.
 */
class FetchPolicyLru : public FetchPolicy
{
public:
  /* Default size values are also considered minimum. TODO: find out if this works OK. */
  FetchPolicyLru(){};
  ~FetchPolicyLru() override{};

  /* Fetch policy interface methods */
  bool init(const char *parameters) override;
  bool acquire(const std::string &url) override;
  bool release(const std::string &url) override;
  const char *name() override;
  size_t getMaxSize() override;
  size_t getSize() override;

protected:
  LruMap _map;
  LruList _list;
  LruList::size_type _maxSize = 10;
  LruList::size_type _size    = 0;
};
