/** @file cache.h

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

#include <optional>
#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>
#include <mutex>

class ObjectSizeCache
{
public:
  using cache_size_type  = size_t;
  using object_size_type = uint64_t;

  ObjectSizeCache(cache_size_type cache_size);

  /**
   * @brief Get an object size from cache.
   *
   * @param url The URL of the object
   * @return std::optional<uint64_t> If the object size was found, return the size of the object.  If not, return std::nullopt.
   */
  std::optional<object_size_type> get(const std::string_view url);

  /**
   * @brief Add an object size to cache.
   *
   * @param url The URL of the object
   * @param object_size The size of the object
   */
  void set(const std::string_view url, object_size_type object_size);

  cache_size_type cache_size();

  std::tuple<uint64_t, uint64_t, uint64_t, uint64_t> cache_stats();

private:
  void find_eviction_slot();

  cache_size_type _cache_size;
  cache_size_type _hand{0};
  std::vector<std::string> _urls;
  std::vector<object_size_type> _object_sizes;
  std::vector<bool> _visits;
  std::unordered_map<std::string_view, cache_size_type> _index;
  std::mutex _mutex;

  uint64_t _cache_hits         = 0;
  uint64_t _cache_misses       = 0;
  uint64_t _cache_write_hits   = 0;
  uint64_t _cache_write_misses = 0;
};
