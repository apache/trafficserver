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

#ifdef WAMR
#include "include/proxy-wasm/wamr.h"
#endif

#ifdef WASMEDGE
#include "include/proxy-wasm/wasmedge.h"
#endif

#include <getopt.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include <string>

// struct for storing plugin configuration
struct WasmInstanceConfig {
  std::list<std::string> config_filenames = {};

  std::list<std::pair<std::shared_ptr<ats_wasm::Wasm>, std::shared_ptr<proxy_wasm::PluginBase>>> configs = {};

  std::list<std::pair<std::shared_ptr<ats_wasm::Wasm>, std::shared_ptr<proxy_wasm::PluginBase>>> deleted_configs = {};
};

static std::unique_ptr<WasmInstanceConfig> wasm_config = nullptr;

// handler for transform event
static int
transform_handler(TSCont contp, ats_wasm::TransformInfo *ti)
{
  TSVConn output_conn;
  TSVIO input_vio;
  TSIOBufferReader input_reader;
  TSIOBufferBlock blk;
  int64_t toread, towrite, blk_len, upstream_done, input_avail;
  const char *start;
  const char *res;
  size_t res_len;
  bool eos, write_down, empty_input;

  ats_wasm::Context *c;

  Dbg(dbg_ctl, "[%s] transform handler begins", __FUNCTION__);
  c = ti->context;

  output_conn = TSTransformOutputVConnGet(contp);
  input_vio   = TSVConnWriteVIOGet(contp);

  empty_input = false;

  Dbg(dbg_ctl, "[%s] cheking input VIO", __FUNCTION__);
  if (!TSVIOBufferGet(input_vio)) {
    if (ti->output_vio) {
      Dbg(dbg_ctl, "[%s] reenabling output VIO after input VIO does not exist", __FUNCTION__);
      TSVIONBytesSet(ti->output_vio, ti->total);
      TSVIOReenable(ti->output_vio);
      return 0;
    } else {
      Dbg(dbg_ctl, "[%s] no input VIO and output VIO", __FUNCTION__);
      empty_input = true;
    }
  }

  if (!empty_input) {
    input_reader = TSVIOReaderGet(input_vio);
  }

  Dbg(dbg_ctl, "[%s] creating buffer and reader", __FUNCTION__);
  if (!ti->output_buffer) {
    ti->output_buffer = TSIOBufferCreate();
    ti->output_reader = TSIOBufferReaderAlloc(ti->output_buffer);

    ti->reserved_buffer = TSIOBufferCreate();
    ti->reserved_reader = TSIOBufferReaderAlloc(ti->reserved_buffer);

    if (!empty_input) {
      ti->upstream_bytes = TSVIONBytesGet(input_vio);
    } else {
      ti->upstream_bytes = 0;
    }

    ti->downstream_bytes = INT64_MAX;
  }

  Dbg(dbg_ctl, "[%s] init variables inside handler", __FUNCTION__);
  if (!empty_input) {
    input_avail   = TSIOBufferReaderAvail(input_reader);
    upstream_done = TSVIONDoneGet(input_vio);
    toread        = TSVIONTodoGet(input_vio);

    if (toread <= input_avail) { // upstream finished
      eos = true;
    } else {
      eos = false;
    }
  } else {
    input_avail   = 0;
    upstream_done = 0;
    toread        = 0;
    eos           = true;
  }

  if (input_avail > 0) {
    // move to the reserved.buffer
    TSIOBufferCopy(ti->reserved_buffer, input_reader, input_avail, 0);

    // reset input
    TSIOBufferReaderConsume(input_reader, input_avail);
    TSVIONDoneSet(input_vio, upstream_done + input_avail);
  }

  write_down = false;
  if (!empty_input) {
    towrite = TSIOBufferReaderAvail(ti->reserved_reader);
  } else {
    towrite = 0;
  }

  do {
    Dbg(dbg_ctl, "[%s] inside transform handler loop", __FUNCTION__);
    proxy_wasm::FilterDataStatus status = proxy_wasm::FilterDataStatus::Continue;

    if (towrite == 0 && !empty_input) {
      break;
    }

    Dbg(dbg_ctl, "[%s] retrieving text and calling the wasm handler function", __FUNCTION__);
    if (!empty_input) {
      blk   = TSIOBufferReaderStart(ti->reserved_reader);
      start = TSIOBufferBlockReadStart(blk, ti->reserved_reader, &blk_len);

      int size = 0;
      if (towrite > blk_len) {
        c->setTransformResult(start, blk_len);
        towrite -= blk_len;
        TSIOBufferReaderConsume(ti->reserved_reader, blk_len);
        size = blk_len;
      } else {
        c->setTransformResult(start, towrite);
        TSIOBufferReaderConsume(ti->reserved_reader, towrite);
        size    = towrite;
        towrite = 0;
      }

      if (!towrite && eos) {
        if (ti->request) {
          status = c->onRequestBody(size, true);
        } else {
          status = c->onResponseBody(size, true);
        }
      } else {
        if (ti->request) {
          status = c->onRequestBody(size, false);
        } else {
          status = c->onResponseBody(size, false);
        }
      }
    } else {
      c->setTransformResult(nullptr, 0);
      if (ti->request) {
        status = c->onRequestBody(0, true);
      } else {
        status = c->onResponseBody(0, true);
      }
    }

    Dbg(dbg_ctl, "[%s] retrieving returns from wasm handler function and pass back to ATS", __FUNCTION__);
    if ((status == proxy_wasm::FilterDataStatus::Continue) ||
        ((status == proxy_wasm::FilterDataStatus::StopIterationAndBuffer ||
          status == proxy_wasm::FilterDataStatus::StopIterationAndWatermark) &&
         eos && !towrite)) {
      res = c->getTransformResult(&res_len);

      if (res && res_len > 0) {
        if (!ti->output_vio) {
          if (eos && !towrite) {
            ti->output_vio = TSVConnWrite(output_conn, contp, ti->output_reader, res_len); // HttpSM go on
          } else {
            ti->output_vio = TSVConnWrite(output_conn, contp, ti->output_reader, ti->downstream_bytes); // HttpSM go on
          }
        }

        TSIOBufferWrite(ti->output_buffer, res, res_len);
        ti->total  += res_len;
        write_down  = true;
      }

      c->clearTransformResult();
    }

    if (status == proxy_wasm::FilterDataStatus::StopIterationNoBuffer) {
      c->clearTransformResult();
    }

    if (eos && !towrite) { // EOS
      break;
    }

  } while (towrite > 0);

  if (eos && !ti->output_vio) {
    ti->output_vio = TSVConnWrite(output_conn, contp, ti->output_reader, 0);
  }

  if (write_down || eos) {
    TSVIOReenable(ti->output_vio);
  }

  if (toread > input_avail) { // upstream not finished.
    if (eos) {
      // this should not happen because eos is set to true if toread <= input_avail
      // we are, though, expecting that eos may be set by the wasm module function in the future
      TSVIONBytesSet(ti->output_vio, ti->total);
      if (!empty_input) {
        TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_EOS, input_vio);
      }
    } else {
      if (!empty_input) {
        TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_READY, input_vio);
      }
    }
  } else { // upstream is finished.
    TSVIONBytesSet(ti->output_vio, ti->total);
    if (!empty_input) {
      TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_COMPLETE, input_vio);
    }
  }

  return 0;
}

