// Copyright 2016-2019 Envoy Project Authors
// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "include/proxy-wasm/context.h"
#include "include/proxy-wasm/wasm.h"
#include "src/shared_data.h"
#include "src/shared_queue.h"

#define CHECK_FAIL(_stream_type, _stream_type2, _return_open, _return_closed)                      \
  if (isFailed()) {                                                                                \
    if (plugin_->fail_open_) {                                                                     \
      return _return_open;                                                                         \
    }                                                                                              \
    if (!stream_failed_) {                                                                         \
      failStream(_stream_type);                                                                    \
      failStream(_stream_type2);                                                                   \
      stream_failed_ = true;                                                                       \
    }                                                                                              \
    return _return_closed;                                                                         \
  }

#define CHECK_FAIL_HTTP(_return_open, _return_closed)                                              \
  CHECK_FAIL(WasmStreamType::Request, WasmStreamType::Response, _return_open, _return_closed)
#define CHECK_FAIL_NET(_return_open, _return_closed)                                               \
  CHECK_FAIL(WasmStreamType::Downstream, WasmStreamType::Upstream, _return_open, _return_closed)

namespace proxy_wasm {

DeferAfterCallActions::~DeferAfterCallActions() {
  wasm_->stopNextIteration(false);
  wasm_->doAfterVmCallActions();
}

WasmResult BufferBase::copyTo(WasmBase *wasm, size_t start, size_t length, uint64_t ptr_ptr,
                              uint64_t size_ptr) const {
  if (owned_data_) {
    std::string_view s(owned_data_.get() + start, length);
    if (!wasm->copyToPointerSize(s, ptr_ptr, size_ptr)) {
      return WasmResult::InvalidMemoryAccess;
    }
    return WasmResult::Ok;
  }
  std::string_view s = data_.substr(start, length);
  if (!wasm->copyToPointerSize(s, ptr_ptr, size_ptr)) {
    return WasmResult::InvalidMemoryAccess;
  }
  return WasmResult::Ok;
}

// Test support.
uint32_t resolveQueueForTest(std::string_view vm_id, std::string_view queue_name) {
  return getGlobalSharedQueue().resolveQueue(vm_id, queue_name);
}

std::string PluginBase::makeLogPrefix() const {
  std::string prefix;
  if (!name_.empty()) {
    prefix = prefix + " " + name_;
  }
  if (!root_id_.empty()) {
    prefix = prefix + " " + std::string(root_id_);
  }
  if (!vm_id_.empty()) {
    prefix = prefix + " " + std::string(vm_id_);
  }
  return prefix;
}

ContextBase::ContextBase() : parent_context_(this) {}

ContextBase::ContextBase(WasmBase *wasm) : wasm_(wasm), parent_context_(this) {
  wasm_->contexts_[id_] = this;
}

ContextBase::ContextBase(WasmBase *wasm, const std::shared_ptr<PluginBase> &plugin)
    : wasm_(wasm), id_(wasm->allocContextId()), parent_context_(this), root_id_(plugin->root_id_),
      root_log_prefix_(makeRootLogPrefix(plugin->vm_id_)), plugin_(plugin) {
  wasm_->contexts_[id_] = this;
}

// NB: wasm can be nullptr if it failed to be created successfully.
ContextBase::ContextBase(WasmBase *wasm, uint32_t parent_context_id,
                         const std::shared_ptr<PluginHandleBase> &plugin_handle)
    : wasm_(wasm), id_(wasm != nullptr ? wasm->allocContextId() : 0),
      parent_context_id_(parent_context_id), plugin_(plugin_handle->plugin()),
      plugin_handle_(plugin_handle) {
  if (wasm_ != nullptr) {
    wasm_->contexts_[id_] = this;
    parent_context_ = wasm_->contexts_[parent_context_id_];
  }
}

WasmVm *ContextBase::wasmVm() const { return wasm_->wasm_vm(); }

bool ContextBase::isFailed() { return (wasm_ == nullptr || wasm_->isFailed()); }

std::string ContextBase::makeRootLogPrefix(std::string_view vm_id) const {
  std::string prefix;
  if (!root_id_.empty()) {
    prefix = prefix + " " + std::string(root_id_);
  }
  if (!vm_id.empty()) {
    prefix = prefix + " " + std::string(vm_id);
  }
  return prefix;
}

//
// Calls into the WASM code.
//
bool ContextBase::onStart(std::shared_ptr<PluginBase> plugin) {
  DeferAfterCallActions actions(this);
  bool result = true;
  if (wasm_->on_context_create_) {
    temp_plugin_ = plugin;
    wasm_->on_context_create_(this, id_, 0);
    in_vm_context_created_ = true;
    temp_plugin_.reset();
  }
  if (wasm_->on_vm_start_) {
    // Do not set plugin_ as the on_vm_start handler should be independent of the plugin since the
    // specific plugin which ends up calling it is not necessarily known by the Wasm module.
    result =
        wasm_->on_vm_start_(this, id_, static_cast<uint32_t>(wasm()->vm_configuration().size()))
            .u64_ != 0;
  }
  return result;
}

bool ContextBase::onConfigure(std::shared_ptr<PluginBase> plugin) {
  if (isFailed()) {
    return true;
  }

  // on_context_create is yet to be executed for all the root contexts except the first one
  if (!in_vm_context_created_ && wasm_->on_context_create_) {
    DeferAfterCallActions actions(this);
    wasm_->on_context_create_(this, id_, 0);
  }

  // NB: If no on_context_create function is registered the in-VM SDK is responsible for
  // managing any required in-VM state.
  in_vm_context_created_ = true;

  if (!wasm_->on_configure_) {
    return true;
  }

  DeferAfterCallActions actions(this);
  temp_plugin_ = plugin;
  auto result =
      wasm_->on_configure_(this, id_, static_cast<uint32_t>(plugin->plugin_configuration_.size()))
          .u64_ != 0;
  temp_plugin_.reset();
  return result;
}

void ContextBase::onCreate() {
  if (!isFailed() && !in_vm_context_created_ && wasm_->on_context_create_) {
    DeferAfterCallActions actions(this);
    wasm_->on_context_create_(this, id_, parent_context_ != nullptr ? parent_context()->id() : 0);
  }
  // NB: If no on_context_create function is registered the in-VM SDK is responsible for
  // managing any required in-VM state.
  in_vm_context_created_ = true;
}

// Shared Data
WasmResult ContextBase::getSharedData(std::string_view key,
                                      std::pair<std::string, uint32_t> *data) {
  return getGlobalSharedData().get(wasm_->vm_id(), key, data);
}

WasmResult ContextBase::setSharedData(std::string_view key, std::string_view value, uint32_t cas) {
  return getGlobalSharedData().set(wasm_->vm_id(), key, value, cas);
}

WasmResult ContextBase::getSharedDataKeys(std::vector<std::string> *result) {
  return getGlobalSharedData().keys(wasm_->vm_id(), result);
}

WasmResult ContextBase::removeSharedDataKey(std::string_view key, uint32_t cas,
                                            std::pair<std::string, uint32_t> *result) {
  return getGlobalSharedData().remove(wasm_->vm_id(), key, cas, result);
}

// Shared Queue

WasmResult ContextBase::registerSharedQueue(std::string_view queue_name,
                                            SharedQueueDequeueToken *token_ptr) {
  // Get the id of the root context if this is a stream context because onQueueReady is on the
  // root.
  *token_ptr = getGlobalSharedQueue().registerQueue(wasm_->vm_id(), queue_name,
                                                    isRootContext() ? id_ : parent_context_id_,
                                                    wasm_->callOnThreadFunction(), wasm_->vm_key());
  return WasmResult::Ok;
}

WasmResult ContextBase::lookupSharedQueue(std::string_view vm_id, std::string_view queue_name,
                                          SharedQueueDequeueToken *token_ptr) {
  SharedQueueDequeueToken token =
      getGlobalSharedQueue().resolveQueue(vm_id.empty() ? wasm_->vm_id() : vm_id, queue_name);
  if (isFailed() || token == 0U) {
    return WasmResult::NotFound;
  }
  *token_ptr = token;
  return WasmResult::Ok;
}

WasmResult ContextBase::dequeueSharedQueue(uint32_t token, std::string *data) {
  return getGlobalSharedQueue().dequeue(token, data);
}

WasmResult ContextBase::enqueueSharedQueue(uint32_t token, std::string_view value) {
  return getGlobalSharedQueue().enqueue(token, value);
}
void ContextBase::destroy() {
  if (destroyed_) {
    return;
  }
  destroyed_ = true;
  onDone();
}

void ContextBase::onTick(uint32_t /*token*/) {
  if (!isFailed() && wasm_->on_tick_) {
    DeferAfterCallActions actions(this);
    wasm_->on_tick_(this, id_);
  }
}

void ContextBase::onForeignFunction(uint32_t foreign_function_id, uint32_t data_size) {
  if (wasm_->on_foreign_function_) {
    DeferAfterCallActions actions(this);
    wasm_->on_foreign_function_(this, id_, foreign_function_id, data_size);
  }
}

FilterStatus ContextBase::onNetworkNewConnection() {
  CHECK_FAIL_NET(FilterStatus::Continue, FilterStatus::StopIteration);
  if (!wasm_->on_new_connection_) {
    return FilterStatus::Continue;
  }
  DeferAfterCallActions actions(this);
  const auto result = wasm_->on_new_connection_(this, id_);
  CHECK_FAIL_NET(FilterStatus::Continue, FilterStatus::StopIteration);
  return result == 0 ? FilterStatus::Continue : FilterStatus::StopIteration;
}

FilterStatus ContextBase::onDownstreamData(uint32_t data_length, bool end_of_stream) {
  CHECK_FAIL_NET(FilterStatus::Continue, FilterStatus::StopIteration);
  if (!wasm_->on_downstream_data_) {
    return FilterStatus::Continue;
  }
  DeferAfterCallActions actions(this);
  auto result = wasm_->on_downstream_data_(this, id_, static_cast<uint32_t>(data_length),
                                           static_cast<uint32_t>(end_of_stream));
  // TODO(PiotrSikora): pull Proxy-WASM's FilterStatus values.
  CHECK_FAIL_NET(FilterStatus::Continue, FilterStatus::StopIteration);
  return result == 0 ? FilterStatus::Continue : FilterStatus::StopIteration;
}

FilterStatus ContextBase::onUpstreamData(uint32_t data_length, bool end_of_stream) {
  CHECK_FAIL_NET(FilterStatus::Continue, FilterStatus::StopIteration);
  if (!wasm_->on_upstream_data_) {
    return FilterStatus::Continue;
  }
  DeferAfterCallActions actions(this);
  auto result = wasm_->on_upstream_data_(this, id_, static_cast<uint32_t>(data_length),
                                         static_cast<uint32_t>(end_of_stream));
  // TODO(PiotrSikora): pull Proxy-WASM's FilterStatus values.
  CHECK_FAIL_NET(FilterStatus::Continue, FilterStatus::StopIteration);
  return result == 0 ? FilterStatus::Continue : FilterStatus::StopIteration;
}

void ContextBase::onDownstreamConnectionClose(CloseType close_type) {
  if (!isFailed() && wasm_->on_downstream_connection_close_) {
    DeferAfterCallActions actions(this);
    wasm_->on_downstream_connection_close_(this, id_, static_cast<uint32_t>(close_type));
  }
}

void ContextBase::onUpstreamConnectionClose(CloseType close_type) {
  if (!isFailed() && wasm_->on_upstream_connection_close_) {
    DeferAfterCallActions actions(this);
    wasm_->on_upstream_connection_close_(this, id_, static_cast<uint32_t>(close_type));
  }
}

// Empty headers/trailers have zero size.
template <typename P> static uint32_t headerSize(const P &p) { return p ? p->size() : 0; }

FilterHeadersStatus ContextBase::onRequestHeaders(uint32_t headers, bool end_of_stream) {
  CHECK_FAIL_HTTP(FilterHeadersStatus::Continue, FilterHeadersStatus::StopAllIterationAndWatermark);
  if (!wasm_->on_request_headers_abi_01_ && !wasm_->on_request_headers_abi_02_) {
    return FilterHeadersStatus::Continue;
  }
  DeferAfterCallActions actions(this);
  const auto result = wasm_->on_request_headers_abi_01_
                          ? wasm_->on_request_headers_abi_01_(this, id_, headers)
                          : wasm_->on_request_headers_abi_02_(this, id_, headers,
                                                              static_cast<uint32_t>(end_of_stream));
  CHECK_FAIL_HTTP(FilterHeadersStatus::Continue, FilterHeadersStatus::StopAllIterationAndWatermark);
  return convertVmCallResultToFilterHeadersStatus(result);
}

FilterDataStatus ContextBase::onRequestBody(uint32_t body_length, bool end_of_stream) {
  CHECK_FAIL_HTTP(FilterDataStatus::Continue, FilterDataStatus::StopIterationNoBuffer);
  if (!wasm_->on_request_body_) {
    return FilterDataStatus::Continue;
  }
  DeferAfterCallActions actions(this);
  const auto result =
      wasm_->on_request_body_(this, id_, body_length, static_cast<uint32_t>(end_of_stream));
  CHECK_FAIL_HTTP(FilterDataStatus::Continue, FilterDataStatus::StopIterationNoBuffer);
  return convertVmCallResultToFilterDataStatus(result);
}

FilterTrailersStatus ContextBase::onRequestTrailers(uint32_t trailers) {
  CHECK_FAIL_HTTP(FilterTrailersStatus::Continue, FilterTrailersStatus::StopIteration);
  if (!wasm_->on_request_trailers_) {
    return FilterTrailersStatus::Continue;
  }
  DeferAfterCallActions actions(this);
  const auto result = wasm_->on_request_trailers_(this, id_, trailers);
  CHECK_FAIL_HTTP(FilterTrailersStatus::Continue, FilterTrailersStatus::StopIteration);
  return convertVmCallResultToFilterTrailersStatus(result);
}

FilterMetadataStatus ContextBase::onRequestMetadata(uint32_t elements) {
  CHECK_FAIL_HTTP(FilterMetadataStatus::Continue, FilterMetadataStatus::Continue);
  if (!wasm_->on_request_metadata_) {
    return FilterMetadataStatus::Continue;
  }
  DeferAfterCallActions actions(this);
  const auto result = wasm_->on_request_metadata_(this, id_, elements);
  CHECK_FAIL_HTTP(FilterMetadataStatus::Continue, FilterMetadataStatus::Continue);
  return convertVmCallResultToFilterMetadataStatus(result);
}

FilterHeadersStatus ContextBase::onResponseHeaders(uint32_t headers, bool end_of_stream) {
  CHECK_FAIL_HTTP(FilterHeadersStatus::Continue, FilterHeadersStatus::StopAllIterationAndWatermark);
  if (!wasm_->on_response_headers_abi_01_ && !wasm_->on_response_headers_abi_02_) {
    return FilterHeadersStatus::Continue;
  }
  DeferAfterCallActions actions(this);
  const auto result = wasm_->on_response_headers_abi_01_
                          ? wasm_->on_response_headers_abi_01_(this, id_, headers)
                          : wasm_->on_response_headers_abi_02_(
                                this, id_, headers, static_cast<uint32_t>(end_of_stream));
  CHECK_FAIL_HTTP(FilterHeadersStatus::Continue, FilterHeadersStatus::StopAllIterationAndWatermark);
  return convertVmCallResultToFilterHeadersStatus(result);
}

FilterDataStatus ContextBase::onResponseBody(uint32_t body_length, bool end_of_stream) {
  CHECK_FAIL_HTTP(FilterDataStatus::Continue, FilterDataStatus::StopIterationNoBuffer);
  if (!wasm_->on_response_body_) {
    return FilterDataStatus::Continue;
  }
  DeferAfterCallActions actions(this);
  const auto result =
      wasm_->on_response_body_(this, id_, body_length, static_cast<uint32_t>(end_of_stream));
  CHECK_FAIL_HTTP(FilterDataStatus::Continue, FilterDataStatus::StopIterationNoBuffer);
  return convertVmCallResultToFilterDataStatus(result);
}

FilterTrailersStatus ContextBase::onResponseTrailers(uint32_t trailers) {
  CHECK_FAIL_HTTP(FilterTrailersStatus::Continue, FilterTrailersStatus::StopIteration);
  if (!wasm_->on_response_trailers_) {
    return FilterTrailersStatus::Continue;
  }
  DeferAfterCallActions actions(this);
  const auto result = wasm_->on_response_trailers_(this, id_, trailers);
  CHECK_FAIL_HTTP(FilterTrailersStatus::Continue, FilterTrailersStatus::StopIteration);
  return convertVmCallResultToFilterTrailersStatus(result);
}

FilterMetadataStatus ContextBase::onResponseMetadata(uint32_t elements) {
  CHECK_FAIL_HTTP(FilterMetadataStatus::Continue, FilterMetadataStatus::Continue);
  if (!wasm_->on_response_metadata_) {
    return FilterMetadataStatus::Continue;
  }
  DeferAfterCallActions actions(this);
  const auto result = wasm_->on_response_metadata_(this, id_, elements);
  CHECK_FAIL_HTTP(FilterMetadataStatus::Continue, FilterMetadataStatus::Continue);
  return convertVmCallResultToFilterMetadataStatus(result);
}

void ContextBase::onHttpCallResponse(uint32_t token, uint32_t headers, uint32_t body_size,
                                     uint32_t trailers) {
  if (isFailed() || !wasm_->on_http_call_response_) {
    return;
  }
  DeferAfterCallActions actions(this);
  wasm_->on_http_call_response_(this, id_, token, headers, body_size, trailers);
}

void ContextBase::onQueueReady(uint32_t token) {
  if (!isFailed() && wasm_->on_queue_ready_) {
    DeferAfterCallActions actions(this);
    wasm_->on_queue_ready_(this, id_, token);
  }
}

void ContextBase::onGrpcReceiveInitialMetadata(uint32_t token, uint32_t elements) {
  if (isFailed() || !wasm_->on_grpc_receive_initial_metadata_) {
    return;
  }
  DeferAfterCallActions actions(this);
  wasm_->on_grpc_receive_initial_metadata_(this, id_, token, elements);
}

void ContextBase::onGrpcReceiveTrailingMetadata(uint32_t token, uint32_t trailers) {
  if (isFailed() || !wasm_->on_grpc_receive_trailing_metadata_) {
    return;
  }
  DeferAfterCallActions actions(this);
  wasm_->on_grpc_receive_trailing_metadata_(this, id_, token, trailers);
}

void ContextBase::onGrpcReceive(uint32_t token, uint32_t response_size) {
  if (isFailed() || !wasm_->on_grpc_receive_) {
    return;
  }
  DeferAfterCallActions actions(this);
  wasm_->on_grpc_receive_(this, id_, token, response_size);
}

void ContextBase::onGrpcClose(uint32_t token, uint32_t status_code) {
  if (isFailed() || !wasm_->on_grpc_close_) {
    return;
  }
  DeferAfterCallActions actions(this);
  wasm_->on_grpc_close_(this, id_, token, status_code);
}

bool ContextBase::onDone() {
  if (!isFailed() && wasm_->on_done_) {
    DeferAfterCallActions actions(this);
    return wasm_->on_done_(this, id_).u64_ != 0;
  }
  return true;
}

void ContextBase::onLog() {
  if (!isFailed() && wasm_->on_log_) {
    DeferAfterCallActions actions(this);
    wasm_->on_log_(this, id_);
  }
}

void ContextBase::onDelete() {
  if (in_vm_context_created_ && !isFailed() && wasm_->on_delete_) {
    DeferAfterCallActions actions(this);
    wasm_->on_delete_(this, id_);
  }
}

WasmResult ContextBase::setTimerPeriod(std::chrono::milliseconds period,
                                       uint32_t *timer_token_ptr) {
  wasm()->setTimerPeriod(root_context()->id(), period);
  *timer_token_ptr = 0;
  return WasmResult::Ok;
}

FilterHeadersStatus ContextBase::convertVmCallResultToFilterHeadersStatus(uint64_t result) {
  if (wasm()->isNextIterationStopped() ||
      result > static_cast<uint64_t>(FilterHeadersStatus::StopAllIterationAndWatermark)) {
    return FilterHeadersStatus::StopAllIterationAndWatermark;
  }
  if (result == static_cast<uint64_t>(FilterHeadersStatus::StopIteration)) {
    // Always convert StopIteration (pause processing headers, but continue processing body)
    // to StopAllIterationAndWatermark (pause all processing), since the former breaks all
    // assumptions about HTTP processing.
    return FilterHeadersStatus::StopAllIterationAndWatermark;
  }
  return static_cast<FilterHeadersStatus>(result);
}

FilterDataStatus ContextBase::convertVmCallResultToFilterDataStatus(uint64_t result) {
  if (wasm()->isNextIterationStopped() ||
      result > static_cast<uint64_t>(FilterDataStatus::StopIterationNoBuffer)) {
    return FilterDataStatus::StopIterationNoBuffer;
  }
  return static_cast<FilterDataStatus>(result);
}

FilterTrailersStatus ContextBase::convertVmCallResultToFilterTrailersStatus(uint64_t result) {
  if (wasm()->isNextIterationStopped() ||
      result > static_cast<uint64_t>(FilterTrailersStatus::StopIteration)) {
    return FilterTrailersStatus::StopIteration;
  }
  return static_cast<FilterTrailersStatus>(result);
}

FilterMetadataStatus ContextBase::convertVmCallResultToFilterMetadataStatus(uint64_t result) {
  if (static_cast<FilterMetadataStatus>(result) == FilterMetadataStatus::Continue) {
    return FilterMetadataStatus::Continue;
  }
  return FilterMetadataStatus::Continue; // This is currently the only return code.
}

ContextBase::~ContextBase() {
  // Do not remove vm context which has the same lifetime as wasm_.
  if (id_ != 0U) {
    wasm_->contexts_.erase(id_);
  }
}

} // namespace proxy_wasm
