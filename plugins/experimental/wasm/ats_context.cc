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

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "ats_context.h"
#include "ats_wasm.h"

#include "include/proxy-wasm/context.h"
#include "include/proxy-wasm/wasm.h"
#include "src/shared_data.h"
#include "src/shared_queue.h"

namespace ats_wasm
{

DbgCtl dbg_ctl{WASM_DEBUG_TAG};

static int
async_handler(TSCont cont, TSEvent event, void *edata)
{
  // information for the handler
  TSHttpTxn txn         = static_cast<TSHttpTxn>(edata);
  AsyncInfo *ai         = static_cast<AsyncInfo *>(TSContDataGet(cont));
  uint32_t token        = ai->token;
  Context *root_context = ai->root_context;
  Wasm *wasm            = root_context->wasm();

  // variables to be used in handler
  TSEvent result    = static_cast<TSEvent>(FETCH_EVENT_ID_BASE + 1);
  const void *body  = nullptr;
  size_t body_size  = 0;
  TSMBuffer hdr_buf = nullptr;
  TSMLoc hdr_loc    = nullptr;
  int header_size   = 0;

  TSMutexLock(wasm->mutex());
  // filling in variables for a successful fetch
  if (event == static_cast<TSEvent>(FETCH_EVENT_ID_BASE)) {
    int data_len;
    const char *data_start = TSFetchRespGet(txn, &data_len);
    if (data_start && (data_len > 0)) {
      const char *data_end = data_start + data_len;
      TSHttpParser parser  = TSHttpParserCreate();
      hdr_buf              = TSMBufferCreate();
      hdr_loc              = TSHttpHdrCreate(hdr_buf);
      TSHttpHdrTypeSet(hdr_buf, hdr_loc, TS_HTTP_TYPE_RESPONSE);
      if (TSHttpHdrParseResp(parser, hdr_buf, hdr_loc, &data_start, data_end) == TS_PARSE_DONE) {
        TSHttpStatus status = TSHttpHdrStatusGet(hdr_buf, hdr_loc);
        header_size         = TSMimeHdrFieldsCount(hdr_buf, hdr_loc);
        body                = data_start; // data_start will now be pointing to body
        body_size           = data_end - data_start;
        Dbg(dbg_ctl, "[%s] Fetch result had a status code of %d with a body length of %ld", __FUNCTION__, status, body_size);
      } else {
        TSError("[wasm][%s] Unable to parse call response", __FUNCTION__);
        event = static_cast<TSEvent>(FETCH_EVENT_ID_BASE + 1);
      }
      TSHttpParserDestroy(parser);
    } else {
      TSError("[wasm][%s] Successful fetch did not result in any content. Assuming failure", __FUNCTION__);
      event = static_cast<TSEvent>(FETCH_EVENT_ID_BASE + 1);
    }
    result = event;
  }

  // callback function
  Dbg(dbg_ctl, "[%s] setting root context call result", __FUNCTION__);
  root_context->setHttpCallResult(hdr_buf, hdr_loc, body, body_size, result);
  Dbg(dbg_ctl, "[%s] trigger root context function, token:  %d", __FUNCTION__, token);
  root_context->onHttpCallResponse(token, header_size, body_size, 0);
  Dbg(dbg_ctl, "[%s] resetting root context call result", __FUNCTION__);
  root_context->resetHttpCallResult();

  // cleaning up
  if (hdr_loc) {
    TSMLoc null_parent_loc = nullptr;
    TSHandleMLocRelease(hdr_buf, null_parent_loc, hdr_loc);
  }
  if (hdr_buf) {
    TSMBufferDestroy(hdr_buf);
  }

  TSMutexUnlock(wasm->mutex());

  Dbg(dbg_ctl, "[%s] delete async info and continuation", __FUNCTION__);
  // delete the Async Info
  delete ai;
  // delete continuation
  TSContDestroy(cont);

  return 0;
}

// utiltiy function for properties
static void
print_address(struct sockaddr const *ip, std::string *result)
{
  if (ip != nullptr) {
    char cip[128];
    int64_t port = 0;
    if (ip->sa_family == AF_INET) {
      const auto *s_sockaddr_in = reinterpret_cast<const struct sockaddr_in *>(ip);
      inet_ntop(AF_INET, &s_sockaddr_in->sin_addr, cip, sizeof(cip));
      port = s_sockaddr_in->sin_port;
    } else {
      const auto *s_sockaddr_in6 = reinterpret_cast<const struct sockaddr_in6 *>(ip);
      inet_ntop(AF_INET6, &s_sockaddr_in6->sin6_addr, cip, sizeof(cip));
      port = s_sockaddr_in6->sin6_port;
    }
    Dbg(dbg_ctl, "[%s] property retrieval - address: %.*s", __FUNCTION__, static_cast<int>(sizeof(cip)), cip);
    std::string cip_str(cip);
    result->assign(cip_str + ":" + std::to_string(port));
  } else {
    *result = pv_empty;
  }
}

static void
print_port(struct sockaddr const *ip, std::string *result)
{
  if (ip != nullptr) {
    int64_t port = 0;
    if (ip->sa_family == AF_INET) {
      const auto *s_sockaddr_in = reinterpret_cast<const struct sockaddr_in *>(ip);
      port                      = s_sockaddr_in->sin_port;
    } else {
      const auto *s_sockaddr_in6 = reinterpret_cast<const struct sockaddr_in6 *>(ip);
      port                       = s_sockaddr_in6->sin6_port;
    }
    Dbg(dbg_ctl, "[%s] looking for source port: %d", __FUNCTION__, static_cast<int>(port));
    result->assign(reinterpret_cast<const char *>(&port), sizeof(int64_t));
  } else {
    *result = pv_empty;
  }
}

static void
print_certificate(std::string *result, X509_NAME *name)
{
  if (name == nullptr) {
    *result = pv_empty;
    return;
  }

  BIO *bio = BIO_new(BIO_s_mem());
  if (bio == nullptr) {
    *result = pv_empty;
    return;
  }

  if (X509_NAME_print_ex(bio, name, 0 /* indent */, XN_FLAG_ONELINE) > 0) {
    int64_t len = 0;
    char *ptr   = nullptr;
    len         = BIO_get_mem_data(bio, &ptr);
    result->assign(ptr, len);
    Dbg(dbg_ctl, "print SSL certificate %.*s", static_cast<int>(len), ptr);
  }

  BIO_free(bio);
}

static void
print_san_certificate(std::string *result, X509 *cert, int type)
{
  int ext_ndx = X509_get_ext_by_NID(cert, NID_subject_alt_name, -1);
  if (ext_ndx >= 0) {
    X509_EXTENSION *ext               = nullptr;
    STACK_OF(GENERAL_NAME) *alt_names = nullptr;
    GENERAL_NAME *gen_name            = nullptr;

    ext       = X509_get_ext(cert, ext_ndx);
    alt_names = static_cast<stack_st_GENERAL_NAME *>(X509V3_EXT_d2i(ext));
    if (alt_names != nullptr) {
      int num    = sk_GENERAL_NAME_num(alt_names);
      bool found = false;
      for (int i = 0; i < num; i++) {
        gen_name = sk_GENERAL_NAME_value(alt_names, i);
        if (gen_name->type == type) {
          char *dnsname   = reinterpret_cast<char *>(ASN1_STRING_data(gen_name->d.dNSName));
          int dnsname_len = ASN1_STRING_length(gen_name->d.dNSName);
          result->assign(dnsname, dnsname_len);
          found = true;
          break;
        }
      }
      if (!found) {
        *result = pv_empty;
      }
      sk_GENERAL_NAME_free(alt_names);
    } else {
      *result = pv_empty;
    }
  } else {
    *result = pv_empty;
  }
}

static bool
get_header(TSMBuffer bufp, TSMLoc hdr_loc, std::string_view v, std::string *result)
{
  const char *key       = v.data();
  int key_len           = v.size();
  const char *val       = nullptr;
  int val_len           = 0;
  std::string res       = "";
  TSMLoc field_loc      = nullptr;
  TSMLoc next_field_loc = nullptr;
  bool found            = false;

  field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, key, key_len);
  if (field_loc != TS_NULL_MLOC) {
    while (field_loc != TS_NULL_MLOC) {
      val            = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, -1, &val_len);
      next_field_loc = TSMimeHdrFieldNextDup(bufp, hdr_loc, field_loc);
      res.append(val, val_len);
      if (next_field_loc != TS_NULL_MLOC) {
        res.append(",", 1);
      }
      TSHandleMLocRelease(bufp, hdr_loc, field_loc);
      field_loc = next_field_loc;
    }
    found = true;
  }
  *result = res;
  return found;
}