static int
transform_entry(TSCont contp, TSEvent ev, void *edata)
{
  int event;
  TSVIO input_vio;
  ats_wasm::TransformInfo *ti;

  event = (int)ev;
  ti    = (ats_wasm::TransformInfo *)TSContDataGet(contp);

  Dbg(dbg_ctl, "[%s] begin transform entry", __FUNCTION__);
  if (TSVConnClosedGet(contp)) {
    delete ti;
    TSContDestroy(contp);
    return 0;
  }

  Dbg(dbg_ctl, "[%s] checking event inside transform entry", __FUNCTION__);
  switch (event) {
  case TS_EVENT_ERROR:
    Dbg(dbg_ctl, "[%s] event error", __FUNCTION__);
    input_vio = TSVConnWriteVIOGet(contp);
    TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
    break;

  // we should handle TS_EVENT_VCONN_EOS similarly here if we support setting EOS from wasm module
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    Dbg(dbg_ctl, "[%s] event vconn write complete", __FUNCTION__);
    TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
    break;

  case TS_EVENT_VCONN_WRITE_READY:
  default:
    Dbg(dbg_ctl, "[%s] event vconn write ready/default", __FUNCTION__);
    transform_handler(contp, ti);
    break;
  }

  return 0;
}

// handler for timer event
static int
schedule_handler(TSCont contp, TSEvent /*event*/, void * /*data*/)
{
  Dbg(dbg_ctl, "[%s] Inside schedule_handler", __FUNCTION__);

  auto *c = static_cast<ats_wasm::Context *>(TSContDataGet(contp));

  auto *old_wasm = static_cast<ats_wasm::Wasm *>(c->wasm());
  TSMutexLock(old_wasm->mutex());

  c->onTick(0); // use 0 as  token

  if (wasm_config->configs.empty()) {
    TSError("[wasm][%s] Configuration objects are empty", __FUNCTION__);
    TSMutexUnlock(old_wasm->mutex());
    return 0;
  }

  bool found = false;
  for (auto it = wasm_config->configs.begin(); it != wasm_config->configs.end(); it++) {
    std::shared_ptr<ats_wasm::Wasm> wbp = it->first;
    if (wbp.get() == old_wasm) {
      found                    = true;
      auto *wasm               = static_cast<ats_wasm::Wasm *>(c->wasm());
      uint32_t root_context_id = c->id();
      if (wasm->existsTimerPeriod(root_context_id)) {
        Dbg(dbg_ctl, "[%s] reschedule continuation", __FUNCTION__);
        std::chrono::milliseconds period = wasm->getTimerPeriod(root_context_id);
        TSContScheduleOnPool(contp, static_cast<TSHRTime>(period.count()), TS_THREAD_POOL_NET);
      } else {
        Dbg(dbg_ctl, "[%s] can't find period for root context id: %d", __FUNCTION__, root_context_id);
      }
      break;
    }
  }

  if (!found) {
    std::shared_ptr<ats_wasm::Wasm> temp = nullptr;
    uint32_t root_context_id             = c->id();
    old_wasm->removeTimerPeriod(root_context_id);

    if (old_wasm->readyShutdown()) {
      Dbg(dbg_ctl, "[%s] starting WasmBase Shutdown", __FUNCTION__);
      old_wasm->startShutdown();
      if (!old_wasm->readyDelete()) {
        Dbg(dbg_ctl, "[%s] not ready to delete WasmBase/PluginBase", __FUNCTION__);
      } else {
        Dbg(dbg_ctl, "[%s] remove wasm from deleted_configs", __FUNCTION__);
        bool advance = true;
        for (auto it = wasm_config->deleted_configs.begin(); it != wasm_config->deleted_configs.end(); advance ? it++ : it) {
          advance = true;
          Dbg(dbg_ctl, "[%s] looping through deleted_configs", __FUNCTION__);
          std::shared_ptr<ats_wasm::Wasm> wbp = it->first;
          temp                                = wbp;
          if (wbp.get() == old_wasm) {
            Dbg(dbg_ctl, "[%s] found matching WasmBase", __FUNCTION__);
            it      = wasm_config->deleted_configs.erase(it);
            advance = false;
          }
        }
      }
    } else {
      Dbg(dbg_ctl, "[%s] not ready to shutdown WasmBase", __FUNCTION__);
    }

    Dbg(dbg_ctl, "[%s] config wasm has changed. thus not scheduling", __FUNCTION__);
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

  context->resetTxnReenable();

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
      context->setTxnReenable();
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
      context->setTxnReenable();
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

  case TS_EVENT_HTTP_TXN_CLOSE: {
    context->onDone();
    context->onDelete();

    bool found = false;
    for (auto it = wasm_config->configs.begin(); it != wasm_config->configs.end(); it++) {
      std::shared_ptr<ats_wasm::Wasm> wbp = it->first;
      if (wbp.get() == context->wasm()) {
        found = true;
        break;
      }
    }

    if (found) {
      Dbg(dbg_ctl, "[%s] config wasm has not changed", __FUNCTION__);
    } else {
      if (old_wasm->readyShutdown()) {
        Dbg(dbg_ctl, "[%s] starting WasmBase Shutdown", __FUNCTION__);
        old_wasm->startShutdown();
        if (!old_wasm->readyDelete()) {
          Dbg(dbg_ctl, "[%s] not ready to delete WasmBase/PluginBase", __FUNCTION__);
        } else {
          Dbg(dbg_ctl, "[%s] remove wasm from deleted_configs", __FUNCTION__);
          bool advance = true;
          for (auto it = wasm_config->deleted_configs.begin(); it != wasm_config->deleted_configs.end(); advance ? it++ : it) {
            advance = true;
            Dbg(dbg_ctl, "[%s] looping through deleted_configs", __FUNCTION__);
            std::shared_ptr<ats_wasm::Wasm> wbp = it->first;
            temp                                = wbp;
            if (wbp.get() == old_wasm) {
              Dbg(dbg_ctl, "[%s] found matching WasmBase", __FUNCTION__);
              it      = wasm_config->deleted_configs.erase(it);
              advance = false;
            }
          }
        }
      } else {
        Dbg(dbg_ctl, "[%s] not ready to shutdown WasmBase", __FUNCTION__);
      }

      Dbg(dbg_ctl, "[%s] config wasm has changed", __FUNCTION__);
    }

    delete context;

    TSContDestroy(contp);
    result = 0;
    break;
  }
  default:
    break;
  }

  TSMutexUnlock(old_wasm->mutex());

  // check if we have reenable transaction already or not
  if ((context == nullptr) || (!context->isTxnReenable())) {
    Dbg(dbg_ctl, "[%s] no context or not yet reenabled transaction", __FUNCTION__);

    if (result == 0) {
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
      if (context != nullptr) {
        context->setTxnReenable();
      }
    } else if (result < 0) {
      Dbg(dbg_ctl, "[%s] abnormal event, continue with error", __FUNCTION__);
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
      if (context != nullptr) {
        context->setTxnReenable();
      }
    } else {
      if (context->isLocalReply()) {
        Dbg(dbg_ctl, "[%s] abnormal return, continue with error due to local reply", __FUNCTION__);
        TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
        if (context != nullptr) {
          context->setTxnReenable();
        }
      } else {
        Dbg(dbg_ctl, "[%s] abnormal return, no continue, context id: %d", __FUNCTION__, context->id());
      }
    }
  } else {
    Dbg(dbg_ctl, "[%s] transaction already reenabled", __FUNCTION__);
  }
  return 0;
}

