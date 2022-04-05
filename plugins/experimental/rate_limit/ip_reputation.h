/** @file

  Include file for all the IP reputation classes.

  @section license License

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

#include <string>
#include <cstdint>
#include <tuple>
#include <unordered_map>
#include <list>
#include <vector>
#include <chrono>
#include <arpa/inet.h>

#include "ts/ts.h"

namespace IpReputation
{
using KeyClass    = uint64_t;
using SystemClock = std::chrono::system_clock;

// Key / Count / bucket # (rank, 0-<n>) / time added
using LruEntry = std::tuple<KeyClass, uint32_t, uint32_t, std::chrono::time_point<SystemClock>>;

// This is a wrapper around a std::list which lets us size limit the list to a
// certain size.
class SieveBucket : public std::list<LruEntry>
{
public:
  SieveBucket(uint32_t max_size) : _max_size(max_size) {}

  bool
  full() const
  {
    return (_max_size > 0 ? (size() >= _max_size) : false);
  }

  size_t
  max_size() const
  {
    return _max_size;
  }

  // Move an element to the top of an LRU. This *can* move it from the current LRU (bucket)
  // to another, when promoted to a higher rank.
  void
  moveTop(SieveBucket *source_lru, SieveBucket::iterator &item)
  {
    splice(begin(), *source_lru, item);
  }

  // Debugging tools
  size_t memorySize() const;

private:
  uint32_t _max_size;
};

using HashMap = std::unordered_map<KeyClass, SieveBucket::iterator>; // The hash map for finding the entry

// This is a concept / POC: Ranked LRU buckets
//
// Also, obviously the std::string here is not awesome, rather, we ought to save the
// hashed value from the IP as the key (just like the hashed in cache_promote).
class SieveLru
{
public:
  SieveLru() : _lock(TSMutexCreate()){}; // The unitialized version
  SieveLru(uint32_t num_buckets, uint32_t size);
  ~SieveLru()
  {
    for (uint32_t i = 0; i <= _num_buckets + 1; ++i) { // Remember to delete the two special allow/block buckets too
      delete _buckets[i];
    }
  }

  void initialize(uint32_t num_buckets = 10, uint32_t size = 15);

  // Return value is the bucket (0 .. num_buckets) that the IP is in, and the
  // current count of "hits". The lookup version is similar, except it doesn't
  // modify the status of the IP (read-only).
  std::tuple<uint32_t, uint32_t> increment(KeyClass key);

  std::tuple<uint32_t, uint32_t>
  increment(const sockaddr *sock)
  {
    return increment(hasher(sock));
  }

  // Move an IP to the perm-block or perma-allow LRUs. A zero (default) maxage is indefinite (no timeout).
  uint32_t
  block(KeyClass key)
  {
    return move_bucket(key, blockBucket());
  }

  uint32_t
  allow(KeyClass key)
  {
    return move_bucket(key, allowBucket());
  }

  uint32_t
  block(const sockaddr *sock)
  {
    return move_bucket(hasher(sock), blockBucket());
  }

  uint32_t
  allow(const sockaddr *sock)
  {
    return move_bucket(hasher(sock), allowBucket());
  }

  // Lookup the current state of an IP
  std::tuple<uint32_t, uint32_t> lookup(KeyClass key) const;

  std::tuple<uint32_t, uint32_t>
  lookup(const sockaddr *sock) const
  {
    return lookup(hasher(sock));
  }

  // A helper function to hash an INET or INET6 sockaddr to a 64-bit hash.
  static uint64_t hasher(const sockaddr *sock);
  static uint64_t hasher(const std::string &ip, u_short family = AF_INET);

  // Identifying some of the special buckets:
  //
  //   entryBucket == the highest bucket, where new IPs enter (also the biggest bucket)
  //   lastBucket  == the last bucket, which is most likely to be abusive
  //   blockBucket == the bucket where we "permanently" block bad IPs
  //   allowBucket == the bucket where we "permanently" allow good IPs (can not be blocked)
  uint32_t
  entryBucket() const
  {
    return _num_buckets;
  }

  constexpr uint32_t
  lastBucket() const
  {
    return 1;
  }

  constexpr uint32_t
  blockBucket() const
  {
    return 0;
  }

  uint32_t
  allowBucket() const
  {
    return _num_buckets + 1;
  }

  size_t
  bucketSize(uint32_t bucket) const
  {
    if (bucket <= (_num_buckets + 1)) {
      return _buckets[bucket]->size();
    } else {
      return 0;
    }
  }

  bool
  initialized() const
  {
    return _initialized;
  }

  // Aging getters and setters
  std::chrono::seconds
  maxAge() const
  {
    return _max_age;
  }

  std::chrono::seconds
  permaMaxAge() const
  {
    return _perma_max_age;
  }

  void
  maxAge(std::chrono::seconds maxage)
  {
    _max_age = maxage;
  }

  void
  permaMaxAge(std::chrono::seconds maxage)
  {
    _perma_max_age = maxage;
  }

  // Debugging tool, dumps some info around the buckets
  void dump();
  size_t memoryUsed() const;

protected:
  int32_t move_bucket(KeyClass key, uint32_t to_bucket);

private:
  HashMap _map;
  std::vector<SieveBucket *> _buckets;
  uint32_t _num_buckets               = 10;                           // Leave this at 10 ...
  uint32_t _size                      = 0;                            // Set this up to initialize
  std::chrono::seconds _max_age       = std::chrono::seconds::zero(); // Aging time in the SieveLru (default off)
  std::chrono::seconds _perma_max_age = std::chrono::seconds::zero(); // Aging time in the SieveLru for perma-blocks
  bool _initialized                   = false;                        // If this has been properly initialized yet
  TSMutex _lock;                                                      // The lock around all data access
};

} // namespace IpReputation
