/*
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

#define CRIPTS_CONVENIENCE_APIS 1

#include <cripts/CacheGroup.hpp>
#include <cripts/Preamble.hpp>

do_create_instance()
{
  // Create a cache-group for this site / remap rule(s). They can be shared.
  instance.data[0] = cripts::Cache::Group::Manager::Factory("example");
}

do_delete_instance()
{
  void *ptr = AsPointer(instance.data[0]);

  if (ptr) {
    delete static_cast<std::shared_ptr<cripts::Cache::Group> *>(ptr);
    instance.data[0] = nullptr;
  }
}

do_cache_lookup()
{
  if (cached.response.lookupstatus != cripts::LookupStatus::MISS) {
    void *ptr = AsPointer(instance.data[0]);

    if (ptr) {
      auto date = cached.response.AsDate("Date");

      if (date > 0) {
        auto cache_groups = cached.response["Cache-Groups"];

        CDebug("Looking up {}", cache_groups);
        if (!cache_groups.empty()) {
          borrow cg = *static_cast<std::shared_ptr<cripts::Cache::Group> *>(ptr);

          if (cg->Lookup(cache_groups.split(','), date)) {
            CDebug("Cache Group hit, forcing revalidation for request");
            cached.response.lookupstatus = cripts::LookupStatus::HIT_STALE;
          }
        }
      }
    }
  }
}

do_read_response()
{
  void *ptr = AsPointer(instance.data[0]);

  if (ptr) {
    auto invalidation = client.request["Cache-Group-Invalidation"];

    if (!invalidation.empty()) {
      borrow cg = *static_cast<std::shared_ptr<cripts::Cache::Group> *>(ptr);

      cg->Insert(invalidation.split(','));
    }
  }

// This is just for simulating origin responses that would include cache-groups.
#if 0
  server.response["Cache-Groups"] = "\"foo\", \"bar\"";
#endif
}

// The RFC draft does not support / provide definitions for this. It is useful,
// but should be protected with appropriate ACLs / authentication.
#if 0
do_remap()
{
  void *ptr = AsPointer(instance.data[0]);

  if (ptr && urls.pristine.path == ".well-known/Cache-Groups") {
    auto invalidation = client.request["Cache-Group-Invalidation"];

    if (!invalidation.empty()) {
      borrow cg = *static_cast<std::shared_ptr<cripts::Cache::Group> *>(ptr);

      cg->Insert(invalidation.split(','));
      CDebug("Forcing a cache miss for cache-groups: {}", invalidation);
      StatusCode(202);
    }
  }
}
#endif

#include <cripts/Epilogue.hpp>
