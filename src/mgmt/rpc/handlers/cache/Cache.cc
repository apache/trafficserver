/** @file

  Cache JSON-RPC handlers.

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

#include "mgmt/rpc/handlers/cache/Cache.h"
#include "mgmt/rpc/handlers/common/ErrorUtils.h"

#include "records/RecCore.h"

#include <limits>
#include <mutex>

namespace
{
constexpr char CACHE_GENERATION_RECORD[] = "proxy.config.http.cache.generation";
std::mutex     generation_mutex;
} // namespace

namespace rpc::handlers::cache
{
swoc::Rv<YAML::Node>
clear_cache(std::string_view const & /* id ATS_UNUSED */, YAML::Node const & /* params ATS_UNUSED */)
{
  namespace err = rpc::handlers::errors;

  swoc::Rv<YAML::Node> resp;
  std::lock_guard      lock{generation_mutex};
  auto                 generation = RecGetRecordInt(CACHE_GENERATION_RECORD);

  if (!generation) {
    resp.errata()
      .assign(std::error_code{err::Codes::CONFIGURATION})
      .note("Could not read cache generation record '{}'.", CACHE_GENERATION_RECORD);
    return resp;
  }

  RecInt next_generation = 0;
  if (*generation >= 0 && *generation < std::numeric_limits<RecInt>::max()) {
    next_generation = *generation + 1;
  }

  if (RecSetRecordInt(CACHE_GENERATION_RECORD, next_generation, REC_SOURCE_DEFAULT) != REC_ERR_OKAY) {
    resp.errata()
      .assign(std::error_code{err::Codes::CONFIGURATION})
      .note("Could not advance cache generation record '{}'.", CACHE_GENERATION_RECORD);
  }

  return resp;
}
} // namespace rpc::handlers::cache
