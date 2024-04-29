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

#pragma once

#include <atomic>
#include <chrono>
#include <ctime>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include "include/proxy-wasm/context.h"

#include "ts/ts.h"

#define WASM_DEBUG_TAG "wasm"

namespace ats_wasm
{
const unsigned int LOCAL_IP_ADDRESS    = 0x0100007f;
const int          LOCAL_PORT          = 8080;
const int          FETCH_EVENT_ID_BASE = 10000;

using proxy_wasm::BufferBase;
using proxy_wasm::BufferInterface;
using proxy_wasm::ContextBase;
using proxy_wasm::GrpcStatusCode;
using proxy_wasm::LogLevel;
using proxy_wasm::MetricType;
using proxy_wasm::Pairs;
using proxy_wasm::PluginBase;
using proxy_wasm::WasmBase;
using proxy_wasm::WasmBufferType;
using proxy_wasm::WasmHeaderMapType;
using proxy_wasm::WasmResult;
using proxy_wasm::WasmStreamType;

class Wasm;

// constants for property names
constexpr std::string_view p_request_path                         = {"request\0path", 12};
constexpr std::string_view p_request_url_path                     = {"request\0url_path", 16};
constexpr std::string_view p_request_host                         = {"request\0host", 12};
constexpr std::string_view p_request_scheme                       = {"request\0scheme", 14};
constexpr std::string_view p_request_method                       = {"request\0method", 14};
constexpr std::string_view p_request_headers                      = {"request\0headers", 15};
constexpr std::string_view p_request_referer                      = {"request\0referer", 15};
constexpr std::string_view p_request_useragent                    = {"request\0useragent", 17};
constexpr std::string_view p_request_time                         = {"request\0time", 12};
constexpr std::string_view p_request_id                           = {"request\0id", 10};
constexpr std::string_view p_request_protocol                     = {"request\0protocol", 16};
constexpr std::string_view p_request_query                        = {"request\0query", 13};
constexpr std::string_view p_request_duration                     = {"request\0duration", 16};
constexpr std::string_view p_request_size                         = {"request\0size", 12};
constexpr std::string_view p_request_total_size                   = {"request\0total_size", 18};
constexpr std::string_view p_response_code                        = {"response\0code", 13};
constexpr std::string_view p_response_code_details                = {"response\0code_details", 21};
constexpr std::string_view p_response_headers                     = {"response\0headers", 16};
constexpr std::string_view p_response_size                        = {"response\0size", 13};
constexpr std::string_view p_response_total_size                  = {"response\0total_size", 19};
constexpr std::string_view p_node                                 = {"node", 4};
constexpr std::string_view p_plugin_name                          = {"plugin_name", 11};
constexpr std::string_view p_plugin_root_id                       = {"plugin_root_id", 14};
constexpr std::string_view p_plugin_vm_id                         = {"plugin_vm_id", 12};
constexpr std::string_view p_source_address                       = {"source\0address", 14};
constexpr std::string_view p_source_port                          = {"source\0port", 11};
constexpr std::string_view p_destination_address                  = {"destination\0address", 19};
constexpr std::string_view p_destination_port                     = {"destination\0port", 16};
constexpr std::string_view p_connection_mtls                      = {"connection\0mtls", 15};
constexpr std::string_view p_connection_requested_server_name     = {"connection\0requested_server_name", 32};
constexpr std::string_view p_connection_tls_version               = {"connection\0tls_version", 22};
constexpr std::string_view p_connection_subject_local_certificate = {"connection\0subject_local_certificate", 36};
constexpr std::string_view p_connection_subject_peer_certificate  = {"connection\0subject_peer_certificate", 35};
constexpr std::string_view p_connection_dns_san_local_certificate = {"connection\0dns_san_local_certificate", 36};
constexpr std::string_view p_connection_dns_san_peer_certificate  = {"connection\0dns_san_peer_certificate", 35};
constexpr std::string_view p_connection_uri_san_local_certificate = {"connection\0uri_san_local_certificate", 36};
constexpr std::string_view p_connection_uri_san_peer_certificate  = {"connection\0uri_san_peer_certificate", 35};
constexpr std::string_view p_upstream_address                     = {"upstream\0address", 16};
constexpr std::string_view p_upstream_port                        = {"upstream\0port", 13};
constexpr std::string_view p_upstream_local_address               = {"upstream\0local_address", 22};
constexpr std::string_view p_upstream_local_port                  = {"upstream\0local_port", 19};
constexpr std::string_view p_upstream_tls_version                 = {"upstream\0tls_version", 20};
constexpr std::string_view p_upstream_subject_local_certificate   = {"upstream\0subject_local_certificate", 35};
constexpr std::string_view p_upstream_subject_peer_certificate    = {"upstream\0subject_peer_certificate", 34};
constexpr std::string_view p_upstream_dns_san_local_certificate   = {"upstream\0dns_san_local_certificate", 35};
constexpr std::string_view p_upstream_dns_san_peer_certificate    = {"upstream\0dns_san_peer_certificate", 34};
constexpr std::string_view p_upstream_uri_san_local_certificate   = {"upstream\0uri_san_local_certificate", 35};
constexpr std::string_view p_upstream_uri_san_peer_certificate    = {"upstream\0uri_san_peer_certificate", 34};

// constants for property values
constexpr std::string_view pv_http2  = {"HTTP/2", 6};
constexpr std::string_view pv_http10 = {"HTTP/1.0", 8};
constexpr std::string_view pv_http11 = {"HTTP/1.1", 8};
constexpr std::string_view pv_empty  = {"", 0};

extern DbgCtl dbg_ctl;

// local struct representing the transaction header
struct HeaderMap {
  TSMBuffer bufp{nullptr};
  TSMLoc    hdr_loc{nullptr};

