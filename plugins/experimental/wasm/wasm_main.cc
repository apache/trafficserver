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

#include "ts/ts.h"
#include <yaml-cpp/yaml.h>
#include "ats_wasm.h"
#include "include/proxy-wasm/wamr.h"

#include <getopt.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include <string>

// struct for storing plugin configuration
struct WasmInstanceConfig {
  std::string config_filename;
  std::string wasm_filename;
  std::shared_ptr<ats_wasm::Wasm> wasm           = nullptr;
  std::shared_ptr<proxy_wasm::PluginBase> plugin = nullptr;

  std::list<std::pair<std::shared_ptr<ats_wasm::Wasm>, std::shared_ptr<proxy_wasm::PluginBase>>> deleted_configs = {};
};

static std::unique_ptr<WasmInstanceConfig> wasm_config = nullptr;

// handler for timer event
static int
schedule_handler(TSCont contp, TSEvent /*event*/, void * /*data*/)
{
  TSDebug(WASM_DEBUG_TAG, "[%s] Inside schedule_handler", __FUNCTION__);

  auto *c = static_cast<ats_wasm::Context *>(TSContDataGet(contp));

  auto *old_wasm = static_cast<ats_wasm::Wasm *>(c->wasm());
  TSMutexLock(old_wasm->mutex());

  c->onTick(0); // use 0 as  token

  if (!wasm_config->wasm) {
    TSError("[wasm][%s] Configuration object is null", __FUNCTION__);
    TSMutexUnlock(old_wasm->mutex());
    return 0;
  }

  if (c->wasm() == wasm_config->wasm.get()) {
    auto *wasm               = static_cast<ats_wasm::Wasm *>(c->wasm());
    uint32_t root_context_id = c->id();
    if (wasm->existsTimerPeriod(root_context_id)) {
      TSDebug(WASM_DEBUG_TAG, "[%s] reschedule continuation", __FUNCTION__);
      std::chrono::milliseconds period = wasm->getTimerPeriod(root_context_id);
      TSContScheduleOnPool(contp, static_cast<TSHRTime>(period.count()), TS_THREAD_POOL_NET);
    } else {
      TSDebug(WASM_DEBUG_TAG, "[%s] can't find period for root context id: %d", __FUNCTION__, root_context_id);
    }
  } else {
    std::shared_ptr<ats_wasm::Wasm> temp = nullptr;
    uint32_t root_context_id             = c->id();
    old_wasm->removeTimerPeriod(root_context_id);

    if (old_wasm->readyShutdown()) {
      TSDebug(WASM_DEBUG_TAG, "[%s] starting WasmBase Shutdown", __FUNCTION__);
      old_wasm->startShutdown();
      if (!old_wasm->readyDelete()) {
        TSDebug(WASM_DEBUG_TAG, "[%s] not ready to delete WasmBase/PluginBase", __FUNCTION__);
      } else {
        TSDebug(WASM_DEBUG_TAG, "[%s] remove wasm from deleted_configs", __FUNCTION__);
        bool advance = true;
        for (auto it = wasm_config->deleted_configs.begin(); it != wasm_config->deleted_configs.end(); advance ? it++ : it) {
          advance = true;
          TSDebug(WASM_DEBUG_TAG, "[%s] looping through deleted_configs", __FUNCTION__);
          std::shared_ptr<ats_wasm::Wasm> wbp = it->first;
          temp                                = wbp;
          if (wbp.get() == old_wasm) {
            TSDebug(WASM_DEBUG_TAG, "[%s] found matching WasmBase", __FUNCTION__);
            it      = wasm_config->deleted_configs.erase(it);
            advance = false;
          }
        }
      }
    } else {
      TSDebug(WASM_DEBUG_TAG, "[%s] not ready to shutdown WasmBase", __FUNCTION__);
    }

    TSDebug(WASM_DEBUG_TAG, "[%s] config wasm has changed. thus not scheduling", __FUNCTION__);
  }

  TSMutexUnlock(old_wasm->mutex());

  return 0;
}

