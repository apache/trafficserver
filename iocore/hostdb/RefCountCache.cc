/** @file

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

#include <P_RefCountCache.h>

// Since the hashing values are all fixed size, we can simply use a classAllocator to avoid mallocs
static ClassAllocator<RefCountCacheHashEntry> refCountCacheHashingValueAllocator("refCountCacheHashingValueAllocator");

ClassAllocator<PriorityQueueEntry<RefCountCacheHashEntry *>> expiryQueueEntry("expiryQueueEntry");

RefCountCacheHashEntry *
RefCountCacheHashEntry::alloc()
{
  return refCountCacheHashingValueAllocator.alloc();
}

void
RefCountCacheHashEntry::dealloc(RefCountCacheHashEntry *e)
{
  return refCountCacheHashingValueAllocator.free(e);
}

RefCountCacheHeader::RefCountCacheHeader(ts::VersionNumber object_version)
  : magic(REFCOUNTCACHE_MAGIC_NUMBER), object_version(object_version){};

bool
RefCountCacheHeader::operator==(const RefCountCacheHeader other) const
{
  return this->magic == other.magic && this->version == other.version;
}

bool
RefCountCacheHeader::compatible(RefCountCacheHeader *other) const
{
  return (this->magic == other->magic && this->version._major == other->version._major &&
          this->object_version._major == other->version._major);
};