static void
set_header(TSMBuffer bufp, TSMLoc hdr_loc, std::string_view v, std::string_view serialized_value)
{
  bool remove = false;

  if (serialized_value == "") {
    remove = true;
  }

  TSMLoc field_loc = nullptr;
  TSMLoc tmp       = nullptr;
  bool first       = true;

  field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, v.data(), v.size());

  if (remove) {
    while (field_loc != TS_NULL_MLOC) {
      tmp = TSMimeHdrFieldNextDup(bufp, hdr_loc, field_loc);
      TSMimeHdrFieldDestroy(bufp, hdr_loc, field_loc);
      TSHandleMLocRelease(bufp, hdr_loc, field_loc);
      field_loc = tmp;
    }
  } else if (field_loc != TS_NULL_MLOC) {
    first = true;
    while (field_loc != TS_NULL_MLOC) {
      tmp = TSMimeHdrFieldNextDup(bufp, hdr_loc, field_loc);
      if (first) {
        first = false;
        TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, serialized_value.data(), serialized_value.size());
      } else {
        TSMimeHdrFieldDestroy(bufp, hdr_loc, field_loc);
      }
      TSHandleMLocRelease(bufp, hdr_loc, field_loc);
      field_loc = tmp;
    }
  } else if (TSMimeHdrFieldCreateNamed(bufp, hdr_loc, v.data(), v.size(), &field_loc) != TS_SUCCESS) {
    TSError("[wasm][%s] TSMimeHdrFieldCreateNamed error", __FUNCTION__);
  } else {
    TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, serialized_value.data(), serialized_value.size());
    TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc);
  }

  if (field_loc != TS_NULL_MLOC) {
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  }
}

// Buffer copyTo
WasmResult
Buffer::copyTo(WasmBase *wasm, size_t start, size_t length, uint64_t ptr_ptr, uint64_t size_ptr) const
{
  if (owned_data_str_ != "") {
    std::string_view s(owned_data_str_);
    if (!wasm->copyToPointerSize(s, ptr_ptr, size_ptr)) {
      return WasmResult::InvalidMemoryAccess;
    }
    return WasmResult::Ok;
  }
  return BufferBase::copyTo(wasm, start, length, ptr_ptr, size_ptr);
}

Context::Context() : ContextBase() {}

Context::Context(Wasm *wasm) : ContextBase(wasm) {}

Context::Context(Wasm *wasm, const std::shared_ptr<PluginBase> &plugin) : ContextBase(wasm, plugin) {}

// NB: wasm can be nullptr if it failed to be created successfully.
Context::Context(Wasm *wasm, uint32_t parent_context_id, const std::shared_ptr<PluginBase> &plugin) : ContextBase(wasm, plugin)
{
  // setting up parent context
  parent_context_id_ = parent_context_id;
  if (wasm_ != nullptr) {
    parent_context_ = wasm_->getContext(parent_context_id_);
  }
}

// utility functions for the extended class
Wasm *
Context::wasm() const
{
  return static_cast<Wasm *>(wasm_);
}

Context *
Context::parent_context() const
{
  return static_cast<Context *>(parent_context_);
}

Context *
Context::root_context() const
{
  const ContextBase *previous = this;
  ContextBase *parent         = parent_context_;
  while (parent != previous) {
    previous = parent;
    parent   = parent->parent_context();
  }
  return static_cast<Context *>(parent);
}

void
Context::initialize(TSHttpTxn txnp)
{
  txnp_ = txnp;
}

void
Context::initialize(TSCont cont)
{
  scheduler_cont_ = cont;
}

TSHttpTxn
Context::txnp()
{
  return txnp_;
}

TSCont
Context::scheduler_cont()
{
  return scheduler_cont_;
}

void
Context::error(std::string_view message)
{
  TSError("%.*s", static_cast<int>(message.size()), message.data());
  abort();
}

// local reply handler
void
Context::onLocalReply()
{
  if (local_reply_) {
    if (txnp_ == nullptr) {
      return;
    }

    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;
    if (TSHttpTxnClientRespGet(txnp_, &bufp, &hdr_loc) != TS_SUCCESS) {
      return;
    }

    // set local reply reason
    if (!local_reply_details_.empty()) {
      TSHttpHdrReasonSet(bufp, hdr_loc, local_reply_details_.data(), local_reply_details_.size());
    }

    // set local reply headers
    for (auto &p : local_reply_headers_) {
      std::string key(p.first);
      std::string value(p.second);

      auto *loc = TSMimeHdrFieldFind(bufp, hdr_loc, key.data(), static_cast<int>(key.size()));
      if (loc != TS_NULL_MLOC) {
        int first = 1;
        while (loc != TS_NULL_MLOC) {
          auto *tmp = TSMimeHdrFieldNextDup(bufp, hdr_loc, loc);
          if (first != 0) {
            first = 0;
            TSMimeHdrFieldValueStringSet(bufp, hdr_loc, loc, -1, value.data(), static_cast<int>(value.size()));
          } else {
            TSMimeHdrFieldDestroy(bufp, hdr_loc, loc);
          }
          TSHandleMLocRelease(bufp, hdr_loc, loc);
          loc = tmp;
        }
      } else if (TSMimeHdrFieldCreateNamed(bufp, hdr_loc, key.data(), static_cast<int>(key.size()), &loc) != TS_SUCCESS) {
        TSError("[wasm][%s] TSMimeHdrFieldCreateNamed error", __FUNCTION__);
        TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
        return;
      } else {
        TSMimeHdrFieldValueStringSet(bufp, hdr_loc, loc, -1, value.data(), static_cast<int>(value.size()));
        TSMimeHdrFieldAppend(bufp, hdr_loc, loc);
      }

      if (loc != TS_NULL_MLOC) {
        TSHandleMLocRelease(bufp, hdr_loc, loc);
      }
    }

    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  }
}