// handler for transaction event
static int
http_event_handler(TSCont contp, TSEvent event, void *data)
{
  int result     = -1;
  auto *context  = static_cast<ats_wasm::Context *>(TSContDataGet(contp));
  auto *old_wasm = static_cast<ats_wasm::Wasm *>(context->wasm());
  TSMutexLock(old_wasm->mutex());
  std::shared_ptr<ats_wasm::Wasm> temp = nullptr;
  auto *txnp                           = static_cast<TSHttpTxn>(data);

  TSMBuffer buf  = nullptr;
  TSMLoc hdr_loc = nullptr;
  int count      = 0;

  switch (event) {
  case TS_EVENT_HTTP_TXN_START:
    break;

  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    if (TSHttpTxnClientReqGet(txnp, &buf, &hdr_loc) != TS_SUCCESS) {
      TSError("[wasm][%s] cannot retrieve client request", __FUNCTION__);
      TSMutexUnlock(old_wasm->mutex());
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
      return 0;
    }
    count = TSMimeHdrFieldsCount(buf, hdr_loc);
    TSHandleMLocRelease(buf, TS_NULL_MLOC, hdr_loc);

    result = context->onRequestHeaders(count, false) == proxy_wasm::FilterHeadersStatus::Continue ? 0 : 1;
    break;

  case TS_EVENT_HTTP_POST_REMAP:
    break;

  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    break;

  case TS_EVENT_HTTP_SEND_REQUEST_HDR:
    break;

  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    if (TSHttpTxnServerRespGet(txnp, &buf, &hdr_loc) != TS_SUCCESS) {
      TSError("[wasm][%s] cannot retrieve server response", __FUNCTION__);
      TSMutexUnlock(old_wasm->mutex());
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
      return 0;
    }
    count = TSMimeHdrFieldsCount(buf, hdr_loc);
    TSHandleMLocRelease(buf, TS_NULL_MLOC, hdr_loc);

    result = context->onResponseHeaders(count, false) == proxy_wasm::FilterHeadersStatus::Continue ? 0 : 1;
    break;

  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    context->onLocalReply();
    result = 0;
    break;

  case TS_EVENT_HTTP_PRE_REMAP:
    break;

  case TS_EVENT_HTTP_OS_DNS:
    break;

  case TS_EVENT_HTTP_READ_CACHE_HDR:
    break;

  case TS_EVENT_HTTP_TXN_CLOSE:
    context->onDone();
    context->onDelete();

    if (context->wasm() == wasm_config->wasm.get()) {
      TSDebug(WASM_DEBUG_TAG, "[%s] config wasm has not changed", __FUNCTION__);
    } else {
      if (old_wasm->readyShutdown()) {
        TSDebug(WASM_DEBUG_TAG, "[%s] starting WasmBase Shutdown", __FUNCTION__);
        old_wasm->startShutdown();
        if (!old_wasm->readyDelete()) {
          TSDebug(WASM_DEBUG_TAG, "[%s] not ready to delete WasmBase/PluginBase", __FUNCTION__);
        } else {
          TSDebug(WASM_DEBUG_TAG, "[%s] remove wasm from deleted_configs", __FUNCTION__);
          bool advance = true;
          for (auto it = wasm_config->deleted_configs.begin(); it != wasm_config->deleted_configs.end(); advance ? it++ : it) {
            advance = true;
            TSDebug(WASM_DEBUG_TAG, "[%s] looping through deleted_configs", __FUNCTION__);
            std::shared_ptr<ats_wasm::Wasm> wbp = it->first;
            temp                                = wbp;
            if (wbp.get() == old_wasm) {
              TSDebug(WASM_DEBUG_TAG, "[%s] found matching WasmBase", __FUNCTION__);
              it      = wasm_config->deleted_configs.erase(it);
              advance = false;
            }
          }
        }
      } else {
        TSDebug(WASM_DEBUG_TAG, "[%s] not ready to shutdown WasmBase", __FUNCTION__);
      }

      TSDebug(WASM_DEBUG_TAG, "[%s] config wasm has changed", __FUNCTION__);
    }

    delete context;

    TSContDestroy(contp);
    result = 0;
    break;

  default:
    break;
  }

  TSMutexUnlock(old_wasm->mutex());

  if (result == 0) {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  } else if (result < 0) {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
  } else {
    // TODO: wait for async operation
    // Temporarily resume with error
    TSDebug(WASM_DEBUG_TAG, "[%s] result > 0, continue with error for now", __FUNCTION__);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
  }
  return 0;
}

