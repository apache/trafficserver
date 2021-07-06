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

#include "tscore/ink_assert.h"
#include "tscore/ink_atomic.h"
#include "tscore/ink_resource.h"
#include <execinfo.h>

int res_track_memory          = 0; // Disabled by default
uint64_t ssl_memory_allocated = 0;
uint64_t ssl_memory_freed     = 0;

std::map<const char *, Resource *> ResourceTracker::_resourceMap;
ink_mutex ResourceTracker::resourceLock = PTHREAD_MUTEX_INITIALIZER;

/**
 * Individual resource to keep track of.  A map of these are in the ResourceTracker.
 */
class Resource
{
public:
  Resource() { _name[0] = '\0'; }
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
  void
  setSymbol(const void *symbol)
  {
    _symbol = symbol;
  }
  void
  setName(const char *name)
  {
    strncpy(_name, name, sizeof(_name));
    _name[sizeof(_name) - 1] = '\0';
  }
  void
  setName(const void *symbol, const char *name)
  {
    Dl_info info;
    dladdr(const_cast<void *>(symbol), &info);
    snprintf(_name, sizeof(_name), "%s/%s", name, info.dli_sname);
  }
  const char *
  getName() const
  {
    return _name;
  }
  const void *
  getSymbol() const
  {
    return _symbol;
  }

private:
  int64_t _incrementCount = 0;
  int64_t _decrementCount = 0;
  int64_t _value          = 0;
  const void *_symbol     = nullptr;
  char _name[128];
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
  Resource &resource      = lookup(name);
  const char *lookup_name = resource.getName();
  if (lookup_name[0] == '\0') {
    resource.setName(name);
  }
  resource.increment(size);
}

void
ResourceTracker::increment(const void *symbol, const int64_t size, const char *name)
{
  Resource &resource = lookup(static_cast<const char *>(symbol));
  if (resource.getSymbol() == nullptr && name != nullptr) {
    resource.setName(symbol, name);
    resource.setSymbol(symbol);
  }
  resource.increment(size);
}

Resource &
ResourceTracker::lookup(const char *name)
{
  Resource *resource = nullptr;
  ink_mutex_acquire(&resourceLock);
  std::map<const char *, Resource *>::iterator it = _resourceMap.find(name);
  if (it != _resourceMap.end()) {
    resource = it->second;
  } else {
    // create a new entry
    resource           = new Resource;
    _resourceMap[name] = resource;
  }
  ink_mutex_release(&resourceLock);
  ink_release_assert(resource != nullptr);
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
    for (std::map<const char *, Resource *>::const_iterator it = _resourceMap.begin(); it != _resourceMap.end(); ++it) {
      const Resource &resource = *it->second;
      int64_t average_size     = 0;
      if (resource.getIncrement() - resource.getDecrement() > 0) {
        average_size = resource.getValue() / (resource.getIncrement() - resource.getDecrement());
      }
      fprintf(fd, "%10" PRId64 " | %10" PRId64 " | %20" PRId64 " | %10" PRId64 " | %-50s\n", resource.getIncrement(),
              resource.getDecrement(), resource.getValue(), average_size, resource.getName());
      total += resource.getValue();
    }
    fprintf(fd, "                          %20" PRId64 " |            | %-50s\n", total, "TOTAL");
    fprintf(fd, "--------------------------------------------------------------"
                "--------------------------------------------------------------------\n");
  }

  ink_mutex_release(&resourceLock);

  if (res_track_memory >= 2) {
    fprintf(fd, "\n%-20s | %-20s | %-20s | %-20s\n", "Total Allocated", "Total Freed", "Currently Allocated", "Type");
    fprintf(fd, "---------------------|----------------------|----------------------|----------------------\n");
    fprintf(fd, "%20" PRId64 " | %20" PRId64 " | %20" PRId64 " | %-50s\n", ssl_memory_allocated, ssl_memory_freed,
            ssl_memory_allocated - ssl_memory_freed, "SSL Allocated Memory");
    fprintf(fd, "---------------------|----------------------|----------------------|----------------------\n");
  }
}