//
// General functions
//
WasmResult
Context::log(uint32_t level, std::string_view message)
{
  auto l = static_cast<LogLevel>(level);
  switch (l) {
  case LogLevel::trace:
    Dbg(dbg_ctl, "wasm trace log%s: %.*s", std::string(log_prefix()).c_str(), static_cast<int>(message.size()), message.data());
    return WasmResult::Ok;
  case LogLevel::debug:
    Dbg(dbg_ctl, "wasm debug log%s: %.*s", std::string(log_prefix()).c_str(), static_cast<int>(message.size()), message.data());
    return WasmResult::Ok;
  case LogLevel::info:
    Dbg(dbg_ctl, "wasm info log%s: %.*s", std::string(log_prefix()).c_str(), static_cast<int>(message.size()), message.data());
    return WasmResult::Ok;
  case LogLevel::warn:
    Dbg(dbg_ctl, "wasm warn log%s: %.*s", std::string(log_prefix()).c_str(), static_cast<int>(message.size()), message.data());
    return WasmResult::Ok;
  case LogLevel::error:
    Dbg(dbg_ctl, "wasm error log%s: %.*s", std::string(log_prefix()).c_str(), static_cast<int>(message.size()), message.data());
    return WasmResult::Ok;
  case LogLevel::critical:
    Dbg(dbg_ctl, "wasm critical log%s: %.*s", std::string(log_prefix()).c_str(), static_cast<int>(message.size()), message.data());
    return WasmResult::Ok;
  default: // e.g. off
    return unimplemented();
  }
  return unimplemented();
}

uint64_t
Context::getCurrentTimeNanoseconds()
{
  return TShrtime();
}

uint64_t
Context::getMonotonicTimeNanoseconds()
{
  return TShrtime();
}

std::string_view
Context::getConfiguration()
{
  return plugin_->plugin_configuration_;
}

WasmResult
Context::setTimerPeriod(std::chrono::milliseconds period, uint32_t *timer_token_ptr)
{
  Wasm *wasm            = this->wasm();
  Context *root_context = this->root_context();
  TSMutexLock(wasm->mutex());
  if (!wasm->existsTimerPeriod(root_context->id())) {
    Dbg(dbg_ctl, "[%s] no previous timer period set", __FUNCTION__);
    TSCont contp = root_context->scheduler_cont();
    if (contp != nullptr) {
      Dbg(dbg_ctl, "[%s] scheduling continuation for timer", __FUNCTION__);
      TSContDataSet(contp, root_context);
      TSContScheduleOnPool(contp, static_cast<TSHRTime>(period.count()), TS_THREAD_POOL_NET);
    }
  }

  wasm->setTimerPeriod(root_context->id(), period);
  *timer_token_ptr = 0;
  TSMutexUnlock(wasm->mutex());
  return WasmResult::Ok;
}

BufferInterface *
Context::getBuffer(WasmBufferType type)
{
  switch (type) {
  case WasmBufferType::VmConfiguration:
    return buffer_.set(wasm_->vm_configuration());
  case WasmBufferType::PluginConfiguration:
    return buffer_.set(plugin_->plugin_configuration_);
  case WasmBufferType::HttpCallResponseBody:
    if (cr_body_ != nullptr) {
      return buffer_.set(std::string(static_cast<const char *>(cr_body_), cr_body_size_));
    }
    return buffer_.set("");
  case WasmBufferType::HttpRequestBody:
  case WasmBufferType::HttpResponseBody:
    // return transform result
    return &transform_result_;
  case WasmBufferType::CallData:
  case WasmBufferType::NetworkDownstreamData:
  case WasmBufferType::NetworkUpstreamData:
  case WasmBufferType::GrpcReceiveBuffer:
  default:
    unimplemented();
    return nullptr;
  }
}

WasmResult
Context::httpCall(std::string_view target, const Pairs &request_headers, std::string_view request_body,
                  const Pairs &request_trailers, int timeout_millisconds, uint32_t *token_ptr)
{
  Wasm *wasm            = this->wasm();
  Context *root_context = this->root_context();

  TSCont contp;
  std::string request, method, path, authority;

  // setup local address for API call
  struct sockaddr_in addr;
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = LOCAL_IP_ADDRESS;
  addr.sin_port        = LOCAL_PORT;

  for (const auto &p : request_headers) {
    std::string key(p.first);
    std::string value(p.second);

    if (key == ":method") {
      method = value;
    } else if (key == ":path") {
      path = value;
    } else if (key == ":authority") {
      authority = value;
    }
  }

  /* request */
  request = method + " https://" + authority + path + " HTTP/1.1\r\n";
  for (const auto &p : request_headers) {
    std::string key(p.first);
    std::string value(p.second);
    request += key + ": " + value + "\r\n";
  }
  request += "\r\n";
  request += request_body;

  TSFetchEvent event_ids;
  event_ids.success_event_id = FETCH_EVENT_ID_BASE;
  event_ids.failure_event_id = FETCH_EVENT_ID_BASE + 1;
  event_ids.timeout_event_id = FETCH_EVENT_ID_BASE + 2;

  contp            = TSContCreate(async_handler, TSMutexCreate());
  AsyncInfo *ai    = new AsyncInfo();
  ai->token        = wasm->nextHttpCallId();
  ai->root_context = root_context;
  *token_ptr       = ai->token; // to be returned to the caller
  TSContDataSet(contp, ai);

  // API call for async fetch
  TSFetchUrl(request.c_str(), request.size(), reinterpret_cast<struct sockaddr const *>(&addr), contp, AFTER_BODY, event_ids);

  return WasmResult::Ok;
}

// Metrics
WasmResult
Context::defineMetric(uint32_t metric_type, std::string_view name, uint32_t *metric_id_ptr)
{
  int idp                    = 0;
  TSStatSync ats_metric_type = TS_STAT_SYNC_COUNT;
  auto type                  = static_cast<MetricType>(metric_type);
  switch (type) {
  case MetricType::Counter:
    ats_metric_type = TS_STAT_SYNC_COUNT;
    break;
  case MetricType::Gauge:
    ats_metric_type = TS_STAT_SYNC_SUM;
    break;
  case MetricType::Histogram:
    ats_metric_type = TS_STAT_SYNC_AVG;
    break;
  default:
    TSError("[wasm][%s] Invalid metric type", __FUNCTION__);
    return WasmResult::BadArgument;
  }

  if (TSStatFindName(name.data(), &idp) == TS_ERROR) {
    idp = TSStatCreate(name.data(), TS_RECORDDATATYPE_INT, TS_STAT_PERSISTENT, ats_metric_type);
    Dbg(dbg_ctl, "[%s] creating stat: %.*s", __FUNCTION__, static_cast<int>(name.size()), name.data());
    *metric_id_ptr = idp;
  } else {
    TSError("[wasm][%s] Metric already exists", __FUNCTION__);
    *metric_id_ptr = idp;
  }

  return WasmResult::Ok;
}

