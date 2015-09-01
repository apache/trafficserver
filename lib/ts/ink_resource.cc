/** @file

  A brief file description

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

#include "ts/ink_assert.h"
#include "ts/ink_atomic.h"
#include "ts/ink_resource.h"
#include <execinfo.h>

volatile int res_track_memory = 0; // Disabled by default

std::map<std::string, Resource *> ResourceTracker::_resourceMap;
ink_mutex ResourceTracker::resourceLock = PTHREAD_MUTEX_INITIALIZER;

/**
 * Individual resource to keep track of.  A map of these are in the ResourceTracker.
 */
class Resource
{
public:
  Resource() : _incrementCount(0), _decrementCount(0), _value(0) {}
  void increment(const int64_t size);
  int64_t
  getValue() const
  {
    return _value;
  }
  int64_t
  getIncrement() const
  {
    return _incrementCount;
  }
  int64_t
  getDecrement() const
  {
    return _decrementCount;
  }

private:
  int64_t _incrementCount;
  int64_t _decrementCount;
  int64_t _value;
};

void
Resource::increment(const int64_t size)
{
  ink_atomic_increment(&_value, size);
  if (size >= 0) {
    ink_atomic_increment(&_incrementCount, 1);
  } else {
    ink_atomic_increment(&_decrementCount, 1);
  }
}

void
ResourceTracker::increment(const char *name, const int64_t size)
{
  Resource &resource = lookup(name);
  resource.increment(size);
}

Resource &
ResourceTracker::lookup(const char *name)
{
  Resource *resource = NULL;
  ink_mutex_acquire(&resourceLock);
  std::map<std::string, Resource *>::iterator it = _resourceMap.find(name);
  if (it != _resourceMap.end()) {
    resource = it->second;
  } else {
    // create a new entry
    resource = new Resource;
    _resourceMap[name] = resource;
  }
  ink_mutex_release(&resourceLock);
  ink_release_assert(resource != NULL);
  return *resource;
}

void
ResourceTracker::dump(FILE *fd)
{
  if (!res_track_memory) {
    return;
  }
  int64_t total = 0;

  ink_mutex_acquire(&resourceLock);
  if (!_resourceMap.empty()) {
    fprintf(fd, "\n%-10s | %-10s | %-20s | %-10s | %-50s\n", "Allocs", "Frees", "Size In-use", "Avg Size", "Location");
    fprintf(fd, "-----------|------------|----------------------|------------|"
                "--------------------------------------------------------------------\n");
    for (std::map<std::string, Resource *>::const_iterator it = _resourceMap.begin(); it != _resourceMap.end(); ++it) {
      const Resource &resource = *it->second;
      if (resource.getIncrement() - resource.getDecrement()) {
        fprintf(fd, "%10" PRId64 " | %10" PRId64 " | %20" PRId64 " | %10" PRId64 " | %-50s\n", resource.getIncrement(),
                resource.getDecrement(), resource.getValue(),
                resource.getValue() / (resource.getIncrement() - resource.getDecrement()), it->first.c_str());
        total += resource.getValue();
      }
    }
    fprintf(fd, "                          %20" PRId64 " |            | %-50s\n", total, "TOTAL");
  }
  ink_mutex_release(&resourceLock);
}
