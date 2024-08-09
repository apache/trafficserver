/** @file cache.cc

  Metadata cache to store object sizes.

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

#include "ObjectSizeCache.h"
#include <cassert>

ObjectSizeCache::ObjectSizeCache(cache_size_type cache_size)
  : _cache_capacity(cache_size), _urls(cache_size), _object_sizes(cache_size, 0), _visits(cache_size, false)
{
}

std::optional<uint64_t>
ObjectSizeCache::get(const std::string_view url)
{
  std::lock_guard lock{_mutex};
  if (auto it = _index.find(url); it != _index.end()) {
    // Cache hit
    cache_size_type i = it->second;
    _visits[i]        = true;
    assert(url == _urls[i]);
    return _object_sizes[i];
  } else {
    // Cache miss
    return std::nullopt;
  }
}

void
ObjectSizeCache::set(const std::string_view url, uint64_t object_size)
{
  std::lock_guard lock{_mutex};
  cache_size_type i;
  if (auto it = _index.find(url); it != _index.end()) {
    // Already exists in cache.  Overwrite.
    i = it->second;
  } else {
    // Doesn't exist in cache.  Evict something else.
    find_eviction_slot();
    i                = _hand;
    _urls[i]         = url;
    _index[_urls[i]] = _hand;
    _hand++;
    if (_hand >= _cache_capacity) {
      _hand = 0;
    }
  }
  _object_sizes[i] = object_size;
}

void
ObjectSizeCache::remove(const std::string_view url)
{
  std::lock_guard lock{_mutex};
  if (auto it = _index.find(url); it != _index.end()) {
    cache_size_type i = it->second;
    _visits[i]        = false;
    _urls[i].erase();
    _index.erase(it);
  }
}

/**
 * @brief Make _hand point to the next entry that should be replaced, and clear that entry if it exists.
 *
 */
void
ObjectSizeCache::find_eviction_slot()
{
  while (_visits[_hand]) {
    _visits[_hand] = false;
    _hand++;
    if (_hand >= _cache_capacity) {
      _hand = 0;
    }
  }

  std::string_view evicted_url = _urls[_hand];
  if (!evicted_url.empty()) {
    auto it = _index.find(evicted_url);
    assert(it != _index.end());
    _index.erase(it);
    _urls[_hand].erase();
  }
}

ObjectSizeCache::cache_size_type
ObjectSizeCache::cache_capacity()
{
  return _cache_capacity;
}

ObjectSizeCache::cache_size_type
ObjectSizeCache::cache_count()
{
  return _index.size();
}