// main handler/entry point for the plugin
static int
global_hook_handler(TSCont /*contp*/, TSEvent /*event*/, void *data)
{
  auto *wasm = wasm_config->wasm.get();
  TSMutexLock(wasm->mutex());
  auto *rootContext = wasm->getRootContext(wasm_config->plugin, false);
  auto *context     = new ats_wasm::Context(wasm, rootContext->id(), wasm_config->plugin);
  auto *txnp        = static_cast<TSHttpTxn>(data);
  context->initialize(txnp);
  context->onCreate();
  TSMutexUnlock(wasm->mutex());

  // create continuation for transaction
  TSCont txn_contp = TSContCreate(http_event_handler, nullptr);
  TSHttpTxnHookAdd(txnp, TS_HTTP_READ_REQUEST_HDR_HOOK, txn_contp);
  TSHttpTxnHookAdd(txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, txn_contp);
  TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, txn_contp);
  // add send response hook for local reply if needed
  TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, txn_contp);

  TSContDataSet(txn_contp, context);

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

// function to read a file
static inline int
read_file(const std::string &fn, std::string *s)
{
  auto fd = open(fn.c_str(), O_RDONLY);
  if (fd < 0) {
    return -1;
  }
  auto n = ::lseek(fd, 0, SEEK_END);
  ::lseek(fd, 0, SEEK_SET);
  s->resize(n);
  auto nn = ::read(fd, const_cast<char *>(&*s->begin()), n);
  if (nn != static_cast<ssize_t>(n)) {
    return -1;
  }
  return 0;
}

