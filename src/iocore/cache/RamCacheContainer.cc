/** @file

  A cache wrapper that duplicates the underlying cache onto several NUMA nodes

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

#include "P_Cache.h"
#include <array>
#include <mutex>
#include "tscore/NUMADebug.h"
#include "tscore/ink_thread.h"
#include <numaif.h>

class RamCacheContainer : public RamCache
{
  std::vector<RamCache *> caches;

  int64_t   max_bytes   = 0;
  StripeSM *stripe      = nullptr;
  bool      init_called = false;
  RamCache *get_cache(unsigned int my_node, unsigned int node);

public:
  void init_one_cache();
  // returns 1 on found/stored, 0 on not found/stored, if provided auxkey1 and auxkey2 must match
  int     get(CryptoHash *key, Ptr<IOBufferData> *ret_data, uint64_t auxkey = 0) override;
  int     put(CryptoHash *key, IOBufferData *data, uint32_t len, bool copy = false, uint64_t auxkey = 0) override;
  int     fixup(const CryptoHash *key, uint64_t old_auxkey, uint64_t new_auxkey) override;
  int64_t size() const override;
  void    init(int64_t max_bytes_, StripeSM *stripe_) override;
  RamCacheContainer();
  ~RamCacheContainer() override;
};

extern int cache_config_ram_cache_algorithm;
extern int cache_config_ram_cache_numa_duplicate;

RamCacheContainer::RamCacheContainer() : caches(numa_max_node() + 1, nullptr) {}

RamCacheContainer::~RamCacheContainer()
{
  for (auto c : caches)
    delete c;
}

RamCache *
new_RamCacheFromConfig()
{
  switch (cache_config_ram_cache_algorithm) {
  default:
  case RAM_CACHE_ALGORITHM_CLFUS:
    return new_RamCacheCLFUS();
  case RAM_CACHE_ALGORITHM_LRU:
    return new_RamCacheLRU();
  }
}

RamCache *
new_RamCacheContainer()
{
  return new RamCacheContainer;
}

void
RamCacheContainer::init_one_cache()
{
  unsigned int my_node = 0;
  getcpu(nullptr, &my_node);
  if (caches[my_node]) {
    ink_error("Attempt to double-init duplicated cache!");
  } else {
    RamCache *cache = new_RamCacheFromConfig();
    cache->init(max_bytes, stripe);
    caches[my_node] = cache;
  }
}

RamCache *
RamCacheContainer::get_cache(unsigned int my_node, unsigned int node)
{
  return caches[node];
}

static void *
ram_cache_container_thread_init_func(void *arg)
{
  ((RamCacheContainer *)arg)->init_one_cache();
  return nullptr;
}

void
RamCacheContainer::init(int64_t max_bytes_, StripeSM *stripe_)
{
  max_bytes   = max_bytes_;
  stripe      = stripe_;
  init_called = true;
  std::vector<ink_thread> threads;
  for (unsigned int node = 0; node < (unsigned int)caches.size(); ++node) {
    ink_thread  t;
    hwloc_obj_t obj = hwloc_get_obj_by_type(ink_get_topology(), HWLOC_OBJ_NODE, node);
    ink_thread_create(&t, ram_cache_container_thread_init_func, (void *)this, false, 0, nullptr, obj->cpuset);
    threads.push_back(t);
  }
  for (auto t : threads) {
    ink_thread_join(t);
  }
  for (auto cache : caches) {
    if (!cache) {
      ink_fatal("Failed to initialize NUMA local ram cache.");
    }
  }
}

constexpr size_t PAGE_SIZE = 4096;

static void *
align_pointer_to_page(const void *ptr)
{
  // Align to the page size (move_page requires that)
  return reinterpret_cast<void *>(reinterpret_cast<intptr_t>(ptr) & (~(PAGE_SIZE - 1)));
}

static size_t
page_count(const void *ptr, size_t size)
{
  auto start = reinterpret_cast<intptr_t>(ptr);
  return (start + size + (PAGE_SIZE - 1)) / PAGE_SIZE - start / PAGE_SIZE;
}

// returns true if consistent
static bool
check_pages_consistency(void *data, size_t size, const char *name = "")
{
  unsigned int my_node = 0;
  getcpu(nullptr, &my_node);

  size_t count = page_count(data, size);
  if (count == 0)
    return true;

  data = align_pointer_to_page(data);

  intptr_t            pos = reinterpret_cast<intptr_t>(data);
  std::vector<void *> pages(count);
  std::vector<int>    status(count, 0);
  for (size_t i = 0; i < count; ++i) {
    pages[i]  = reinterpret_cast<void *>(pos);
    pos      += PAGE_SIZE;
  }
  auto result = move_pages(0, count, pages.data(), nullptr, status.data(), 0);
  if (result < 0) {
    ink_notice("move_pages failed");
    return false;
  }

  bool inconsistent = false;
  if (status[0] >= 0) {
    for (size_t i = 1; i < count; ++i) {
      if (status[i] >= 0 && status[i] != status[0]) {
        inconsistent = true;
        break;
      }
    }
  }
  if (inconsistent) {
    std::string print(count, '?');
    for (size_t i = 0; i < count; ++i) {
      print[i] = '0' + status[i];
    }
    ink_notice("Inconsistent pages at %s when putting data into cache %s, execution node=%d", name, print.c_str(), my_node);
  } else {
    // ink_notice("Consistent pages at %s when putting data into cache, node=%d", name, status[0]);
  }
  return !inconsistent;
}

int
RamCacheContainer::get(CryptoHash *key, Ptr<IOBufferData> *ret_data, uint64_t auxkey)
{
  unsigned int my_node = 0;
  getcpu(nullptr, &my_node);
  // Do we have it?
  RamCache *my_cache = get_cache(my_node, my_node);
  if (!my_cache)
    return 0;
  if (my_cache->get(key, ret_data, auxkey)) {
    // From difference, can tell if its not coherent
    NUMA_CHECK((*ret_data)->data(), 0);
    NUMA_CHECK((*ret_data)->data(), (*ret_data)->block_size());
    return 1;
  }
  // Do we have it on some other node?

  for (unsigned i = 0; i < caches.size(); i++)
    if (i != my_node) {
      RamCache *cache = get_cache(my_node, i);
      if (cache && cache->get(key, ret_data, auxkey)) {
        my_cache->put(key, (*ret_data).get(), (*ret_data)->block_size(), false, auxkey);
        return 1;
      }
    }
  return 0;
}

static void
move_pages_to_numa_zone(void *data, size_t size, int dest_numa)
{
  size_t pcount = page_count(data, size);
  if (pcount == 0)
    return;

  data                    = align_pointer_to_page(data);
  intptr_t            pos = reinterpret_cast<intptr_t>(data);
  std::vector<void *> pages(pcount);
  std::vector<int>    nodes(pcount);
  std::vector<int>    status(pcount, 0);
  for (size_t i = 0; i < pcount; ++i) {
    nodes[i]  = dest_numa;
    pages[i]  = reinterpret_cast<void *>(pos);
    pos      += PAGE_SIZE;
  }
  auto result = move_pages(0, pcount, pages.data(), nodes.data(), status.data(), MPOL_MF_MOVE);
  if (result < 0) {
    ink_notice("move_pages failed");
    return;
  }
  size_t failures = 0;
  for (auto &s : status) {
    if (s < 0)
      failures++;
  }
  if (failures > 0) {
    ink_notice("move_pages_to_numa_zone had %" PRIuPTR " failures", (uintptr_t)failures);
  }
  // TODO: error reporting
  bool inconsistent = false;
  if (status[0] >= 0) {
    for (size_t i = 1; i < pcount; ++i) {
      if (status[i] >= 0 && status[i] != status[0]) {
        inconsistent = true;
        break;
      }
    }
  }
  if (inconsistent) {
    std::string print(pcount, '?');
    for (size_t i = 0; i < pcount; ++i) {
      print[i] = '0' + status[i];
    }
    ink_notice("Inconsistent pages after move_pages %s", print.c_str());
  }
}

static void
move_pages_to_current_numa_zone(void *data, size_t size)
{
  unsigned int data_numa_node = 0;
  getcpu(nullptr, &data_numa_node);
  move_pages_to_numa_zone(data, size, data_numa_node);
}

int
RamCacheContainer::put(CryptoHash *key, IOBufferData *data, uint32_t len, bool copy, uint64_t auxkey)
{
#if NUMA_CONSISTENCY_CHECK
  check_pages_consistency(data->data(), len, "Check 1");
#endif
  unsigned int my_node = 0;
  getcpu(nullptr, &my_node);

  NUMA_CHECK(data->data(), len);
  move_pages_to_numa_zone(data->data(), len, my_node);
#if NUMA_CONSISTENCY_CHECK
  check_pages_consistency(data->data(), len, "Check 2");
#endif
  return get_cache(my_node, my_node)->put(key, data, len, copy, auxkey);
}

int
RamCacheContainer::fixup(const CryptoHash *key, uint64_t old_auxkey, uint64_t new_auxkey)
{
  unsigned int my_node = 0;
  getcpu(nullptr, &my_node);
  for (size_t i = 0; i < caches.size(); i++) {
    RamCache *cache = get_cache(my_node, i);
    if (cache) {
      int result = cache->fixup(key, old_auxkey, new_auxkey);
      if (result) {
        return result;
      }
    }
  }
  return 0;
}

int64_t
RamCacheContainer::size() const
{
  int64_t result = 0;
  for (auto c : caches) {
    if (c)
      result += c->size();
  }
  return result;
}
