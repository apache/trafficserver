/**
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

#include "Storage.h"
#include "tscore/BufferWriter.h"
#include "rpc/handlers/common/ErrorUtils.h"
#include "P_Cache.h"

namespace rpc::handlers::storage::field_names
{
static constexpr auto PATH{"path"};
static constexpr auto STATUS{"status"};
static constexpr auto ERRORS{"error_count"};
} // namespace rpc::handlers::storage::field_names

namespace YAML
{
template <> struct convert<CacheDisk> {
  static Node
  encode(CacheDisk const &cdisk)
  {
    namespace field = rpc::handlers::storage::field_names;
    Node node;
    try {
      node[field::PATH]   = cdisk.path;
      node[field::STATUS] = (cdisk.online ? "online" : "offline");
      node[field::ERRORS] = cdisk.num_errors;
    } catch (std::exception const &e) {
      return node;
    }

    Node cacheDiskNode;
    cacheDiskNode["cachedisk"] = node;
    return cacheDiskNode;
  }
};

} // namespace YAML

namespace rpc::handlers::storage
{
namespace err = rpc::handlers::errors;

ts::Rv<YAML::Node>
set_storage_offline(std::string_view const &id, YAML::Node const &params)
{
  ts::Rv<YAML::Node> resp;

  for (auto &&it : params) {
    std::string device = it.as<std::string>();
    CacheDisk *d       = cacheProcessor.find_by_path(device.c_str(), (device.size()));

    if (d) {
      Debug("rpc.server", "Marking %s offline", device.c_str());

      YAML::Node n;
      auto ret                     = cacheProcessor.mark_storage_offline(d, /* admin */ true);
      n["path"]                    = device;
      n["has_online_storage_left"] = ret ? "true" : "false";
      resp.result().push_back(std::move(n));
    } else {
      resp.errata().push(err::make_errata(err::Codes::STORAGE, "Passed device:'{}' does not match any defined storage", device));
    }
  }
  return resp;
}

ts::Rv<YAML::Node>
get_storage_status(std::string_view const &id, YAML::Node const &params)
{
  ts::Rv<YAML::Node> resp;

  for (auto &&it : params) {
    std::string device = it.as<std::string>();
    CacheDisk *d       = cacheProcessor.find_by_path(device.c_str(), static_cast<int>(device.size()));

    if (d) {
      resp.result().push_back(*d);
    } else {
      resp.errata().push(err::make_errata(err::Codes::STORAGE, "Passed device:'{}' does not match any defined storage", device));
    }
  }
  return resp;
}
} // namespace rpc::handlers::storage