// function to read configuration
static bool
read_configuration()
{
  // PluginBase parameters
  std::string name          = "";
  std::string root_id       = "";
  std::string configuration = "";
  bool fail_open            = true;

  // WasmBase parameters
  std::string runtime          = "";
  std::string vm_id            = "";
  std::string vm_configuration = "";
  std::string wasm_filename    = "";
  bool allow_precompiled       = true;

  proxy_wasm::AllowedCapabilitiesMap cap_maps;
  std::unordered_map<std::string, std::string> envs;

  try {
    YAML::Node config = YAML::LoadFile(wasm_config->config_filename);

    for (YAML::const_iterator it = config.begin(); it != config.end(); ++it) {
      const std::string &node_name = it->first.as<std::string>();
      YAML::NodeType::value type   = it->second.Type();

      if (node_name != "config" || type != YAML::NodeType::Map) {
        TSError("[wasm][%s] Invalid YAML Configuration format for wasm: %s, reason: Top level nodes must be named config and be of "
                "type map",
                __FUNCTION__, wasm_config->config_filename.c_str());
        return false;
      }

      for (YAML::const_iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
        const YAML::Node first  = it2->first;
        const YAML::Node second = it2->second;

        const std::string &key = first.as<std::string>();
        if (second.IsScalar()) {
          const std::string &value = second.as<std::string>();
          if (key == "name") {
            name = value;
          }
          if (key == "root_id" || key == "rootId") {
            root_id = value;
          }
          if (key == "configuration") {
            configuration = value;
          }
          if (key == "fail_open") {
            if (value == "false") {
              fail_open = false;
            }
          }
        }
        if (second.IsMap() && (key == "capability_restriction_config")) {
          if (second["allowed_capabilities"]) {
            const YAML::Node ac_node = second["allowed_capabilities"];
            if (ac_node.IsSequence()) {
              for (const auto &i : ac_node) {
                auto ac = i.as<std::string>();
                proxy_wasm::SanitizationConfig sc;
                cap_maps[ac] = sc;
              }
            }
          }
        }

        if (second.IsMap() && (key == "vm_config" || key == "vmConfig")) {
          for (YAML::const_iterator it3 = second.begin(); it3 != second.end(); ++it3) {
            const YAML::Node vm_config_first  = it3->first;
            const YAML::Node vm_config_second = it3->second;

            const std::string &vm_config_key = vm_config_first.as<std::string>();
            if (vm_config_second.IsScalar()) {
              const std::string &vm_config_value = vm_config_second.as<std::string>();
              if (vm_config_key == "runtime") {
                runtime = vm_config_value;
              }
              if (vm_config_key == "vm_id" || vm_config_key == "vmId") {
                vm_id = vm_config_value;
              }
              if (vm_config_key == "configuration") {
                vm_configuration = vm_config_value;
              }
              if (vm_config_key == "allow_precompiled") {
                if (vm_config_value == "false") {
                  allow_precompiled = false;
                }
              }
            }

            if (vm_config_key == "environment_variables" && vm_config_second.IsMap()) {
              if (vm_config_second["host_env_keys"]) {
                const YAML::Node ek_node = vm_config_second["host_env_keys"];
                if (ek_node.IsSequence()) {
                  for (const auto &i : ek_node) {
                    auto ek = i.as<std::string>();
                    if (auto *value = std::getenv(ek.data())) {
                      envs[ek] = value;
                    }
                  }
                }
              }
              if (vm_config_second["key_values"]) {
                const YAML::Node kv_node = vm_config_second["key_values"];
                if (kv_node.IsMap()) {
                  for (YAML::const_iterator it4 = kv_node.begin(); it4 != kv_node.end(); ++it4) {
                    envs[it4->first.as<std::string>()] = it4->second.as<std::string>();
                  }
                }
              }
            }

            if (vm_config_key == "code" && vm_config_second.IsMap()) {
              if (vm_config_second["local"]) {
                const YAML::Node local_node = vm_config_second["local"];
                if (local_node["filename"]) {
                  wasm_filename = local_node["filename"].as<std::string>();
                }
              }
            }
          }
        }
      }

      // only allowed one config block (first one) for now
      break;
    }
  } catch (const YAML::Exception &e) {
    TSError("[wasm][%s] YAML::Exception %s when parsing YAML config file %s for wasm", __FUNCTION__, e.what(),
            wasm_config->config_filename.c_str());
    return false;
  }

  auto wasm                      = std::make_shared<ats_wasm::Wasm>(proxy_wasm::createWamrVm(), // VM
                                               vm_id,                      // vm_id
                                               vm_configuration,           // vm_configuration
                                               "",                         // vm_key,
                                               envs,                       // envs
                                               cap_maps                    // allowed capabilities
  );
  wasm->wasm_vm()->integration() = std::make_unique<ats_wasm::ATSWasmVmIntegration>();

  auto plugin = std::make_shared<proxy_wasm::PluginBase>(name,          // name
                                                         root_id,       // root_id
                                                         vm_id,         // vm_id
                                                         runtime,       // engine
                                                         configuration, // plugin_configuration
                                                         fail_open,     // failopen
                                                         ""             // TODO: plugin key from where ?
  );

  wasm_config->wasm_filename = wasm_filename;
  if (*wasm_config->wasm_filename.begin() != '/') {
    wasm_config->wasm_filename = std::string(TSConfigDirGet()) + "/" + wasm_config->wasm_filename;
  }
  std::string code;
  if (read_file(wasm_config->wasm_filename, &code) < 0) {
    TSError("[wasm][%s] wasm unable to read file '%s'", __FUNCTION__, wasm_config->wasm_filename.c_str());
    return false;
  }

  if (code.empty()) {
    TSError("[wasm][%s] code is empty", __FUNCTION__);
    return false;
  }

  if (!wasm) {
    TSError("[wasm][%s] wasm wasm wasm unable to create vm", __FUNCTION__);
    return false;
  }
  if (!wasm->load(code, allow_precompiled)) {
    TSError("[wasm][%s] Failed to load Wasm code", __FUNCTION__);
    return false;
  }
  if (!wasm->initialize()) {
    TSError("[wasm][%s] Failed to initialize Wasm code", __FUNCTION__);
    return false;
  }

  TSCont contp      = TSContCreate(schedule_handler, TSMutexCreate());
  auto *rootContext = wasm->start(plugin, contp);

  if (!wasm->configure(rootContext, plugin)) {
    TSError("[wasm][%s] Failed to configure Wasm", __FUNCTION__);
    return false;
  }

  auto old_wasm   = wasm_config->wasm;
  auto old_plugin = wasm_config->plugin;

  wasm_config->wasm   = wasm;
  wasm_config->plugin = plugin;

  if (old_wasm != nullptr) {
    TSDebug(WASM_DEBUG_TAG, "[%s] previous WasmBase exists", __FUNCTION__);
    TSMutexLock(old_wasm->mutex());
    if (old_wasm->readyShutdown()) {
      TSDebug(WASM_DEBUG_TAG, "[%s] starting WasmBase Shutdown", __FUNCTION__);
      old_wasm->startShutdown();
      if (!old_wasm->readyDelete()) {
        TSDebug(WASM_DEBUG_TAG, "[%s] not ready to delete WasmBase/PluginBase", __FUNCTION__);
        auto deleted_config = std::make_pair(old_wasm, old_plugin);
        wasm_config->deleted_configs.push_front(deleted_config);
      }
    } else {
      TSDebug(WASM_DEBUG_TAG, "[%s] not ready to shutdown WasmBase", __FUNCTION__);
      auto deleted_config = std::make_pair(old_wasm, old_plugin);
      wasm_config->deleted_configs.push_front(deleted_config);
    }
    TSMutexUnlock(old_wasm->mutex());
  }

  return true;
}