WasmResult
Context::incrementMetric(uint32_t metric_id, int64_t offset)
{
  TSStatIntIncrement(metric_id, offset);
  return WasmResult::Ok;
}

WasmResult
Context::recordMetric(uint32_t metric_id, uint64_t value)
{
  TSStatIntSet(metric_id, value);
  return WasmResult::Ok;
}

WasmResult
Context::getMetric(uint32_t metric_id, uint64_t *value_ptr)
{
  *value_ptr = TSStatIntGet(metric_id);
  return WasmResult::Ok;
}

// Properties
WasmResult
Context::getProperty(std::string_view path, std::string *result)
{
  if (path.substr(0, p_plugin_root_id.size()) == p_plugin_root_id) {
    *result = this->plugin_->root_id_;
    Dbg(dbg_ctl, "[%s] looking for plugin_root_id: %.*s", __FUNCTION__, static_cast<int>((*result).size()), (*result).data());
    return WasmResult::Ok;
  } else if (path.substr(0, p_plugin_name.size()) == p_plugin_name) {
    *result = this->plugin_->name_;
    Dbg(dbg_ctl, "[%s] looking for plugin_name: %.*s", __FUNCTION__, static_cast<int>((*result).size()), (*result).data());
    return WasmResult::Ok;
  } else if (path.substr(0, p_plugin_vm_id.size()) == p_plugin_vm_id) {
    *result = this->plugin_->vm_id_;
    Dbg(dbg_ctl, "[%s] looking for plugin_vm_id: %.*s", __FUNCTION__, static_cast<int>((*result).size()), (*result).data());
    return WasmResult::Ok;
  } else if (path.substr(0, p_node.size()) == p_node) {
    *result = pv_empty;
    Dbg(dbg_ctl, "[%s] looking for node property: empty string for now", __FUNCTION__);
    return WasmResult::Ok;
  } else if (path.substr(0, p_source_address.size()) == p_source_address) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    struct sockaddr const *client_ip = nullptr;
    client_ip                        = TSHttpTxnClientAddrGet(txnp_);
    print_address(client_ip, result);
    return WasmResult::Ok;
  } else if (path.substr(0, p_source_port.size()) == p_source_port) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    struct sockaddr const *client_ip = nullptr;
    client_ip                        = TSHttpTxnClientAddrGet(txnp_);
    print_port(client_ip, result);
    return WasmResult::Ok;
  } else if (path.substr(0, p_destination_address.size()) == p_destination_address) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    struct sockaddr const *local_ip = nullptr;
    local_ip                        = TSHttpTxnIncomingAddrGet(txnp_);
    print_address(local_ip, result);
    return WasmResult::Ok;
  } else if (path.substr(0, p_destination_port.size()) == p_destination_port) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    struct sockaddr const *local_ip = nullptr;
    local_ip                        = TSHttpTxnIncomingAddrGet(txnp_);
    print_port(local_ip, result);
    return WasmResult::Ok;
  } else if (path.substr(0, p_connection_mtls.size()) == p_connection_mtls) {
    bool m = false;
    if (txnp_ == nullptr) {
      result->assign(reinterpret_cast<const char *>(&m), sizeof(bool));
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnClientVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
#ifdef OPENSSL_IS_OPENSSL3
    X509 *cert = SSL_get1_peer_certificate(ssl);
#else
    X509 *cert = SSL_get_peer_certificate(ssl);
#endif
    if (cert != nullptr) {
      m = true;
      X509_free(cert);
    }
    result->assign(reinterpret_cast<const char *>(&m), sizeof(bool));
    return WasmResult::Ok;
  } else if (path.substr(0, p_connection_requested_server_name.size()) == p_connection_requested_server_name) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp             = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn        = TSHttpSsnClientVConnGet(ssnp);
    TSSslConnection sslobj     = TSVConnSslConnectionGet(client_conn);
    SSL *ssl                   = reinterpret_cast<SSL *>(sslobj);
    char const *const sni_name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
    if (sni_name != nullptr) {
      result->assign(sni_name);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_connection_tls_version.size()) == p_connection_tls_version) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    const char *ssl_protocol = "-";
    TSHttpSsn ssnp           = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn      = TSHttpSsnClientVConnGet(ssnp);

    if (TSVConnIsSsl(client_conn) != 0) {
      ssl_protocol = TSVConnSslProtocolGet(client_conn);
    }

    result->assign(ssl_protocol);
    return WasmResult::Ok;
  } else if (path.substr(0, p_connection_subject_local_certificate.size()) == p_connection_subject_local_certificate) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnClientVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
    X509 *cert             = SSL_get_certificate(ssl);
    if (cert != nullptr) {
      print_certificate(result, X509_get_subject_name(cert));
      X509_free(cert);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_connection_subject_peer_certificate.size()) == p_connection_subject_peer_certificate) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnClientVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
#ifdef OPENSSL_IS_OPENSSL3
    X509 *cert = SSL_get1_peer_certificate(ssl);
#else
    X509 *cert = SSL_get_peer_certificate(ssl);
#endif
    if (cert != nullptr) {
      print_certificate(result, X509_get_subject_name(cert));
      X509_free(cert);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_connection_dns_san_local_certificate.size()) == p_connection_dns_san_local_certificate) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnClientVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
    X509 *cert             = SSL_get_certificate(ssl);
    if (cert != nullptr) {
      print_san_certificate(result, cert, GEN_DNS);
      X509_free(cert);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_connection_dns_san_peer_certificate.size()) == p_connection_dns_san_peer_certificate) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnClientVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
#ifdef OPENSSL_IS_OPENSSL3
    X509 *cert = SSL_get1_peer_certificate(ssl);
#else
    X509 *cert = SSL_get_peer_certificate(ssl);
#endif
    if (cert != nullptr) {
      print_san_certificate(result, cert, GEN_DNS);
      X509_free(cert);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_connection_uri_san_local_certificate.size()) == p_connection_uri_san_local_certificate) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnClientVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
    X509 *cert             = SSL_get_certificate(ssl);
    if (cert != nullptr) {
      print_san_certificate(result, cert, GEN_URI);
      X509_free(cert);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_connection_uri_san_peer_certificate.size()) == p_connection_uri_san_peer_certificate) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnClientVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
#ifdef OPENSSL_IS_OPENSSL3
    X509 *cert = SSL_get1_peer_certificate(ssl);
#else
    X509 *cert = SSL_get_peer_certificate(ssl);