// main handler/entry point for the plugin
static int
global_hook_handler(TSCont /*contp*/, TSEvent /*event*/, void *data)
{
  auto *txnp = static_cast<TSHttpTxn>(data);
  for (auto it = wasm_config->configs.begin(); it != wasm_config->configs.end(); it++) {
    std::shared_ptr<ats_wasm::Wasm> wbp         = it->first;
    std::shared_ptr<proxy_wasm::PluginBase> plg = it->second;
    auto *wasm                                  = wbp.get();
    TSMutexLock(wasm->mutex());
    auto *rootContext = wasm->getRootContext(plg, false);
    auto *context     = new ats_wasm::Context(wasm, rootContext->id(), plg);
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

    // create transform items
    Dbg(dbg_ctl, "[%s] creating transform info, continuation and hook", __FUNCTION__);
    ats_wasm::TransformInfo *reqbody_ti  = new ats_wasm::TransformInfo();
    reqbody_ti->request                  = true;
    reqbody_ti->context                  = context;
    ats_wasm::TransformInfo *respbody_ti = new ats_wasm::TransformInfo();
    respbody_ti->request                 = false;
    respbody_ti->context                 = context;

    TSVConn reqbody_connp = TSTransformCreate(transform_entry, txnp);
    TSContDataSet(reqbody_connp, reqbody_ti);
    TSVConn respbody_connp = TSTransformCreate(transform_entry, txnp);
    TSContDataSet(respbody_connp, respbody_ti);

    TSHttpTxnHookAdd(txnp, TS_HTTP_REQUEST_TRANSFORM_HOOK, reqbody_connp);
    TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, respbody_connp);
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

// function to read a file
static inline int
read_file(const std::string &fn, std::string *s)
{
  auto fd = open(fn.c_str(), O_RDONLY);
  if (fd < 0) {
    char *errmsg = strerror(errno);
    TSError("[wasm][%s] wasm unable to open: %s", __FUNCTION__, errmsg);
    return -1;
  }
  auto n = ::lseek(fd, 0, SEEK_END);
  if (n < 0) {
    char *errmsg = strerror(errno);
    TSError("[wasm][%s] wasm unable to lseek: %s", __FUNCTION__, errmsg);
    return -1;
  }
  ::lseek(fd, 0, SEEK_SET);
  s->resize(n);
  auto nn = ::read(fd, const_cast<char *>(&*s->begin()), n);
  if (nn < 0) {
    char *errmsg = strerror(errno);
    TSError("[wasm][%s] wasm unable to read: %s", __FUNCTION__, errmsg);
    return -1;
  }
  if (nn != static_cast<ssize_t>(n)) {
    TSError("[wasm][%s] wasm unable to read: size different from buffer", __FUNCTION__);
    return -1;
  }
  return 0;
}

// function to read configuration
static bool
read_configuration()
{
  std::list<std::pair<std::shared_ptr<ats_wasm::Wasm>, std::shared_ptr<proxy_wasm::PluginBase>>> new_configs = {};

  for (auto const &cfn : wasm_config->config_filenames) {
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
      YAML::Node config = YAML::LoadFile(cfn);

      for (YAML::const_iterator it = config.begin(); it != config.end(); ++it) {
        const std::string &node_name = it->first.as<std::string>();
        YAML::NodeType::value type   = it->second.Type();

        if (node_name != "config" || type != YAML::NodeType::Map) {
          TSError(
            "[wasm][%s] Invalid YAML Configuration format for wasm: %s, reason: Top level nodes must be named config and be of "
            "type map",
            __FUNCTION__, cfn.c_str());
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
      TSError("[wasm][%s] YAML::Exception %s when parsing YAML config file %s for wasm", __FUNCTION__, e.what(), cfn.c_str());
      return false;
    }

    std::shared_ptr<ats_wasm::Wasm> wasm;
    if (runtime == "ats.wasm.runtime.wasmedge") {
#ifdef WASMEDGE
      wasm = std::make_shared<ats_wasm::Wasm>(proxy_wasm::createWasmEdgeVm(), // VM
                                              vm_id,                          // vm_id
                                              vm_configuration,               // vm_configuration
                                              "",                             // vm_key,
                                              envs,                           // envs
                                              cap_maps                        // allowed capabilities
      );
#else
      TSError("[wasm][%s] wasm unable to use WasmEdge runtime", __FUNCTION__);
      return false;
#endif
    } else if (runtime == "ats.wasm.runtime.wamr") {
#ifdef WAMR
      wasm = std::make_shared<ats_wasm::Wasm>(proxy_wasm::createWamrVm(), // VM
                                              vm_id,                      // vm_id
                                              vm_configuration,           // vm_configuration
                                              "",                         // vm_key,
                                              envs,                       // envs
                                              cap_maps                    // allowed capabilities
      );
#else
      TSError("[wasm][%s] wasm unable to use WAMR runtime", __FUNCTION__);
      return false;
#endif
    } else {
      TSError("[wasm][%s] wasm unable to use %s runtime", __FUNCTION__, runtime.c_str());
      return false;
    }
    wasm->wasm_vm()->integration() = std::make_unique<ats_wasm::ATSWasmVmIntegration>();

    auto plugin = std::make_shared<proxy_wasm::PluginBase>(name,          // name
                                                           root_id,       // root_id
                                                           vm_id,         // vm_id
                                                           runtime,       // engine
                                                           configuration, // plugin_configuration
                                                           fail_open,     // failopen
                                                           ""             // TODO: plugin key from where ?
    );

    if (*wasm_filename.begin() != '/') {
      wasm_filename = std::string(TSConfigDirGet()) + "/" + wasm_filename;
    }
    std::string code;
    if (read_file(wasm_filename, &code) < 0) {
      TSError("[wasm][%s] wasm unable to read file '%s'", __FUNCTION__, wasm_filename.c_str());
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

    auto new_config = std::make_pair(wasm, plugin);
    new_configs.push_front(new_config);
  }

  auto old_configs = wasm_config->configs;

  wasm_config->configs = new_configs;

  for (auto it = old_configs.begin(); it != old_configs.end(); it++) {
    std::shared_ptr<ats_wasm::Wasm> old_wasm           = it->first;
    std::shared_ptr<proxy_wasm::PluginBase> old_plugin = it->second;

    if (old_wasm != nullptr) {
      Dbg(dbg_ctl, "[%s] previous WasmBase exists", __FUNCTION__);
      TSMutexLock(old_wasm->mutex());
      if (old_wasm->readyShutdown()) {
        Dbg(dbg_ctl, "[%s] starting WasmBase Shutdown", __FUNCTION__);
        old_wasm->startShutdown();
        if (!old_wasm->readyDelete()) {
          Dbg(dbg_ctl, "[%s] not ready to delete WasmBase/PluginBase", __FUNCTION__);
          auto deleted_config = std::make_pair(old_wasm, old_plugin);
          wasm_config->deleted_configs.push_front(deleted_config);
        }
      } else {
        Dbg(dbg_ctl, "[%s] not ready to shutdown WasmBase", __FUNCTION__);
        auto deleted_config = std::make_pair(old_wasm, old_plugin);
        wasm_config->deleted_configs.push_front(deleted_config);
      }
      TSMutexUnlock(old_wasm->mutex());
    }
  }

  return true;
}

// handler for configuration event
static int
config_handler(TSCont /*contp*/, TSEvent /*event*/, void * /*data*/)
{
  Dbg(dbg_ctl, "[%s] configuration reloading", __FUNCTION__);
  read_configuration();
  Dbg(dbg_ctl, "[%s] configuration reloading ends", __FUNCTION__);
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

  for (int i = 1; i < argc; i++) {
    std::string filename = std::string(argv[i]);
    if (*filename.begin() != '/') {
      filename = std::string(TSConfigDirGet()) + "/" + filename;
    }
    wasm_config->config_filenames.push_front(filename);
  }

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
