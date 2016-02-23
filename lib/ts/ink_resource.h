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

#ifndef __INK_RESOURCE_H__
#define __INK_RESOURCE_H__

#include "ts/ink_mutex.h"
#include <map>
#include <string>

extern volatile int res_track_memory; /* set this to zero to disable resource tracking */
extern uint64_t ssl_memory_allocated;
extern uint64_t ssl_memory_freed;

#define __RES_PATH(x) #x
#define _RES_PATH(x) __RES_PATH(x)
#define RES_PATH(x) x __FILE__ ":" _RES_PATH(__LINE__)

class Resource;

/**
 * Generic class to keep track of memory usage
 * Used to keep track of the location in the code that allocated ioBuffer memory
 */
class ResourceTracker
{
public:
  ResourceTracker(){};
  static void increment(const char *name, const int64_t size);
  static void increment(const void *symbol, const int64_t size, const char *name);
  static void dump(FILE *fd);

private:
  static Resource &lookup(const char *name);
  static std::map<const char *, Resource *> _resourceMap;
  static ink_mutex resourceLock;
};

#endif /* __INK_RESOURCE_H__ */