#endif
    if (cert != nullptr) {
      print_san_certificate(result, cert, GEN_URI);
      X509_free(cert);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_upstream_address.size()) == p_upstream_address) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    struct sockaddr const *server_ip = nullptr;
    server_ip                        = TSHttpTxnServerAddrGet(txnp_);
    print_address(server_ip, result);
    return WasmResult::Ok;
  } else if (path.substr(0, p_upstream_port.size()) == p_upstream_port) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    struct sockaddr const *server_ip = nullptr;
    server_ip                        = TSHttpTxnClientAddrGet(txnp_);
    print_port(server_ip, result);
    return WasmResult::Ok;
  } else if (path.substr(0, p_upstream_local_address.size()) == p_upstream_local_address) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    struct sockaddr const *local_ip = nullptr;
    local_ip                        = TSHttpTxnOutgoingAddrGet(txnp_);
    print_address(local_ip, result);
    return WasmResult::Ok;
  } else if (path.substr(0, p_upstream_local_port.size()) == p_upstream_local_port) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    struct sockaddr const *local_ip = nullptr;
    local_ip                        = TSHttpTxnOutgoingAddrGet(txnp_);
    print_port(local_ip, result);
    return WasmResult::Ok;
  } else if (path.substr(0, p_upstream_tls_version.size()) == p_upstream_tls_version) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    const char *ssl_protocol = "-";
    TSHttpSsn ssnp           = TSHttpTxnSsnGet(txnp_);
    TSVConn server_conn      = TSHttpSsnServerVConnGet(ssnp);

    if (TSVConnIsSsl(server_conn) != 0) {
      ssl_protocol = TSVConnSslProtocolGet(server_conn);
    }

    result->assign(ssl_protocol);
    return WasmResult::Ok;
  } else if (path.substr(0, p_upstream_subject_local_certificate.size()) == p_upstream_subject_local_certificate) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnServerVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
    X509 *cert             = SSL_get_certificate(ssl);
    if (cert != nullptr) {
      print_certificate(result, X509_get_subject_name(cert));
      X509_free(cert);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_upstream_subject_peer_certificate.size()) == p_upstream_subject_peer_certificate) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnServerVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
#ifdef OPENSSL_IS_OPENSSL3
    X509 *cert = SSL_get1_peer_certificate(ssl);
#else
    X509 *cert = SSL_get_peer_certificate(ssl);
#endif
    if (cert != nullptr) {
      print_certificate(result, X509_get_subject_name(cert));
      X509_free(cert);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_upstream_dns_san_local_certificate.size()) == p_upstream_dns_san_local_certificate) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnServerVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
    X509 *cert             = SSL_get_certificate(ssl);
    if (cert != nullptr) {
      print_san_certificate(result, cert, GEN_DNS);
      X509_free(cert);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_upstream_dns_san_peer_certificate.size()) == p_upstream_dns_san_peer_certificate) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnServerVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
#ifdef OPENSSL_IS_OPENSSL3
    X509 *cert = SSL_get1_peer_certificate(ssl);
#else
    X509 *cert = SSL_get_peer_certificate(ssl);
#endif
    if (cert != nullptr) {
      print_san_certificate(result, cert, GEN_DNS);
      X509_free(cert);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_upstream_uri_san_local_certificate.size()) == p_upstream_uri_san_local_certificate) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnServerVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
    X509 *cert             = SSL_get_certificate(ssl);
    if (cert != nullptr) {
      print_san_certificate(result, cert, GEN_URI);
      X509_free(cert);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_upstream_uri_san_peer_certificate.size()) == p_upstream_uri_san_peer_certificate) {
    if (txnp_ == nullptr) {
      *result = pv_empty;
      return WasmResult::Ok;
    }
    TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp_);
    TSVConn client_conn    = TSHttpSsnServerVConnGet(ssnp);
    TSSslConnection sslobj = TSVConnSslConnectionGet(client_conn);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
#ifdef OPENSSL_IS_OPENSSL3
    X509 *cert = SSL_get1_peer_certificate(ssl);
#else
    X509 *cert = SSL_get_peer_certificate(ssl);
