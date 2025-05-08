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

#include <yaml-cpp/yaml.h>
#include "ts/ts.h"
#include "utilities.h"

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
  using self_type = SieveBucket;

public:
  SieveBucket(uint32_t max_size) : _max_size(max_size) {}

  SieveBucket()                           = delete;
  SieveBucket(self_type &&)               = delete;
  self_type &operator=(const self_type &) = delete;
  self_type &operator=(self_type &&)      = delete;

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
  using self_type = SieveLru;

public:
  SieveLru(std::string &name) : _lock(TSMutexCreate()) { _name = name; }

  SieveLru()                              = delete;
  SieveLru(self_type &&)                  = delete;
  self_type &operator=(const self_type &) = delete;
  self_type &operator=(self_type &&)      = delete;

  ~SieveLru() {}

  bool parseYaml(const YAML::Node &node);

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
  block(const sockaddr *sock)
  {
    return move_bucket(hasher(sock), blockBucket());
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

  const std::string &
  name() const
  {
    return _name;
  }

  uint32_t
  numBuckets() const
  {
    return _num_buckets;
  }

  uint32_t
  size() const
  {
    return _size;
  }

  uint32_t
  percentage() const
  {
    return _percentage;
  }

  uint32_t
  permablock_count() const
  {
    return _permablock_limit;
  }

  uint32_t
  permablock_threshold() const
  {
    return _permablock_threshold;
  }

  std::chrono::seconds
  maxAge() const
  {
    return _max_age;
  }

  std::chrono::seconds
  permaMaxAge() const
  {
    return _permablock_max_age;
  }

  // Debugging tool, dumps some info around the buckets
  void   dump();
  size_t memoryUsed() const;

protected:
  int32_t move_bucket(KeyClass key, uint32_t to_bucket);

private:
  HashMap                                   _map;
  std::vector<std::unique_ptr<SieveBucket>> _buckets;
  std::string                               _name;
  bool                                      _initialized = false; // If this has been properly initialized yet
  TSMutex                                   _lock;                // The lock around all data access
  // Standard options
  uint32_t             _num_buckets = 10;                           // Leave this at 10 ...
  uint32_t             _size        = 0;                            // Set this up to initialize
  uint32_t             _percentage  = 90;                           // At what percentage of limit do we start blocking
  std::chrono::seconds _max_age     = std::chrono::seconds::zero(); // Aging time in the SieveLru (default off)
  // Perma-block options
  uint32_t             _permablock_limit     = 0;                            // "Hits" limit for blocking permanently
  uint32_t             _permablock_threshold = 0;                            // Pressure threshold for permanent block
  std::chrono::seconds _permablock_max_age   = std::chrono::seconds::zero(); // Aging time in the SieveLru for perma-blocks
};

} // namespace IpReputation