  ~HeaderMap()
  {
    if (bufp != nullptr) {
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    }
  }

  int
  size() const
  {
    if (bufp != nullptr) {
      return TSMimeHdrFieldsCount(bufp, hdr_loc);
    }
    return 0;
  }
};

// extended BufferBase
class Buffer : public BufferBase
{
public:
  Buffer() { owned_data_str_ = ""; }
  ~Buffer() override = default;

  size_t
  size() const override
  {
    if (owned_data_str_ != "") {
      return owned_data_str_.size();
    }
    return BufferBase::size();
  }

  WasmResult copyTo(WasmBase *wasm, size_t start, size_t length, uint64_t ptr_ptr, uint64_t size_ptr) const override;

  WasmResult
  copyFrom(size_t start, size_t length, std::string_view data) override
  {
    owned_data_str_.replace(start, length, data);
    return WasmResult::Ok;
  }

  void
  clear() override
  {
    owned_data_str_ = "";
    BufferBase::clear();
  }

  BufferBase *
  set(std::string data)
  {
    owned_data_str_ = owned_data_str_ + data;
    return this;
  }

  std::string
  get()
  {
    return owned_data_str_;
  }

private:
  std::string owned_data_str_;
};

class Context : public ContextBase
{
public:
  // constructors for the extend class
  Context();
  Context(Wasm *wasm);
  Context(Wasm *wasm, const std::shared_ptr<PluginBase> &plugin);
  Context(Wasm *wasm, uint32_t parent_context_id, const std::shared_ptr<PluginBase> &plugin);

  // extend class utility functions
  Wasm    *wasm() const;
  Context *parent_context() const;
  Context *root_context() const;

  void initialize(TSHttpTxn txnp);
  void initialize(TSCont cont);

  TSHttpTxn txnp();
  TSCont    scheduler_cont();

  void error(std::string_view message) override;

  // local reply handler
  void onLocalReply();

  //
  // General Callbacks.
  //
  WasmResult log(uint32_t level, std::string_view message) override;

  uint64_t getCurrentTimeNanoseconds() override;

  uint64_t getMonotonicTimeNanoseconds() override;

  std::string_view getConfiguration() override;

  WasmResult setTimerPeriod(std::chrono::milliseconds period, uint32_t *timer_token_ptr) override;

  BufferInterface *getBuffer(WasmBufferType type) override;

  WasmResult httpCall(std::string_view target, const Pairs &request_headers, std::string_view request_body,
                      const Pairs &request_trailers, int timeout_millisconds, uint32_t *token_ptr) override;

  // Call result functions
  void
  setHttpCallResult(TSMBuffer buf, TSMLoc loc, const void *body, size_t size, TSEvent result)
  {
    cr_hdr_buf_   = buf;
    cr_hdr_loc_   = loc;
    cr_body_      = body;
    cr_body_size_ = size;
    cr_result_    = result;
  }