#endif
    if (cert != nullptr) {
      print_san_certificate(result, cert, GEN_URI);
      X509_free(cert);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_path.size()) == p_request_path) {
    TSMBuffer bufp    = nullptr;
    TSMLoc hdr_loc    = nullptr;
    TSMLoc url_loc    = nullptr;
    const char *path  = nullptr;
    int path_len      = 0;
    const char *query = nullptr;
    int query_len     = 0;

    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) == TS_SUCCESS) {
        path = TSUrlPathGet(bufp, url_loc, &path_len);
        std::string path_str(path, path_len);

        query = TSUrlHttpQueryGet(bufp, url_loc, &query_len);
        std::string query_str(query, query_len);

        if (query_len > 0) {
          result->assign("/" + path_str + "?" + query_str);
        } else {
          result->assign("/" + path_str);
        }
        TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      } else {
        *result = pv_empty;
      }
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_url_path.size()) == p_request_url_path) {
    TSMBuffer bufp   = nullptr;
    TSMLoc hdr_loc   = nullptr;
    TSMLoc url_loc   = nullptr;
    const char *path = nullptr;
    int path_len     = 0;

    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) == TS_SUCCESS) {
        path = TSUrlPathGet(bufp, url_loc, &path_len);
        std::string path_str(path, path_len);
        result->assign("/" + path_str);
        TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      } else {
        *result = pv_empty;
      }
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_host.size()) == p_request_host) {
    TSMBuffer bufp   = nullptr;
    TSMLoc hdr_loc   = nullptr;
    TSMLoc url_loc   = nullptr;
    const char *host = nullptr;
    int host_len     = 0;

    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) == TS_SUCCESS) {
        host = TSUrlHostGet(bufp, url_loc, &host_len);
        if (host_len == 0) {
          const char *key   = "Host";
          const char *l_key = "host";
          int key_len       = 4;

          TSMLoc field_loc = nullptr;

          field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, key, key_len);
          if (field_loc != nullptr) {
            host = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, -1, &host_len);
            TSHandleMLocRelease(bufp, hdr_loc, field_loc);
          } else {
            field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, l_key, key_len);
            if (field_loc != nullptr) {
              host = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, -1, &host_len);
              TSHandleMLocRelease(bufp, hdr_loc, field_loc);
            }
          }
        }
        Dbg(dbg_ctl, "[%s] request host value(%d): %.*s", __FUNCTION__, host_len, host_len, host);
        result->assign(host, host_len);
        TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      } else {
        *result = pv_empty;
      }
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_scheme.size()) == p_request_scheme) {
    TSMBuffer bufp     = nullptr;
    TSMLoc hdr_loc     = nullptr;
    TSMLoc url_loc     = nullptr;
    const char *scheme = nullptr;
    int scheme_len     = 0;

    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) == TS_SUCCESS) {
        scheme = TSUrlSchemeGet(bufp, url_loc, &scheme_len);
        result->assign(scheme, scheme_len);
        TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      } else {
        *result = pv_empty;
      }
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_method.size()) == p_request_method) {
    TSMBuffer bufp     = nullptr;
    TSMLoc hdr_loc     = nullptr;
    const char *method = nullptr;
    int method_len     = 0;

    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      method = TSHttpHdrMethodGet(bufp, hdr_loc, &method_len);
      result->assign(method, method_len);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_query.size()) == p_request_query) {
    TSMBuffer bufp    = nullptr;
    TSMLoc hdr_loc    = nullptr;
    TSMLoc url_loc    = nullptr;
    const char *query = nullptr;
    int query_len     = 0;

    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) == TS_SUCCESS) {
        query = TSUrlHttpQueryGet(bufp, url_loc, &query_len);
        result->assign(query, query_len);
        TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      } else {
        *result = pv_empty;
      }
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_referer.size()) == p_request_referer) {
    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;
    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      get_header(bufp, hdr_loc, "Referer", result);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_useragent.size()) == p_request_useragent) {
    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;
    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      get_header(bufp, hdr_loc, "User-Agent", result);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_id.size()) == p_request_id) {
    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;
    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      bool found = get_header(bufp, hdr_loc, "x-request-id", result);
      if (!found) {
        uint64_t id = TSHttpTxnIdGet(txnp_);
        result->assign(std::to_string(id));
      }
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      uint64_t id = TSHttpTxnIdGet(txnp_);
      result->assign(std::to_string(id));
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_headers.size()) == p_request_headers) {
    std::string_view key_sv = path.substr(p_request_headers.size(), path.size() - p_request_headers.size() - 1);

    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;
    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      get_header(bufp, hdr_loc, key_sv, result);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_protocol.size()) == p_request_protocol) {
    if (TSHttpTxnClientProtocolStackContains(txnp_, "h2") != nullptr) {
      *result = pv_http2;
    } else if (TSHttpTxnClientProtocolStackContains(txnp_, "http/1.0") != nullptr) {
      *result = pv_http10;
    } else if (TSHttpTxnClientProtocolStackContains(txnp_, "http/1.1") != nullptr) {
      *result = pv_http11;
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_time.size()) == p_request_time) {
    TSHRTime epoch = 0;
    if (TS_SUCCESS == TSHttpTxnMilestoneGet(txnp_, TS_MILESTONE_SM_START, &epoch)) {
      double timestamp = static_cast<double>(epoch) / 1000000000;
      result->assign(reinterpret_cast<const char *>(&timestamp), sizeof(double));
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_duration.size()) == p_request_duration) {
    TSHRTime value = 0;
    TSHRTime epoch = 0;

    if (TS_SUCCESS == TSHttpTxnMilestoneGet(txnp_, TS_MILESTONE_SM_START, &epoch)) {
      if (TS_SUCCESS == TSHttpTxnMilestoneGet(txnp_, TS_MILESTONE_SM_FINISH, &value)) {
        double duration = static_cast<double>((value - epoch)) / 1000000000;
        result->assign(reinterpret_cast<const char *>(&duration), sizeof(double));
        return WasmResult::Ok;
      }
    }
    *result = pv_empty;
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_size.size()) == p_request_size) {
    int64_t bytes = TSHttpTxnClientReqBodyBytesGet(txnp_);
    result->assign(reinterpret_cast<const char *>(&bytes), sizeof(int64_t));
    return WasmResult::Ok;
  } else if (path.substr(0, p_request_total_size.size()) == p_request_total_size) {
    int h_bytes     = TSHttpTxnClientReqHdrBytesGet(txnp_);
    int64_t b_bytes = TSHttpTxnClientReqBodyBytesGet(txnp_);
    int64_t total   = h_bytes + b_bytes;
    result->assign(reinterpret_cast<const char *>(&total), sizeof(int64_t));
    return WasmResult::Ok;
  } else if (path.substr(0, p_response_code.size()) == p_response_code) {
    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;
    int status     = 0;

    if (TSHttpTxnServerRespGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      status = TSHttpHdrStatusGet(bufp, hdr_loc);
      result->assign(reinterpret_cast<const char *>(&status), sizeof(int));
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_response_code_details.size()) == p_response_code_details) {
    TSMBuffer bufp     = nullptr;
    TSMLoc hdr_loc     = nullptr;
    const char *reason = nullptr;
    int reason_len     = 0;

    if (TSHttpTxnServerRespGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      reason = TSHttpHdrReasonGet(bufp, hdr_loc, &reason_len);
      result->assign(reason, reason_len);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_response_headers.size()) == p_response_headers) {
    std::string_view key_sv = path.substr(p_response_headers.size(), path.size() - p_response_headers.size() - 1);
    TSMBuffer bufp          = nullptr;
    TSMLoc hdr_loc          = nullptr;
    if (TSHttpTxnServerRespGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      get_header(bufp, hdr_loc, key_sv, result);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      *result = pv_empty;
    }
    return WasmResult::Ok;
  } else if (path.substr(0, p_response_size.size()) == p_response_size) {
    int64_t bytes = TSHttpTxnServerRespBodyBytesGet(txnp_);
    result->assign(reinterpret_cast<const char *>(&bytes), sizeof(int64_t));
    return WasmResult::Ok;
  } else if (path.substr(0, p_response_total_size.size()) == p_response_total_size) {
    int h_bytes     = TSHttpTxnServerRespHdrBytesGet(txnp_);
    int64_t b_bytes = TSHttpTxnServerRespBodyBytesGet(txnp_);
    int64_t total   = h_bytes + b_bytes;
    result->assign(reinterpret_cast<const char *>(&total), sizeof(int64_t));
    return WasmResult::Ok;
  } else {
    *result = pv_empty;
    Dbg(dbg_ctl, "[%s] looking for unknown property: empty string", __FUNCTION__);
    return WasmResult::Ok;
  }
}

