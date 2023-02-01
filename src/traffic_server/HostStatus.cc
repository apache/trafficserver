/** @file

  Implementation of Host Proxy routing

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
#include <fstream>
#include "HostStatus.h"
#include "I_Tasks.h"

#include "tscore/BufferWriter.h"
#include "rpc/jsonrpc/JsonRPC.h"
#include "shared/rpc/RPCRequests.h"

namespace
{
const std::string STATUS_LIST_KEY{"statusList"};
const std::string ERROR_LIST_KEY{"errorList"};
const std::string HOST_NAME_KEY{"hostname"};
const std::string STATUS_KEY{"status"};

struct HostCmdInfo {
  TSHostStatus type{TSHostStatus::TS_HOST_STATUS_INIT};
  unsigned int reasonType{0};
  std::vector<std::string> hosts;
  int time{0};
};

} // namespace

ts::Rv<YAML::Node> server_get_status(std::string_view const id, YAML::Node const &params);
ts::Rv<YAML::Node> server_set_status(std::string_view const id, YAML::Node const &params);

HostStatRec::HostStatRec()
  : status(TS_HOST_STATUS_UP),
    reasons(0),
    active_marked_down(0),
    local_marked_down(0),
    manual_marked_down(0),
    self_detect_marked_down(0),
    active_down_time(0),
    local_down_time(0),
    manual_down_time(0){};

HostStatRec::HostStatRec(std::string str)
{
  std::vector<std::string> v1;
  std::stringstream ss1(str);

  reasons = 0;

  // parse the csv strings from the stat record value string.
  while (ss1.good()) {
    char b1[64];
    ss1.getline(b1, 64, ',');
    v1.push_back(b1);
  }

  // v1 contains 5 strings.
  ink_assert(v1.size() == 5);

  // set the status and reasons fields.
  for (unsigned int i = 0; i < v1.size(); i++) {
    if (i == 0) { // set the status field
      if (v1.at(i).compare("HOST_STATUS_UP") == 0) {
        status = TS_HOST_STATUS_UP;
      } else if (v1.at(i).compare("HOST_STATUS_DOWN") == 0) {
        status = TS_HOST_STATUS_DOWN;
      }
    } else { // parse and set remaining reason fields.
      std::vector<std::string> v2;
      v2.clear();
      std::stringstream ss2(v1.at(i));
      while (ss2.good()) {
        char b2[64];
        ss2.getline(b2, 64, ':');
        v2.push_back(b2);
      }
      // v2 contains 4 strings.
      ink_assert(v2.size() == 3 || v2.size() == 4);

      if (v2.at(0).compare("ACTIVE") == 0) {
        if (v2.at(1).compare("DOWN") == 0) {
          reasons |= Reason::ACTIVE;
        } else if (reasons & Reason::ACTIVE) {
          reasons ^= Reason::ACTIVE;
        }
        active_marked_down = atoi(v2.at(2).c_str());
        active_down_time   = atoi(v2.at(3).c_str());
      }
      if (v2.at(0).compare("LOCAL") == 0) {
        if (v2.at(1).compare("DOWN") == 0) {
          reasons |= Reason::LOCAL;
        } else if (reasons & Reason::LOCAL) {
          reasons ^= Reason::LOCAL;
        }
        local_marked_down = atoi(v2.at(2).c_str());
        local_down_time   = atoi(v2.at(3).c_str());
      }
      if (v2.at(0).compare("MANUAL") == 0) {
        if (v2.at(1).compare("DOWN") == 0) {
          reasons |= Reason::MANUAL;
        } else if (reasons & Reason::MANUAL) {
          reasons ^= Reason::MANUAL;
        }
        manual_marked_down = atoi(v2.at(2).c_str());
        manual_down_time   = atoi(v2.at(3).c_str());
      }
      if (v2.at(0).compare("SELF_DETECT") == 0) {
        if (v2.at(1).compare("DOWN") == 0) {
          reasons |= Reason::SELF_DETECT;
        } else if (reasons & Reason::SELF_DETECT) {
          reasons ^= Reason::SELF_DETECT;
        }
        self_detect_marked_down = atoi(v2.at(2).c_str());
      }
    }
  }
}

HostStatus::HostStatus()
{
  ink_rwlock_init(&host_status_rwlock);

  // register JSON-RPC methods.
  rpc::add_method_handler("admin_host_set_status", &server_set_status, &rpc::core_ats_rpc_service_provider_handle,
                          {{rpc::RESTRICTED_API}});
  rpc::add_method_handler("admin_host_get_status", &server_get_status, &rpc::core_ats_rpc_service_provider_handle,
                          {{rpc::NON_RESTRICTED_API}});
}

HostStatus::~HostStatus()
{
  for (auto &&it : hosts_statuses) {
    ats_free(it.second);
  }
  // release the read and writer locks.
  ink_rwlock_destroy(&host_status_rwlock);
}

// loads host status persistent store file
void
HostStatus::loadFromPersistentStore()
{
  YAML::Node records;
  std::string fileStore = getHostStatusPersistentFilePath();
  if (access(fileStore.c_str(), R_OK) == 0) {
    try {
      records               = YAML::LoadFile(fileStore.c_str());
      YAML::Node statusList = records["statuses"];
      for (YAML::const_iterator it = statusList.begin(); it != statusList.end(); ++it) {
        const YAML::Node &host = *it;
        std::string hostName   = host[HOST_NAME_KEY].as<std::string>();
        std::string status     = host[STATUS_KEY].as<std::string>();
        HostStatRec h(status);
        loadRecord(hostName, h);
      }
    } catch (std::exception const &ex) {
      Warning("Error loading and decoding %s : %s", fileStore.c_str(), ex.what());
    }
  }
}

// loads in host status record.
void
HostStatus::loadRecord(std::string_view name, HostStatRec &h)
{
  HostStatRec *host_stat = nullptr;
  Debug("host_statuses", "loading host status record for %.*s", int(name.size()), name.data());
  ink_rwlock_wrlock(&host_status_rwlock);
  {
    auto it = hosts_statuses.find(std::string(name));
    if (it == hosts_statuses.end()) {
      host_stat  = static_cast<HostStatRec *>(ats_malloc(sizeof(HostStatRec)));
      *host_stat = h;
      hosts_statuses.emplace(name, host_stat);
    }
  }
  ink_rwlock_unlock(&host_status_rwlock);
}

void
HostStatus::setHostStatus(const std::string_view name, TSHostStatus status, const unsigned int down_time, const unsigned int reason)
{
  std::string stat_name;

  // update / insert status.
  // using the hash table pointer to store the TSHostStatus value.
  HostStatRec *host_stat = nullptr;
  ink_rwlock_wrlock(&host_status_rwlock);
  {
    if (auto it = hosts_statuses.find(std::string(name)); it != hosts_statuses.end()) {
      host_stat = it->second;
    } else {
      host_stat = static_cast<HostStatRec *>(ats_malloc(sizeof(HostStatRec)));
      bzero(host_stat, sizeof(HostStatRec));
      hosts_statuses.emplace(name, host_stat);
    }
    if (reason & Reason::ACTIVE) {
      Debug("host_statuses", "for host %.*s set status: %s, Reason:ACTIVE", int(name.size()), name.data(), HostStatusNames[status]);
      if (status == TSHostStatus::TS_HOST_STATUS_DOWN) {
        host_stat->active_marked_down = time(0);
        host_stat->active_down_time   = down_time;
        host_stat->reasons            |= Reason::ACTIVE;
      } else {
        host_stat->active_marked_down = 0;
        host_stat->active_down_time   = 0;
        if (host_stat->reasons & Reason::ACTIVE) {
          host_stat->reasons ^= Reason::ACTIVE;
        }
      }
    }
    if (reason & Reason::LOCAL) {
      Debug("host_statuses", "for host %.*s set status: %s, Reason:LOCAL", int(name.size()), name.data(), HostStatusNames[status]);
      if (status == TSHostStatus::TS_HOST_STATUS_DOWN) {
        host_stat->local_marked_down = time(0);
        host_stat->local_down_time   = down_time;
        host_stat->reasons           |= Reason::LOCAL;
      } else {
        host_stat->local_marked_down = 0;
        host_stat->local_down_time   = 0;
        if (host_stat->reasons & Reason::LOCAL) {
          host_stat->reasons ^= Reason::LOCAL;
        }
      }
    }
    if (reason & Reason::MANUAL) {
      Debug("host_statuses", "for host %.*s set status: %s, Reason:MANUAL", int(name.size()), name.data(), HostStatusNames[status]);
      if (status == TSHostStatus::TS_HOST_STATUS_DOWN) {
        host_stat->manual_marked_down = time(0);
        host_stat->manual_down_time   = down_time;
        host_stat->reasons            |= Reason::MANUAL;
      } else {
        host_stat->manual_marked_down = 0;
        host_stat->manual_down_time   = 0;
        if (host_stat->reasons & Reason::MANUAL) {
          host_stat->reasons ^= Reason::MANUAL;
        }
      }
    }
    if (reason & Reason::SELF_DETECT) {
      Debug("host_statuses", "for host %.*s set status: %s, Reason:SELF_DETECT", int(name.size()), name.data(),
            HostStatusNames[status]);
      if (status == TSHostStatus::TS_HOST_STATUS_DOWN) {
        host_stat->self_detect_marked_down = time(0);
        host_stat->reasons                 |= Reason::SELF_DETECT;
      } else {
        host_stat->self_detect_marked_down = 0;
        if (host_stat->reasons & Reason::SELF_DETECT) {
          host_stat->reasons ^= Reason::SELF_DETECT;
        }
      }
    }
    if (status == TSHostStatus::TS_HOST_STATUS_UP) {
      if (host_stat->reasons == 0) {
        host_stat->status = TSHostStatus::TS_HOST_STATUS_UP;
      }
      Debug("host_statuses", "reasons: %d, status: %s", host_stat->reasons, HostStatusNames[host_stat->status]);
    } else {
      host_stat->status = status;
      Debug("host_statuses", "reasons: %d, status: %s", host_stat->reasons, HostStatusNames[host_stat->status]);
    }
  }
  ink_rwlock_unlock(&host_status_rwlock);

  // log it.
  if (status == TSHostStatus::TS_HOST_STATUS_DOWN) {
    Note("Host %.*s has been marked down, down_time: %d - %s.", int(name.size()), name.data(), down_time,
         down_time == 0 ? "indefinitely." : "seconds.");
  } else {
    Note("Host %.*s has been marked up.", int(name.size()), name.data());
  }
}

// retrieve all host statuses.
void
HostStatus::getAllHostStatuses(std::vector<HostStatuses> &hosts)
{
  if (hosts_statuses.empty()) {
    return;
  }

  ink_rwlock_rdlock(&host_status_rwlock);
  {
    for (std::pair<std::string, HostStatRec *> hsts : hosts_statuses) {
      std::stringstream ss;
      HostStatuses h;
      h.hostname = hsts.first;
      ss << *hsts.second;
      h.status = ss.str();
      hosts.push_back(h);
    }
  }
  ink_rwlock_unlock(&host_status_rwlock);
}

// retrieve the named host status.
HostStatRec *
HostStatus::getHostStatus(const std::string_view name)
{
  HostStatRec *_status = nullptr;
  time_t now           = time(0);
  bool lookup          = false;

  // if host_statuses is empty, just return
  // a nullptr as there is no need to lock
  // and search.  A return of nullptr indicates
  // to the caller that the host is available,
  // HOST_STATUS_UP.
  if (hosts_statuses.empty()) {
    return _status;
  }

  // the hash table value pointer has the TSHostStatus value.
  ink_rwlock_rdlock(&host_status_rwlock);
  {
    auto it = hosts_statuses.find(std::string(name));
    lookup  = it != hosts_statuses.end();
    if (lookup) {
      _status = it->second;
    }
  }
  ink_rwlock_unlock(&host_status_rwlock);

  // if the host was marked down and it's down_time has elapsed, mark it up.
  if (lookup && _status->status == TSHostStatus::TS_HOST_STATUS_DOWN) {
    unsigned int reasons = _status->reasons;
    if ((_status->reasons & Reason::ACTIVE) && _status->active_down_time > 0) {
      if ((_status->active_down_time + _status->active_marked_down) < now) {
        Debug("host_statuses", "name: %.*s, now: %ld, down_time: %d, marked_down: %ld, reason: %s", int(name.size()), name.data(),
              now, _status->active_down_time, _status->active_marked_down, Reason::ACTIVE_REASON);
        setHostStatus(name, TSHostStatus::TS_HOST_STATUS_UP, 0, Reason::ACTIVE);
        reasons ^= Reason::ACTIVE;
      }
    }
    if ((_status->reasons & Reason::LOCAL) && _status->local_down_time > 0) {
      if ((_status->local_down_time + _status->local_marked_down) < now) {
        Debug("host_statuses", "name: %.*s, now: %ld, down_time: %d, marked_down: %ld, reason: %s", int(name.size()), name.data(),
              now, _status->local_down_time, _status->local_marked_down, Reason::LOCAL_REASON);
        setHostStatus(name, TSHostStatus::TS_HOST_STATUS_UP, 0, Reason::LOCAL);
        reasons ^= Reason::LOCAL;
      }
    }
    if ((_status->reasons & Reason::MANUAL) && _status->manual_down_time > 0) {
      if ((_status->manual_down_time + _status->manual_marked_down) < now) {
        Debug("host_statuses", "name: %.*s, now: %ld, down_time: %d, marked_down: %ld, reason: %s", int(name.size()), name.data(),
              now, _status->manual_down_time, _status->manual_marked_down, Reason::MANUAL_REASON);
        setHostStatus(name, TSHostStatus::TS_HOST_STATUS_UP, 0, Reason::MANUAL);
        reasons ^= Reason::MANUAL;
      }
    }
    _status->reasons = reasons;
  }

  return _status;
}

namespace YAML
{
template <> struct convert<HostCmdInfo> {
  static bool
  decode(const Node &node, HostCmdInfo &rhs)
  {
    if (auto n = node["operation"]) {
      auto const &str = n.as<std::string>();
      if (str == "up") {
        rhs.type = TSHostStatus::TS_HOST_STATUS_UP;
      } else if (str == "down") {
        rhs.type = TSHostStatus::TS_HOST_STATUS_DOWN;
      } else {
        // unknown.
        return false;
      }
    } else {
      return false;
    }

    if (auto n = node["host"]; n.IsSequence() && n.size()) {
      for (auto &&it : n) {
        rhs.hosts.push_back(it.as<std::string>());
      }
    } else {
      return false;
    }

    if (auto n = node["reason"]) {
      auto reasonStr = n.as<std::string>();
      rhs.reasonType = Reason::getReason(reasonStr.c_str());
    } // manual by default.

    if (auto n = node["time"]) {
      rhs.time = std::stoi(n.as<std::string>());
      if (rhs.time < 0) {
        return false;
      }
    } else {
      return false;
    }

    return true;
  }
};
} // namespace YAML

// JSON-RPC method to retrieve host status information.
ts::Rv<YAML::Node>
server_get_status(std::string_view id, YAML::Node const &params)
{
  namespace err = rpc::handlers::errors;
  ts::Rv<YAML::Node> resp;
  YAML::Node statusList{YAML::NodeType::Sequence}, errorList{YAML::NodeType::Sequence};

  try {
    if (!params.IsNull() && params.size() > 0) { // returns host statuses for just the ones asked for.
      for (YAML::const_iterator it = params.begin(); it != params.end(); ++it) {
        YAML::Node host{YAML::NodeType::Map};
        auto name             = it->as<std::string>();
        HostStatRec *host_rec = nullptr;
        HostStatus &hs        = HostStatus::instance();
        host_rec              = hs.getHostStatus(name);
        if (host_rec == nullptr) {
          Debug("host_statuses", "no record for %s was found", name.c_str());
          errorList.push_back("no record for " + name + " was found");
          continue;
        } else {
          std::stringstream s;
          s << *host_rec;
          host[HOST_NAME_KEY] = name;
          host[STATUS_KEY]    = s.str();
          statusList.push_back(host);
          Debug("host_statuses", "hostname: %s, status: %s", name.c_str(), s.str().c_str());
        }
      }
    } else { // return all host statuses.
      std::vector<HostStatuses> hostInfo;
      HostStatus &hs = HostStatus::instance();
      hs.getAllHostStatuses(hostInfo);
      for (auto &h : hostInfo) {
        YAML::Node host{YAML::NodeType::Map};
        host[HOST_NAME_KEY] = h.hostname;
        host[STATUS_KEY]    = h.status;
        statusList.push_back(host);
      }
    }
  } catch (std::exception const &ex) {
    Debug("host_statuses", "Got an error decoding the parameters: %s", ex.what());
    errorList.push_back("Error decoding parameters : " + std::string(ex.what()));
  }

  resp.result()[STATUS_LIST_KEY] = statusList;
  resp.result()[ERROR_LIST_KEY]  = errorList;

  return resp;
}

// JSON-RPC method to mark up or down a host.
ts::Rv<YAML::Node>
server_set_status(std::string_view id, YAML::Node const &params)
{
  Debug("host_statuses", "id=%s", id.data());
  namespace err = rpc::handlers::errors;
  ts::Rv<YAML::Node> resp;

  try {
    if (!params.IsNull()) {
      auto cmdInfo = params.as<HostCmdInfo>();

      for (auto const &name : cmdInfo.hosts) {
        HostStatus &hs       = HostStatus::instance();
        std::string statName = stat_prefix + name;
        Debug("host_statuses", "marking server %s : %s", name.c_str(),
              (cmdInfo.type == TSHostStatus::TS_HOST_STATUS_UP ? "up" : "down"));
        hs.setHostStatus(name.c_str(), cmdInfo.type, cmdInfo.time, cmdInfo.reasonType);
      }
    } else {
      resp.errata().push(err::make_errata(err::Codes::SERVER, "Invalid input parameters, null"));
    }

    // schedule a write to the persistent store.
    Debug("host_statuses", "updating persistent store");
    eventProcessor.schedule_imm(new HostStatusSync, ET_TASK);
  } catch (std::exception const &ex) {
    Debug("host_statuses", "Got an error HostCmdInfo decoding: %s", ex.what());
    resp.errata().push(err::make_errata(err::Codes::SERVER, "Error found during host status set: {}", ex.what()));
  }
  return resp;
}

// method to write host status records to the persistent store.
void
HostStatusSync::sync_task()
{
  YAML::Node records{YAML::NodeType::Map};

  YAML::Node statusList{YAML::NodeType::Sequence};
  std::vector<HostStatuses> statuses;
  HostStatus &hs = HostStatus::instance();
  hs.getAllHostStatuses(statuses);

  for (auto &&h : statuses) {
    YAML::Node host{YAML::NodeType::Map};
    host[HOST_NAME_KEY] = h.hostname;
    host[STATUS_KEY]    = h.status;
    statusList.push_back(host);
  }
  records["statuses"] = statusList;

  std::ofstream fout;
  fout.open(hostRecordsFile.c_str(), std::ofstream::out | std::ofstream::trunc);
  if (fout) {
    fout << records;
    fout << '\n';
    fout.close();
  } else {
    Warning("failed to open %s for writing", hostRecordsFile.c_str());
  }
}