  void
  resetHttpCallResult()
  {
    cr_hdr_buf_   = nullptr;
    cr_hdr_loc_   = nullptr;
    cr_body_      = nullptr;
    cr_body_size_ = 0;
    cr_result_    = static_cast<TSEvent>(FETCH_EVENT_ID_BASE + 1);
  }

  // transform result functions
  void
  clearTransformResult()
  {
    transform_result_.clear();
  }

  void
  setTransformResult(const char *body, size_t body_size)
  {
    if (body == nullptr || body_size == 0) {
      std::string s("");
      transform_result_.set(s);
    } else {
      std::string s(body, body_size);
      transform_result_.set(s);
    }
  }

  const char *
  getTransformResult(size_t *body_size)
  {
    std::string s = transform_result_.get();
    *body_size    = s.size();
    return s.c_str();
  }

  // Metrics
  WasmResult defineMetric(uint32_t metric_type, std::string_view name, uint32_t *metric_id_ptr) override;
  WasmResult incrementMetric(uint32_t metric_id, int64_t offset) override;
  WasmResult recordMetric(uint32_t metric_id, uint64_t value) override;
  WasmResult getMetric(uint32_t metric_id, uint64_t *value_ptr) override;

  // Properties
  WasmResult getProperty(std::string_view path, std::string *result) override;

  WasmResult setProperty(std::string_view key, std::string_view serialized_value) override;

  // send a premade response
  WasmResult continueStream(WasmStreamType stream_type) override;
  WasmResult closeStream(WasmStreamType stream_type) override;
  WasmResult sendLocalResponse(uint32_t response_code, std::string_view body_text, Pairs additional_headers,
                               GrpcStatusCode /* grpc_status */, std::string_view details) override;

  // check stream
  bool
  isTxnReenable()
  {
    return reenable_txn_;
  }
  void
  setTxnReenable()
  {
    reenable_txn_ = true;
  }
  void
  resetTxnReenable()
  {
    reenable_txn_ = false;
  }
  bool
  isLocalReply()
  {
    return local_reply_;
  }

  WasmResult getSharedData(std::string_view key, std::pair<std::string, uint32_t /* cas */> *data) override;

  // Header/Trailer/Metadata Maps
  WasmResult addHeaderMapValue(WasmHeaderMapType type, std::string_view key, std::string_view value) override;
  WasmResult getHeaderMapValue(WasmHeaderMapType type, std::string_view key, std::string_view *result) override;
  WasmResult getHeaderMapPairs(WasmHeaderMapType type, Pairs *result) override;
  WasmResult setHeaderMapPairs(WasmHeaderMapType type, const Pairs &pairs) override;
  WasmResult removeHeaderMapValue(WasmHeaderMapType type, std::string_view key) override;
  WasmResult replaceHeaderMapValue(WasmHeaderMapType type, std::string_view key, std::string_view value) override;
  WasmResult getHeaderMapSize(WasmHeaderMapType type, uint32_t *result) override;

protected:
  friend class Wasm;

private:
  HeaderMap getHeaderMap(WasmHeaderMapType type);

  TSHttpTxn txnp_{nullptr};
  TSCont    scheduler_cont_{nullptr};

  // continue/close stream?
  bool reenable_txn_ = false;

  // local reply
  Pairs       local_reply_headers_{};
  std::string local_reply_details_ = "";
  bool        local_reply_         = false;

  // buffer for result (don't set to null as default)
  BufferBase buffer_;

  // Call result
  TSEvent     cr_result_    = static_cast<TSEvent>(FETCH_EVENT_ID_BASE + 1);
  const void *cr_body_      = nullptr;
  size_t      cr_body_size_ = 0;
  TSMBuffer   cr_hdr_buf_   = nullptr;
  TSMLoc      cr_hdr_loc_   = nullptr;

  // transform result
  Buffer transform_result_;
};

// local struct representing info for async transaction
struct AsyncInfo {
  uint32_t token;
  Context *root_context;
};

// local struct representing info for transform
struct TransformInfo {
  TSVIO            output_vio;
  TSIOBuffer       output_buffer;
  TSIOBufferReader output_reader;

  TSVIO            reserved_vio;
  TSIOBuffer       reserved_buffer;
  TSIOBufferReader reserved_reader;

  int64_t upstream_bytes;
  int64_t downstream_bytes;
  int64_t total;

  Context *context;

  bool request;
};

} // namespace ats_wasm