WasmResult
Context::setProperty(std::string_view key, std::string_view serialized_value)
{
  if (key.substr(0, p_request_url_path.size()) == p_request_url_path) {
    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;
    TSMLoc url_loc = nullptr;
    std::string_view result;

    if (serialized_value.substr(0, 1) == "/") {
      result = serialized_value.substr(1);
    } else {
      result = serialized_value;
    }

    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) == TS_SUCCESS) {
        TSUrlPathSet(bufp, url_loc, result.data(), result.size());
        TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      }
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
    return WasmResult::Ok;
  } else if (key.substr(0, p_request_host.size()) == p_request_host) {
    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;
    TSMLoc url_loc = nullptr;

    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) == TS_SUCCESS) {
        TSUrlHostSet(bufp, url_loc, serialized_value.data(), serialized_value.size());
        TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      }
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
    return WasmResult::Ok;
  } else if (key.substr(0, p_request_scheme.size()) == p_request_scheme) {
    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;
    TSMLoc url_loc = nullptr;

    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) == TS_SUCCESS) {
        TSUrlSchemeSet(bufp, url_loc, serialized_value.data(), serialized_value.size());
        TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      }
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
    return WasmResult::Ok;
  } else if (key.substr(0, p_request_method.size()) == p_request_method) {
    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;

    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      TSHttpHdrMethodSet(bufp, hdr_loc, serialized_value.data(), serialized_value.size());
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
    return WasmResult::Ok;
  } else if (key.substr(0, p_request_query.size()) == p_request_query) {
    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;
    TSMLoc url_loc = nullptr;

    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) == TS_SUCCESS) {
        TSUrlHttpQuerySet(bufp, url_loc, serialized_value.data(), serialized_value.size());
        TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      }
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
    return WasmResult::Ok;
  } else if (key.substr(0, p_request_headers.size()) == p_request_headers) {
    std::string_view key_sv = key.substr(p_request_headers.size(), key.size() - p_request_headers.size() - 1);

    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;

    if (TSHttpTxnClientReqGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      set_header(bufp, hdr_loc, key_sv, serialized_value);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
    return WasmResult::Ok;
  } else if (key.substr(0, p_response_code.size()) == p_response_code) {
    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;

    if (TSHttpTxnServerRespGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      int64_t *status = reinterpret_cast<int64_t *>(const_cast<char *>(serialized_value.data()));
      TSHttpHdrStatusSet(bufp, hdr_loc, static_cast<TSHttpStatus>(*status));
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
    return WasmResult::Ok;
  } else if (key.substr(0, p_response_code_details.size()) == p_response_code_details) {
    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;

    if (TSHttpTxnServerRespGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      TSHttpHdrReasonSet(bufp, hdr_loc, serialized_value.data(), serialized_value.size());
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
    return WasmResult::Ok;
  } else if (key.substr(0, p_response_headers.size()) == p_response_headers) {
    std::string_view key_sv = key.substr(p_response_headers.size(), key.size() - p_response_headers.size() - 1);

    TSMBuffer bufp = nullptr;
    TSMLoc hdr_loc = nullptr;

    if (TSHttpTxnServerRespGet(txnp_, &bufp, &hdr_loc) == TS_SUCCESS) {
      set_header(bufp, hdr_loc, key_sv, serialized_value);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
    return WasmResult::Ok;
  }
  return WasmResult::Ok;
}

WasmResult
Context::continueStream(WasmStreamType /* stream_type */)
{
  if (reenable_txn_) {
    TSError("[wasm][%s] transaction already reenabled", __FUNCTION__);
    return WasmResult::Ok;
  }

  if (txnp_ == nullptr) {
    TSError("[wasm][%s] Can't continue stream without a transaction", __FUNCTION__);
    return WasmResult::InternalFailure;
  } else {
    Dbg(dbg_ctl, "[%s] continuing txn for context %d", __FUNCTION__, id());
    reenable_txn_ = true;
    TSHttpTxnReenable(txnp_, TS_EVENT_HTTP_CONTINUE);
    return WasmResult::Ok;
  }
}

WasmResult
Context::closeStream(WasmStreamType /* stream_type */)
{
  if (reenable_txn_) {
    TSError("[wasm][%s] transaction already reenabled", __FUNCTION__);
    return WasmResult::Ok;
  }

  if (txnp_ == nullptr) {
    TSError("[wasm][%s] Can't continue stream without a transaction", __FUNCTION__);
    return WasmResult::InternalFailure;
  } else {
    Dbg(dbg_ctl, "[%s] continue txn for context %d with error", __FUNCTION__, id());
    reenable_txn_ = true;
    TSHttpTxnReenable(txnp_, TS_EVENT_HTTP_ERROR);
    return WasmResult::Ok;
  }
}

// send pre-made response
WasmResult
Context::sendLocalResponse(uint32_t response_code, std::string_view body_text, Pairs additional_headers,
                           GrpcStatusCode /* grpc_status */, std::string_view details)
{
  if (txnp_ == nullptr) {
    TSError("[wasm][%s] Can't send local response without a transaction", __FUNCTION__);
    return WasmResult::InternalFailure;
  } else {
    TSHttpTxnStatusSet(txnp_, static_cast<TSHttpStatus>(response_code));

    if (body_text.size() > 0) {
      TSHttpTxnErrorBodySet(txnp_, TSstrndup(body_text.data(), body_text.size()), body_text.size(),
                            nullptr); // Defaults to text/html
    }

    local_reply_headers_ = additional_headers;
    local_reply_details_ = details;
    local_reply_         = true;
  }
  return WasmResult::Ok;
}

WasmResult
Context::getSharedData(std::string_view key, std::pair<std::string, uint32_t> *data)
{
  return proxy_wasm::getGlobalSharedData().get(wasm_->vm_id(), key, data);
}

// Header/Trailer/Metadata Maps
WasmResult
Context::addHeaderMapValue(WasmHeaderMapType type, std::string_view key, std::string_view value)
{
  auto map = getHeaderMap(type);
  if (map.bufp == nullptr) {
    TSError("[wasm][%s] Invalid type", __FUNCTION__);
    return WasmResult::BadArgument;
  }
  auto *field_loc = TSMimeHdrFieldFind(map.bufp, map.hdr_loc, key.data(), static_cast<int>(key.size()));
  if (TS_NULL_MLOC == field_loc) {
    if (TS_SUCCESS != TSMimeHdrFieldCreateNamed(map.bufp, map.hdr_loc, key.data(), static_cast<int>(key.size()), &field_loc)) {
      TSError("[wasm][%s] Cannot create named field", __FUNCTION__);
      return WasmResult::InternalFailure;
    }
  }
  if (TS_SUCCESS ==
      TSMimeHdrFieldValueStringSet(map.bufp, map.hdr_loc, field_loc, -1, value.data(), static_cast<int>(value.size()))) {
    TSMimeHdrFieldAppend(map.bufp, map.hdr_loc, field_loc);
    TSHandleMLocRelease(map.bufp, map.hdr_loc, field_loc);
    return WasmResult::Ok;
  } else {
    TSError("[wasm][%s] Cannot set field value", __FUNCTION__);
    TSHandleMLocRelease(map.bufp, map.hdr_loc, field_loc);
    return WasmResult::InternalFailure;
  }
}

WasmResult
Context::getHeaderMapValue(WasmHeaderMapType type, std::string_view key, std::string_view *result)
{
  auto map = getHeaderMap(type);
  if (map.bufp == nullptr) {
    TSError("[wasm][%s] Invalid type", __FUNCTION__);
    return WasmResult::BadArgument;
  }
  if (map.bufp != nullptr) {
    auto *loc = TSMimeHdrFieldFind(map.bufp, map.hdr_loc, key.data(), static_cast<int>(key.size()));
    if (TS_NULL_MLOC != loc) {
      int vlen = 0;
      // TODO: add support for dups
      auto *v = TSMimeHdrFieldValueStringGet(map.bufp, map.hdr_loc, loc, 0, &vlen);
      std::string_view temp(v, vlen);
      *result = temp;
      TSHandleMLocRelease(map.bufp, map.hdr_loc, loc);
      return WasmResult::Ok;
    } else {
      *result = "";
      return WasmResult::Ok;
    }
  }
  return WasmResult::NotFound;
}

WasmResult
Context::getHeaderMapPairs(WasmHeaderMapType type, Pairs *result)
{
  auto map = getHeaderMap(type);
  if (map.bufp == nullptr) {
    TSError("[wasm][%s] Invalid type", __FUNCTION__);
    return WasmResult::BadArgument;
  }
  int num = map.size();

  result->reserve(num);
  for (int i = 0; i < num; i++) {
    auto *loc = TSMimeHdrFieldGet(map.bufp, map.hdr_loc, i);
    int nlen  = 0;
    auto *n   = TSMimeHdrFieldNameGet(map.bufp, map.hdr_loc, loc, &nlen);
    int vlen  = 0;
    // TODO: add support for dups.
    auto *v = TSMimeHdrFieldValueStringGet(map.bufp, map.hdr_loc, loc, 0, &vlen);
    result->push_back(
      std::make_pair(std::string_view(n, static_cast<size_t>(nlen)), std::string_view(v, static_cast<size_t>(vlen))));
    TSHandleMLocRelease(map.bufp, map.hdr_loc, loc);
  }
  return WasmResult::Ok;
}

WasmResult
Context::setHeaderMapPairs(WasmHeaderMapType type, const Pairs &pairs)
{
  auto map = getHeaderMap(type);
  if (map.bufp == nullptr) {
    TSError("[wasm][%s] Invalid type", __FUNCTION__);
    return WasmResult::BadArgument;
  }

  for (const auto &p : pairs) {
    std::string key(p.first);
    std::string value(p.second);

    auto *loc = TSMimeHdrFieldFind(map.bufp, map.hdr_loc, key.data(), static_cast<int>(key.size()));
    if (loc != TS_NULL_MLOC) {
      int first = 1;
      while (loc != TS_NULL_MLOC) {
        auto *tmp = TSMimeHdrFieldNextDup(map.bufp, map.hdr_loc, loc);
        if (first != 0) {
          first = 0;
          TSMimeHdrFieldValueStringSet(map.bufp, map.hdr_loc, loc, -1, value.data(), static_cast<int>(value.size()));
        } else {
          TSMimeHdrFieldDestroy(map.bufp, map.hdr_loc, loc);
        }
        TSHandleMLocRelease(map.bufp, map.hdr_loc, loc);
        loc = tmp;
      }
    } else if (TSMimeHdrFieldCreateNamed(map.bufp, map.hdr_loc, key.data(), static_cast<int>(key.size()), &loc) != TS_SUCCESS) {
      TSError("[wasm][%s] TSMimeHdrFieldCreateNamed error", __FUNCTION__);
      return WasmResult::InternalFailure;
    } else {
      TSMimeHdrFieldValueStringSet(map.bufp, map.hdr_loc, loc, -1, value.data(), static_cast<int>(value.size()));
      TSMimeHdrFieldAppend(map.bufp, map.hdr_loc, loc);
    }

    if (loc != TS_NULL_MLOC) {
      TSHandleMLocRelease(map.bufp, map.hdr_loc, loc);
    }
  }

  return WasmResult::Ok;
}

WasmResult
Context::removeHeaderMapValue(WasmHeaderMapType type, std::string_view key)
{
  auto map = getHeaderMap(type);
  if (map.bufp == nullptr) {
    TSError("[wasm][%s] Invalid type", __FUNCTION__);
    return WasmResult::BadArgument;
  }
  if (map.bufp != nullptr) {
    auto *loc = TSMimeHdrFieldFind(map.bufp, map.hdr_loc, key.data(), static_cast<int>(key.size()));
    while (loc != TS_NULL_MLOC) {
      auto *tmp = TSMimeHdrFieldNextDup(map.bufp, map.hdr_loc, loc);
      TSMimeHdrFieldDestroy(map.bufp, map.hdr_loc, loc);
      TSHandleMLocRelease(map.bufp, map.hdr_loc, loc);
      loc = tmp;
    }
  }

  return WasmResult::Ok;
}

WasmResult
Context::replaceHeaderMapValue(WasmHeaderMapType type, std::string_view key, std::string_view value)
{
  auto map = getHeaderMap(type);
  if (map.bufp == nullptr) {
    TSError("[wasm][%s] Invalid type", __FUNCTION__);
    return WasmResult::BadArgument;
  }
  if (map.bufp != nullptr) {
    auto *loc = TSMimeHdrFieldFind(map.bufp, map.hdr_loc, key.data(), static_cast<int>(key.size()));

    if (loc != TS_NULL_MLOC) {
      int first = 1;
      while (loc != TS_NULL_MLOC) {
        auto *tmp = TSMimeHdrFieldNextDup(map.bufp, map.hdr_loc, loc);
        if (first != 0) {
          first = 0;
          TSMimeHdrFieldValueStringSet(map.bufp, map.hdr_loc, loc, -1, value.data(), static_cast<int>(value.size()));
        } else {
          TSMimeHdrFieldDestroy(map.bufp, map.hdr_loc, loc);
        }
        TSHandleMLocRelease(map.bufp, map.hdr_loc, loc);
        loc = tmp;
      }
    }
  }
  return WasmResult::Ok;
}

WasmResult
Context::getHeaderMapSize(WasmHeaderMapType type, uint32_t *result)
{
  auto map = getHeaderMap(type);
  if (map.bufp == nullptr) {
    TSError("[wasm][%s] Invalid type", __FUNCTION__);
    return WasmResult::BadArgument;
  }
  *result = TSMimeHdrLengthGet(map.bufp, map.hdr_loc);
  return WasmResult::Ok;
}

HeaderMap
Context::getHeaderMap(WasmHeaderMapType type)
{
  HeaderMap map;
  switch (type) {
  case WasmHeaderMapType::RequestHeaders:
    if (txnp_ == nullptr) {
      return {};
    }
    if (TSHttpTxnClientReqGet(txnp_, &map.bufp, &map.hdr_loc) == TS_SUCCESS) {
      return map;
    }
    return {};
  case WasmHeaderMapType::RequestTrailers:
    return {};
  case WasmHeaderMapType::ResponseHeaders:
    if (txnp_ == nullptr) {
      return {};
    }
    if (TSHttpTxnServerRespGet(txnp_, &map.bufp, &map.hdr_loc) == TS_SUCCESS) {
      return map;
    }
    return {};
  case WasmHeaderMapType::ResponseTrailers:
    return {};
  case WasmHeaderMapType::HttpCallResponseHeaders:
    if (cr_hdr_buf_ == nullptr || cr_hdr_loc_ == nullptr) {
      return {};
    }
    map.bufp    = cr_hdr_buf_;
    map.hdr_loc = cr_hdr_loc_;
    return map;
  default:
  case WasmHeaderMapType::GrpcReceiveTrailingMetadata:
  case WasmHeaderMapType::GrpcReceiveInitialMetadata:
  case WasmHeaderMapType::HttpCallResponseTrailers:
    return {};
  }
}

} // namespace ats_wasm
