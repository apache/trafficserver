/** @file

  Implementation details for the IP reputation classes.

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

#include <iostream>
#include <cmath>

#include "ip_reputation.h"

namespace IpReputation
{
// These static class members are here to calculate a uint64_t hash of an IP
uint64_t
SieveLru::hasher(const sockaddr *sock)
{
  switch (sock->sa_family) {
  case AF_INET: {
    const auto *sa = reinterpret_cast<const sockaddr_in *>(sock);

    return (0xffffffff00000000 | sa->sin_addr.s_addr);
  } break;
  case AF_INET6: {
    const auto *sa6 = reinterpret_cast<const sockaddr_in6 *>(sock);

    return (*reinterpret_cast<uint64_t const *>(sa6->sin6_addr.s6_addr) ^
            *reinterpret_cast<uint64_t const *>(sa6->sin6_addr.s6_addr + sizeof(uint64_t)));
  } break;
  default:
    // Clearly shouldn't happen ...
    return 0;
    break;
  }
}

uint64_t
SieveLru::hasher(const std::string &ip, u_short family) // Mostly a convenience function for testing
{
  switch (family) {
  case AF_INET: {
    sockaddr_in sa4;

    if (inet_pton(AF_INET, ip.c_str(), &(sa4.sin_addr))) {
      sa4.sin_family = AF_INET;
      return hasher(reinterpret_cast<const sockaddr *>(&sa4));
    }
  } break;
  case AF_INET6: {
    sockaddr_in6 sa6;

    if (inet_pton(AF_INET6, ip.c_str(), &(sa6.sin6_addr))) {
      sa6.sin6_family = AF_INET6;
      return hasher(reinterpret_cast<const sockaddr *>(&sa6));
    }
  } break;
  default:
    break;
  }

  return 0; // Probably can't happen, but have to return something
}

bool
SieveLru::parseYaml(const YAML::Node &node)
{
  if (node["buckets"]) {
    _num_buckets = node["buckets"].as<uint32_t>();
  }

  if (node["size"]) {
    _size = node["size"].as<uint32_t>();
  }

  if (node["percentage"]) {
    _percentage = node["percentage"].as<uint32_t>();
  }

  if (node["max_age"]) {
    _max_age = std::chrono::seconds(node["max_age"].as<uint32_t>());
  }

  if (node["perma-block"]) {
    const YAML::Node &perma = node["perma-block"];

    if (perma.IsMap()) {
      if (perma["limit"]) {
        _permablock_limit = perma["limit"].as<uint32_t>();
      }

      if (perma["threshold"]) {
        _permablock_threshold = perma["threshold"].as<uint32_t>();
      }

      if (perma["max_age"]) {
        _permablock_max_age = std::chrono::seconds(perma["max_age"].as<uint32_t>());
      }
    } else {
      TSError("[%s] The perma-block node must be a map", PLUGIN_NAME);
      return false;
    }
  }

  uint32_t cur_size = pow(2, 1 + _size - _num_buckets);

  _map.reserve(pow(2, _size + 1));    // Allow for all the sieve LRUs
  _buckets.reserve(_num_buckets + 1); // One extra bucket, for the deny list

  // Create the other buckets, in smaller and smaller sizes (power of 2)
  for (uint32_t i = lastBucket(); i <= entryBucket(); ++i) {
    _buckets[i]  = new SieveBucket(cur_size);
    cur_size    *= 2;
  }
  _buckets[blockBucket()] = new SieveBucket(cur_size / 2); // Block LRU, same size as entry bucket

  Dbg(dbg_ctl, "Loaded IP-Reputation rule: %s(%u, %u, %u, %ld)", _name.c_str(), _num_buckets, _size, _percentage,
      static_cast<long>(_max_age.count()));
  Dbg(dbg_ctl, "\twith perma-block rule: %s(%u, %u, %ld)", _name.c_str(), _permablock_limit, _permablock_threshold,
      static_cast<long>(_permablock_max_age.count()));

  _initialized = true;

  return true;
}

// Increment the count for an element (will be created / added if new).
std::tuple<uint32_t, uint32_t>
SieveLru::increment(KeyClass key)
{
  TSMutexLock(_lock);
  TSAssert(_initialized);

  auto map_it = _map.find(key);

  if (_map.end() == map_it) {
    // This is a new entry, this can only be added to the last LRU bucket
    SieveBucket *lru = _buckets[entryBucket()];

    if (lru->full()) { // The LRU is full, replace the last item with a new one
      auto last                                 = std::prev(lru->end());
      auto &[l_key, l_count, l_bucket, l_added] = *last;

      lru->moveTop(lru, last);
      _map.erase(l_key);
      *last = {key, 1, entryBucket(), SystemClock::now()};
    } else {
      // Create a new entry, the date is not used now (unless perma blocked), but could be useful for aging out stale
      // elements.
      lru->push_front({key, 1, entryBucket(), SystemClock::now()});
    }
    _map[key] = lru->begin();
    TSMutexUnlock(_lock);

    return {entryBucket(), 1};
  } else {
    auto &[map_key, map_item]              = *map_it;
    auto &[list_key, count, bucket, added] = *map_item;
    auto lru                               = _buckets[bucket];
    auto max_age                           = (bucket == blockBucket() ? _permablock_max_age : _max_age);

    // Check if the entry is older than max_age (if set), if so just move it to the entry bucket and restart
    // Yes, this will move likely abusive IPs but they will earn back a bad reputation; The goal here is to
    // not let "spiked" entries sit in small buckets indefinitely. It also cleans up the code. We only check
    // the actual system time every 10 request for an IP, if traffic is less frequent than that, the LRU will
    // age it out properly.
    if ((_max_age > std::chrono::seconds::zero()) && ((count % 10) == 0) &&
        (std::chrono::duration_cast<std::chrono::seconds>(SystemClock::now() - added) > max_age)) {
      auto last_lru = _buckets[entryBucket()];

      count  >>= 3; // Age the count by a factor of 1/8th
      bucket   = entryBucket();
      last_lru->moveTop(lru, map_item);
    } else {
      ++count;

      if (bucket > lastBucket()) {         // Not in the smallest bucket, so we may promote
        auto p_lru = _buckets[bucket - 1]; // Move to previous bucket

        if (!p_lru->full()) {
          p_lru->moveTop(lru, map_item);
          --bucket;
        } else {
          auto p_item                               = std::prev(p_lru->end());
          auto &[p_key, p_count, p_bucket, p_added] = *p_item;

          if (p_count <= count) {
            // Swap places on the two elements, moving both to the top of their respective LRU buckets
            p_lru->moveTop(lru, map_item);
            lru->moveTop(p_lru, p_item);
            --bucket;
            ++p_bucket;
          }
        }
      } else {
        // Just move it to the top of the current LRU
        lru->moveTop(lru, map_item);
      }
    }
    TSMutexUnlock(_lock);

    return {bucket, count};
  }
}

// Lookup the status of the IP in the current tables, without modifying anything
std::tuple<uint32_t, uint32_t>
SieveLru::lookup(KeyClass key) const
{
  TSMutexLock(_lock);
  TSAssert(_initialized);

  auto map_it = _map.find(key);

  if (_map.end() == map_it) {
    TSMutexUnlock(_lock);

    return {0, entryBucket()}; // Nothing found, return 0 hits and the entry bucket #
  } else {
    auto &[map_key, map_item]              = *map_it;
    auto &[list_key, count, bucket, added] = *map_item;

    TSMutexUnlock(_lock);

    return {bucket, count};
  }
}

// A little helper function, to properly move an IP to one of the two special buckets,
// allow-bucket and block-bucket.
int32_t
SieveLru::move_bucket(KeyClass key, uint32_t to_bucket)
{
  TSMutexLock(_lock);
  TSAssert(_initialized);

  auto map_it = _map.find(key);

  if (_map.end() == map_it) {
    // This is a new entry, add it directly to the special bucket
    SieveBucket *lru = _buckets[to_bucket];

    if (lru->full()) { // The LRU is full, replace the last item with a new one
      auto last                                 = std::prev(lru->end());
      auto &[l_key, l_count, l_bucket, l_added] = *last;

      lru->moveTop(lru, last);
      _map.erase(l_key);
      *last = {key, 1, to_bucket, SystemClock::now()};
    } else {
      // Create a new entry
      lru->push_front({key, 1, to_bucket, SystemClock::now()});
    }
    _map[key] = lru->begin();
  } else {
    auto &[map_key, map_item]              = *map_it;
    auto &[list_key, count, bucket, added] = *map_item;
    auto lru                               = _buckets[bucket];

    if (bucket != to_bucket) { // Make sure it's not already blocked
      auto move_lru = _buckets[to_bucket];

      // Free a space for a new entry, if needed
      if (move_lru->size() >= move_lru->max_size()) {
        auto d_entry                              = std::prev(move_lru->end());
        auto &[d_key, d_count, d_bucket, d_added] = *d_entry;

        move_lru->erase(d_entry);
        _map.erase(d_key);
      }
      move_lru->moveTop(lru, map_item); // Move the LRU item to the perma-blocks
      bucket = to_bucket;
      added  = SystemClock::now();
    }
  }
  TSMutexUnlock(_lock);

  return to_bucket; // Just as a convenience, return the destination bucket for this entry
}

void
SieveLru::dump()
{
  TSMutexLock(_lock);
  TSAssert(_initialized);

  for (uint32_t i = 0; i < _num_buckets + 1; ++i) {
    int64_t cnt = 0, sum = 0;
    auto    lru = _buckets[i];

    std::cout << '\n' << "Dumping bucket " << i << " (size=" << lru->size() << ", max_size=" << lru->max_size() << ")" << '\n';
    for (auto &it : *lru) {
      auto &[key, count, bucket, added] = it;

      ++cnt;
      sum += count;
#if 0
      if (0 == i) { // Also dump the content of the top bucket
        std::cout << "\t" << key << "; Count=" << count << ", Bucket=" << bucket << "\n";
      }
#endif
    }

    std::cout << "\tAverage count=" << (cnt > 0 ? sum / cnt : 0) << '\n';
  }
  TSMutexUnlock(_lock);
}

// Debugging tools, these memory sizes are best guesses to how much memory the containers will actually use
size_t
SieveBucket::memorySize() const
{
  size_t total = sizeof(SieveBucket);

  total += size() * (2 * sizeof(void *) + sizeof(LruEntry)); // Double linked list + object

  return total;
}

size_t
SieveLru::memoryUsed() const
{
  TSMutexLock(_lock);
  TSAssert(_initialized);

  size_t total = sizeof(SieveLru);

  for (uint32_t i = 0; i <= _num_buckets + 1; ++i) {
    total += _buckets[i]->memorySize();
  }

  total += _map.size() * (sizeof(void *) + sizeof(SieveBucket::iterator));
  total += _map.bucket_count() * (sizeof(size_t) + sizeof(void *));
  TSMutexUnlock(_lock);

  return total;
}

} // namespace IpReputation
