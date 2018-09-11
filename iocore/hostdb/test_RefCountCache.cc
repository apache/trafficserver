/** @file

  Unit tests for RefCountCache

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
#include <RefCountCache.cc>
#include <I_EventSystem.h>
#include "tscore/I_Layout.h"
#include <diags.i>
#include <set>

// TODO: add tests with expiry_time

class ExampleStruct : public RefCountObj
{
public:
  int idx;
  int name_offset; // pointer addr to name
  static std::set<ExampleStruct *> items_freed;

  // Return the char* to the name (TODO: cleaner interface??)
  char *
  name()
  {
    return (char *)this + this->name_offset;
  }

  static ExampleStruct *
  alloc(int size = 0)
  {
    return new (malloc(sizeof(ExampleStruct) + size)) ExampleStruct();
  }

  static void
  dealloc(ExampleStruct *e)
  {
    e->~ExampleStruct();
    ::free(e);
  }

  // Really free the memory, we can use asan leak detection to verify it was freed
  void
  free() override
  {
    this->idx = -1;
    items_freed.insert(this);
    printf("freeing: %p items_freed.size(): %zd\n", this, items_freed.size());
  }

  static ExampleStruct *
  unmarshall(char *buf, unsigned int size)
  {
    if (size < sizeof(ExampleStruct)) {
      return nullptr;
    }
    ExampleStruct *ret = ExampleStruct::alloc(size - sizeof(ExampleStruct));
    memcpy((void *)ret, buf, size);
    // Reset the refcount back to 0, this is a bit ugly-- but I'm not sure we want to expose a method
    // to mess with the refcount, since this is a fairly unique use case
    ret = new (ret) ExampleStruct();
    return ret;
  }
};

std::set<ExampleStruct *> ExampleStruct::items_freed;

void
fillCache(RefCountCache<ExampleStruct> *cache, int start, int end)
{
  // TODO: name per?
  std::string name = "foobar";
  int allocSize    = name.size() + 1;

  for (int i = start; i < end; i++) {
    ExampleStruct *tmp = ExampleStruct::alloc(allocSize);
    cache->put((uint64_t)i, tmp);

    tmp->idx         = i;
    tmp->name_offset = sizeof(ExampleStruct);
    memcpy(tmp->name(), name.c_str(), name.size());
    // nullptr terminate the string
    *(tmp->name() + name.size()) = '\0';

    // Print out the struct we put in there
    // printf("New ExampleStruct%d idx=%d name=%s allocSize=%d\n", i, tmp->idx, name.c_str(), allocSize);
  }
  printf("Loading complete! Cache now has %ld items.\n\n", cache->count());
}

int
verifyCache(RefCountCache<ExampleStruct> *cache, int start, int end)
{
  // Re-query all the structs to make sure they are there and accurate
  for (int i = start; i < end; i++) {
    Ptr<ExampleStruct> ccitem = cache->get(i);
    ExampleStruct *tmp        = ccitem.get();
    if (tmp == nullptr) {
      // printf("ExampleStruct %d missing, skipping\n", i);
      continue;
    }
    // printf("Get (%p) ExampleStruct%d idx=%d name=%s\n", tmp, i, tmp->idx, tmp->name());

    // Check that idx is correct
    if (tmp->idx != i) {
      printf("IDX of ExampleStruct%d incorrect! (%d)\n", i, tmp->idx);
      return 1; // TODO: spin over all?
    }

    // check that the name is correct
    // if (strcmp(tmp->name, name.c_str())){
    //  printf("Name of ExampleStruct%d incorrect! %s %s\n", i, tmp->name, name.c_str());
    //  exit(1);
    //}
  }
  return 0;
}

// TODO: check that the memory was actually free-d better
int
testRefcounting()
{
  int ret = 0;

  RefCountCache<ExampleStruct> *cache = new RefCountCache<ExampleStruct>(4);

  // Create and then immediately delete an item
  ExampleStruct *to_delete = ExampleStruct::alloc();
  ret |= to_delete->refcount() != 0;
  cache->put(1, to_delete);
  ret |= to_delete->refcount() != 1;
  cache->erase(1);
  ret |= to_delete->refcount() != 0;
  ret |= to_delete->idx != -1;

  // Set an item in the cache
  ExampleStruct *tmp = ExampleStruct::alloc();
  ret |= tmp->refcount() != 0;
  printf("ret=%d ref=%d\n", ret, tmp->refcount());
  cache->put((uint64_t)1, tmp);
  ret |= tmp->refcount() != 1;
  printf("ret=%d ref=%d\n", ret, tmp->refcount());
  tmp->idx = 1;

  // Grab a pointer to item 1
  Ptr<ExampleStruct> ccitem = cache->get((uint64_t)1);
  ret |= tmp->refcount() != 2;
  printf("ret=%d ref=%d\n", ret, tmp->refcount());

  Ptr<ExampleStruct> tmpAfter = cache->get((uint64_t)1);
  ret |= tmp->refcount() != 3;
  printf("ret=%d ref=%d\n", ret, tmp->refcount());

  // Delete a single item
  cache->erase(1);
  ret |= tmp->refcount() != 2;
  printf("ret=%d ref=%d\n", ret, tmp->refcount());
  // verify that it still isn't in there
  ret |= cache->get(1).get() != nullptr;
  printf("ret=%d ref=%d\n", ret, tmp->refcount());
  ret |= tmpAfter.get()->idx != 1;
  printf("ret=%d ref=%d\n", ret, tmp->refcount());

  delete cache;

  return ret;
}

int
testclear()
{
  int ret = 0;

  RefCountCache<ExampleStruct> *cache = new RefCountCache<ExampleStruct>(4);

  // Create and then immediately delete an item
  ExampleStruct *item = ExampleStruct::alloc();
  ret |= item->refcount() != 0;
  cache->put(1, item);
  ret |= item->refcount() != 1;
  cache->clear();
  ret |= item->refcount() != 0;
  ret |= item->idx != -1;

  return ret;
}

int
test()
{
  // Initialize IOBufAllocator
  RecModeT mode_type = RECM_STAND_ALONE;
  Layout::create();
  init_diags("", nullptr);
  RecProcessInit(mode_type);
  ink_event_system_init(EVENT_SYSTEM_MODULE_VERSION);

  int ret = 0;

  printf("Starting tests\n");

  printf("Testing refcounts\n");
  ret |= testRefcounting();
  printf("refcount ret %d\n", ret);

  // Initialize our cache
  int cachePartitions                 = 4;
  RefCountCache<ExampleStruct> *cache = new RefCountCache<ExampleStruct>(cachePartitions);
  printf("Created...\n");

  LoadRefCountCacheFromPath<ExampleStruct>(*cache, "/tmp", "/tmp/hostdb_cache", ExampleStruct::unmarshall);
  printf("Cache started...\n");
  int numTestEntries = 10000;

  // See if anything persisted across the restart
  ret |= verifyCache(cache, 0, numTestEntries);
  printf("done verifying startup\n");

  // Clear the cache
  cache->clear();
  ret |= cache->count() != 0;
  printf("clear %d\n", ret);

  // fill it
  printf("filling...\n");
  fillCache(cache, 0, numTestEntries);
  printf("filled...\n");

  // Verify that it has items
  printf("verifying...\n");
  ret |= verifyCache(cache, 0, numTestEntries);
  printf("verified %d\n", ret);

  // Verify that we can alloc() with no extra space
  printf("Alloc item idx 1\n");
  ExampleStruct *tmp = ExampleStruct::alloc();
  cache->put((uint64_t)1, tmp);
  tmp->idx = 1;

  Ptr<ExampleStruct> tmpAfter = cache->get((uint64_t)1);
  printf("Item after (ret=%d) %d %d\n", ret, 1, tmpAfter->idx);
  // Verify every item in the cache
  ret |= verifyCache(cache, 0, numTestEntries);
  printf("verified entire cache ret=%d\n", ret);

  // Grab a pointer to item 1
  Ptr<ExampleStruct> ccitem = cache->get((uint64_t)1);
  ccitem->idx               = 1;
  // Delete a single item
  cache->erase(1);
  // verify that it still isn't in there
  ret |= cache->get(1).get() != nullptr;
  ret |= ccitem.get()->idx != 1;
  printf("ret=%d\n", ret);

  // Verify every item in the cache
  ret |= verifyCache(cache, 0, numTestEntries);

  // TODO: figure out how to test syncing/loading
  // write out the whole thing
  // printf("Sync return: %d\n", cache->sync_all());

  printf("TestRun: %d\n", ret);

  delete cache;

  return ret;
}

int
main()
{
  int ret = test();

  for (const auto item : ExampleStruct::items_freed) {
    printf("really freeing: %p\n", item);
    ExampleStruct::dealloc(item);
  }
  ExampleStruct::items_freed.clear();

  return ret;
}
