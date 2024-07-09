/** @file

  Test JSONRPC method and notification handling inside a plugin.

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
#include <ts/ts.h>
#include <string_view>
#include <thread>
#include <algorithm>
#include <fstream>

#include "swoc/swoc_file.h"
#include "tsutil/ts_bw_format.h"

#include "yaml-cpp/yaml.h"
#include "tscore/Layout.h"
#include "tsutil/ts_bw_format.h"
#include "mgmt/rpc/jsonrpc/JsonRPC.h"

namespace
{
constexpr char PLUGIN_NAME[] = "jsonrpc_plugin_handler_test";

DbgCtl dbg_ctl{PLUGIN_NAME};

const std::string MY_YAML_VERSION{"0.8.0"};
const std::string RPC_PROVIDER_NAME{"RPC Plugin test"};
} // namespace

namespace
{
void
test_join_hosts_method(const char *id, TSYaml p)
{
  Dbg(dbg_ctl, "Got a call! id: %s", id);
  YAML::Node params = *reinterpret_cast<YAML::Node *>(p);

  // This handler errors.
  enum HandlerErrors { NO_HOST_ERROR = 10001, EMPTY_HOSTS_ERROR, UNKNOWN_ERROR };
  try {
    std::vector<std::string> hosts;
    if (auto node = params["hosts"]) {
      hosts = node.as<std::vector<std::string>>();
    } else {
      // We can't continue. Notify the RPC manager.
      std::string descr{"No host provided"};
      TSRPCHandlerError(NO_HOST_ERROR, descr.c_str(), descr.size());
      return;
    }

    if (0 == hosts.size()) {
      // We can't continue. Notify the RPC manager.
      std::string descr{"At least one host should be provided"};
      TSRPCHandlerError(EMPTY_HOSTS_ERROR, descr.c_str(), descr.size());
      return;
    }

    std::string join;
    std::for_each(std::begin(hosts), std::end(hosts), [&join](auto &&s) { join += s; });
    YAML::Node resp;
    resp["join"] = join;
    // All done. Notify the RPC manager.
    TSRPCHandlerDone(reinterpret_cast<TSYaml>(&resp));
  } catch (YAML::Exception const &ex) {
    Dbg(dbg_ctl, "Oops, something went wrong: %s", ex.what());
    std::string descr{ex.what()};
    TSRPCHandlerError(UNKNOWN_ERROR, descr.c_str(), descr.size());
  }
}

// It's a notificaion, we do not care to respond back to the JSONRPC manager.
void
test_join_hosts_notification(TSYaml p)
{
  Dbg(dbg_ctl, "Got a call!");
  try {
    YAML::Node params = *reinterpret_cast<YAML::Node *>(p);

    std::vector<std::string> hosts;
    if (auto hosts = params["hosts"]) {
      hosts = hosts.as<std::vector<std::string>>();
    }
    if (0 == hosts.size()) {
      Dbg(dbg_ctl, "No hosts field provided. Nothing we can do. No response back.");
      return;
    }
    std::string join;
    std::for_each(std ::begin(hosts), std::end(hosts), [&join](auto &&s) { join += s; });
    Dbg(dbg_ctl, "Notification properly handled: %s", join.c_str());
  } catch (YAML::Exception const &ex) {
    Dbg(dbg_ctl, "Oops, something went wrong: %s", ex.what());
  }
}
} // namespace
namespace
{
// Incoming host info structure.
struct HostItem {
  std::string name;
  std::string status;
};

int
CB_handle_rpc_io_call(TSCont contp, TSEvent /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
{
  namespace fs = swoc::file;

  Dbg(dbg_ctl, "Working on the update now");
  YAML::Node params = *static_cast<YAML::Node *>(TSContDataGet(contp));

  // This handler errors.
  enum HandlerErrors { INVALID_PARAM_TYPE = 10010, INVALID_HOST_PARAM_TYPE, FILE_UPDATE_ERROR, UNKNOWN_ERROR };

  // we only care for a map type {}
  if (params.Type() != YAML::NodeType::Map) {
    std::string descr{"Handler is expecting a map."};
    TSRPCHandlerError(INVALID_PARAM_TYPE, descr.c_str(), descr.size());
    return TS_SUCCESS;
  }

  // we want to keep track of the work done! This will become part of the response
  // and it will be validated on the client side. The whole test validation is base
  // on this.
  int updatedHosts{0};
  int addedHosts{0};

  std::vector<HostItem> incHosts;
  if (auto &&passedHosts = params["hosts"]; passedHosts.Type() == YAML::NodeType::Sequence) {
    // fill in
    for (auto &&h : passedHosts) {
      std::string name, status;
      if (auto &&n = h["name"]) {
        name = n.as<std::string>();
      }

      if (auto &&s = h["status"]) {
        status = s.as<std::string>();
      }
      incHosts.push_back({name, status});
    }
  } else {
    std::string descr{"not a sequence, we expect a list of hosts"};
    TSRPCHandlerError(INVALID_HOST_PARAM_TYPE, descr.c_str(), descr.size());
    return TS_SUCCESS;
  }

  // Basic stuffs here.
  // We open the file if exist, we update/add the host in the structure. For simplicity we do not delete anything.
  fs::path sandbox  = fs::current_path() / "runtime";
  fs::path dumpFile = sandbox / "my_test_plugin_dump.yaml";
  bool     newFile{false};
  if (!fs::exists(dumpFile)) {
    newFile = true;
  }

  // handle function to add new hosts to a node.
  auto add_host = [](std::string const &name, std::string const &status, YAML::Node &out) -> void {
    YAML::Node newHost;
    newHost["name"]   = name;
    newHost["status"] = status;
    out.push_back(newHost);
  };

  YAML::Node dump;
  if (!newFile) {
    try {
      dump = YAML::LoadFile(dumpFile.c_str());
      if (dump.IsSequence()) {
        std::vector<HostItem> tobeAdded;
        for (auto &&incHost : incHosts) {
          auto search = std::find_if(std::begin(dump), std::end(dump), [&incHost](YAML::Node const &node) {
            if (auto &&n = node["name"]) {
              return incHost.name == n.as<std::string>();
            } else {
              throw std::runtime_error("We couldn't find 'name' field.");
            }
          });

          if (search != std::end(dump)) {
            (*search)["status"] = incHost.status;
            ++updatedHosts;
          } else {
            add_host(incHost.name, incHost.status, dump);
            ++addedHosts;
          }
        }
      }
    } catch (YAML::Exception const &e) {
      std::string buff;
      swoc::bwprint(buff, "Error during file handling: {}", e.what());
      TSRPCHandlerError(UNKNOWN_ERROR, buff.c_str(), buff.size());
      return TS_SUCCESS;
    }
  } else {
    for (auto &&incHost : incHosts) {
      add_host(incHost.name, incHost.status, dump);
      ++addedHosts;
    }
  }

  // Dump it..
  YAML::Emitter out;
  out << dump;

  fs::path      tmpFile = sandbox / "tmpfile.yaml";
  std::ofstream ofs(tmpFile.c_str());
  ofs << out.c_str();
  ofs.close();

  std::error_code ec;
  if (fs::copy(tmpFile, dumpFile, ec); ec) {
    std::string buff;
    swoc::bwprint(buff, "Error during file handling: {}, {}", ec.value(), ec.message());
    TSRPCHandlerError(FILE_UPDATE_ERROR, buff.c_str(), buff.size());
    return TS_SUCCESS;
  }

  // clean up the temp file if possible.
  if (fs::remove(tmpFile, ec); ec) {
    Dbg(dbg_ctl, "Temp file could not be removed: %s", tmpFile.c_str());
  }

  // make the response. For complex structures YAML::convert<T>::encode() would be the preferred way.
  YAML::Node resp;
  resp["updatedHosts"] = updatedHosts;
  resp["addedHosts"]   = addedHosts;
  resp["dumpFile"]     = dumpFile.c_str(); // In case you need this

  // we are done!!
  TSContDestroy(contp);
  TSRPCHandlerDone(reinterpret_cast<TSYaml>(&resp));
  return TS_SUCCESS;
}

// Perform a field updated on a yaml file. Host will be added or updated
void
test_io_on_et_task(const char * /* id ATS_UNUSED */, TSYaml p)
{
  // A possible scenario would be that a handler needs to perform a "heavy" operation or that the handler
  // is not yet ready to perform the operation when is called, under this scenarios(and some others)
  // we can use the RPC API to easily achieve this and respond just when we are ready.
  TSCont c = TSContCreate(CB_handle_rpc_io_call, TSMutexCreate());
  TSContDataSet(c, p);
  TSContScheduleOnPool(c, 1000 /* no particular reason */, TS_THREAD_POOL_TASK);
}
} // namespace