// handler for configuration event
static int
config_handler(TSCont /*contp*/, TSEvent /*event*/, void * /*data*/)
{
  TSDebug(WASM_DEBUG_TAG, "[%s] configuration reloading", __FUNCTION__);
  read_configuration();
  TSDebug(WASM_DEBUG_TAG, "[%s] configuration reloading ends", __FUNCTION__);
  return 0;
}

// main function for the plugin
void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = "wasm";
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";
  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[wasm] Plugin registration failed");
  }

  if (argc < 2) {
    TSError("[wasm][%s] wasm config argument missing", __FUNCTION__);
    return;
  }

  wasm_config = std::make_unique<WasmInstanceConfig>();

  std::string filename = std::string(argv[1]);
  if (*filename.begin() != '/') {
    filename = std::string(TSConfigDirGet()) + "/" + filename;
  }
  wasm_config->config_filename = filename;

  if (!read_configuration()) {
    return;
  }

  // global handler
  TSCont global_contp = TSContCreate(global_hook_handler, nullptr);
  if (global_contp == nullptr) {
    TSError("[wasm][%s] could not create transaction start continuation", __FUNCTION__);
    return;
  }
  TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, global_contp);

  // configuration handler
  TSCont config_contp = TSContCreate(config_handler, nullptr);
  if (config_contp == nullptr) {
    TSError("[ts_lua][%s] could not create configuration continuation", __FUNCTION__);
    return;
  }
  TSMgmtUpdateRegister(config_contp, "wasm");
}
