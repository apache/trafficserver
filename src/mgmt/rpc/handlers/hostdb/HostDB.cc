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

#include "mgmt/rpc/handlers/hostdb/HostDB.h"
#include "mgmt/rpc/handlers/common/ErrorUtils.h"

#include "iocore/hostdb/HostDBProcessor.h"
#include "../src/iocore/hostdb/P_HostDBProcessor.h"
#include "swoc/MemSpan.h"
#include "tsutil/TsSharedMutex.h"
#include "yaml-cpp/node/node.h"
#include <shared_mutex>
#include <string>

namespace
{
DbgCtl dbg_ctl_rpc_server{"rpc.server"};
DbgCtl dbg_ctl_rpc_handler_server{"rpc.handler.hostdb"};

constexpr std::string_view
str(HostDBType type)
{
  // No default to find HostDBType change
  switch (type) {
  case HostDBType::ADDR:
    return "ADDR";
  case HostDBType::SRV:
    return "SRV";
  case HostDBType::HOST:
    return "HOST";
  case HostDBType::UNSPEC:
    return "UNSPEC";
  }
}

constexpr std::string_view
str(sa_family_t type)
{
  switch (type) {
  case AF_UNIX:
    return "AF_UNIX";
  case AF_INET:
    return "AF_INET";
  case AF_INET6:
    return "AF_INET6";
  case AF_UNSPEC:
    return "UNSPEC";
  default:
    return "UNKNOWN";
  }
}
} // end anonymous namespace

namespace YAML
{
template <> struct convert<HostDBCache> {
  static Node
  encode(const HostDBCache *const hostDB)
  {
    Node partitions;
    for (size_t i = 0; i < hostDB->refcountcache->partition_count(); i++) {
      auto                                 &partition = hostDB->refcountcache->get_partition(i);
      std::vector<RefCountCacheHashEntry *> partition_entries;

      {
        std::shared_lock<ts::shared_mutex> shared_lock{partition.lock};
        partition_entries.reserve(partition.count());
        partition.copy(partition_entries);
      }

      Node partition_node;
      partition_node["id"] = i;

      for (RefCountCacheHashEntry *entry : partition_entries) {
        HostDBRecord *record = static_cast<HostDBRecord *>(entry->item.get());
        partition_node["records"].push_back(*record);
      }

      partitions.push_back(partition_node);
    }

    auto &version = AppVersionInfo::get_version();

    Node node;
    node["metadata"]["timestamp"] =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    node["metadata"]["version"] = version.full_version();
    node["partitions"]          = partitions;

    return node;
  }
};

template <> struct convert<HostDBRecord> {
  static Node
  encode(const HostDBRecord &record)
  {
    Node metadata;
    metadata["name"]         = record.name();
    metadata["type"]         = str(record.record_type);
    metadata["af_familiy"]   = str(record.af_family);
    metadata["failed"]       = record.is_failed();
    metadata["ip_timestamp"] = record.ip_timestamp.time_since_epoch().count();

    Node node;
    node["metadata"] = metadata;

    swoc::MemSpan<const HostDBInfo> span = record.rr_info();
    for (const HostDBInfo &info : span) {
      YAML::Node info_node;
      if (record.is_srv()) {
        YAML::Node srv_node;
        srv_node["weight"]   = info.data.srv.srv_weight;
        srv_node["priority"] = info.data.srv.srv_priority;
        srv_node["port"]     = info.data.srv.srv_port;
        srv_node["target"]   = info.srvname();

        info_node["srv"] = srv_node;
      } else {
        char buf[INET6_ADDRSTRLEN];
        info.data.ip.toString(buf, sizeof(buf));

        info_node["ip"] = std::string(buf);
      }

      info_node["health"]["last_failure"] = info.last_failure.load().time_since_epoch().count();
      info_node["health"]["fail_count"]   = static_cast<int>(info.fail_count.load());

      node["info"].push_back(info_node);
    }

    return node;
  }
};
} // namespace YAML

namespace rpc::handlers::hostdb
{
namespace err = rpc::handlers::errors;

swoc::Rv<YAML::Node>
get_hostdb_status(std::string_view const & /* params ATS_UNUSED */, YAML::Node const & /* params ATS_UNUSED */)
{
  swoc::Rv<YAML::Node> resp;
  try {
    YAML::Node data = YAML::convert<HostDBCache>::encode(hostDBProcessor.cache());

    resp.result()["data"] = data;
  } catch (std::exception const &ex) {
    resp.errata()
      .assign(std::error_code{errors::Codes::SERVER})
      .note("Error found when calling get_hostdb_status API: {}", ex.what());
  }
  return resp;
}
} // namespace rpc::handlers::hostdb