void
TSPluginInit(int /* argc ATS_UNUSED */, const char ** /* argv ATS_UNUSED */)
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
  }

  // Check-in to make sure we are compliant with the YAML version in TS.
  TSRPCProviderHandle rpcRegistrationInfo =
    TSRPCRegister(RPC_PROVIDER_NAME.c_str(), RPC_PROVIDER_NAME.size(), MY_YAML_VERSION.c_str(), MY_YAML_VERSION.size());

  if (rpcRegistrationInfo == nullptr) {
    TSError("[%s] RPC handler registration failed, yaml version not supported.", PLUGIN_NAME);
  }

  TSRPCHandlerOptions opt{{true}};
  std::string         method_name{"test_join_hosts_method"};
  if (TSRPCRegisterMethodHandler(method_name.c_str(), method_name.size(), test_join_hosts_method, rpcRegistrationInfo, &opt) ==
      TS_ERROR) {
    Dbg(dbg_ctl, "%s failed to register", method_name.c_str());
  } else {
    Dbg(dbg_ctl, "%s successfully registered", method_name.c_str());
  }

  // TASK thread.
  method_name = "test_io_on_et_task";
  if (TSRPCRegisterMethodHandler(method_name.c_str(), method_name.size(), test_io_on_et_task, rpcRegistrationInfo, &opt) ==
      TS_ERROR) {
    Dbg(dbg_ctl, "%s failed to register", method_name.c_str());
  } else {
    Dbg(dbg_ctl, "%s successfully registered", method_name.c_str());
  }

  // Notification
  TSRPCHandlerOptions nOpt{{false}};
  method_name = "test_join_hosts_notification";
  if (TSRPCRegisterNotificationHandler(method_name.c_str(), method_name.size(), test_join_hosts_notification, rpcRegistrationInfo,
                                       &nOpt) == TS_ERROR) {
    Dbg(dbg_ctl, "%s failed to register", method_name.c_str());
  } else {
    Dbg(dbg_ctl, "%s successfully registered", method_name.c_str());
  }

  Dbg(dbg_ctl, "Test Plugin Initialized.");
}
