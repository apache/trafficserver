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

#pragma once

#include <string.h>

#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "include/proxy-wasm/context.h"
#include "include/proxy-wasm/exports.h"
#include "include/proxy-wasm/wasm_vm.h"
#include "include/proxy-wasm/vm_id_handle.h"

namespace proxy_wasm {

#include "proxy_wasm_common.h"

class ContextBase;
class WasmHandleBase;

using WasmVmFactory = std::function<std::unique_ptr<WasmVm>()>;
using CallOnThreadFunction = std::function<void(std::function<void()>)>;

struct SanitizationConfig {
  std::vector<std::string> argument_list;
  bool is_allowlist;
};
using AllowedCapabilitiesMap = std::unordered_map<std::string, SanitizationConfig>;

// Wasm execution instance. Manages the host side of the Wasm interface.
class WasmBase : public std::enable_shared_from_this<WasmBase> {
public:
  WasmBase(std::unique_ptr<WasmVm> wasm_vm, std::string_view vm_id,
           std::string_view vm_configuration, std::string_view vm_key,
           std::unordered_map<std::string, std::string> envs,
           AllowedCapabilitiesMap allowed_capabilities);
  WasmBase(const std::shared_ptr<WasmHandleBase> &base_wasm_handle, const WasmVmFactory &factory);
  virtual ~WasmBase();

  bool load(const std::string &code, bool allow_precompiled = false);
  bool initialize();
  void startVm(ContextBase *root_context);
  bool configure(ContextBase *root_context, std::shared_ptr<PluginBase> plugin);
  // Returns the root ContextBase or nullptr if onStart returns false.
  ContextBase *start(const std::shared_ptr<PluginBase> &plugin);

  std::string_view vm_id() const { return vm_id_; }
  std::string_view vm_key() const { return vm_key_; }
  WasmVm *wasm_vm() const { return wasm_vm_.get(); }
  ContextBase *vm_context() const { return vm_context_.get(); }
  ContextBase *getRootContext(const std::shared_ptr<PluginBase> &plugin, bool allow_closed);
  ContextBase *getContext(uint32_t id) {
    auto it = contexts_.find(id);
    if (it != contexts_.end())
      return it->second;
    return nullptr;
  }
  uint32_t allocContextId();
  bool isFailed() { return failed_ != FailState::Ok; }
  FailState fail_state() { return failed_; }

  const std::string &vm_configuration() const;

  const std::string &moduleBytecode() const { return module_bytecode_; }
  const std::string &modulePrecompiled() const { return module_precompiled_; }
  const std::unordered_map<uint32_t, std::string> functionNames() const { return function_names_; }

  void timerReady(uint32_t root_context_id);
  void queueReady(uint32_t root_context_id, uint32_t token);

  void startShutdown(std::string_view plugin_key);
  void startShutdown();
  WasmResult done(ContextBase *root_context);
  void finishShutdown();

  // Proxy specific extension points.
  //
  virtual void registerCallbacks(); // Register functions called out from Wasm.
  virtual void getFunctions();      // Get functions call into Wasm.
  virtual CallOnThreadFunction callOnThreadFunction() {
    unimplemented();
    return nullptr;
  }

  // Capability restriction (restricting/exposing the ABI).
  bool capabilityAllowed(std::string capability_name) {
    return allowed_capabilities_.empty() ||
           allowed_capabilities_.find(capability_name) != allowed_capabilities_.end();
  }

  virtual ContextBase *createVmContext() { return new ContextBase(this); }
  virtual ContextBase *createRootContext(const std::shared_ptr<PluginBase> &plugin) {
    return new ContextBase(this, plugin);
  }
  virtual ContextBase *createContext(const std::shared_ptr<PluginBase> &plugin) {
    return new ContextBase(this, plugin);
  }
  virtual void setTimerPeriod(uint32_t root_context_id, std::chrono::milliseconds period) {
    timer_period_[root_context_id] = period;
  }

  // Support functions.
  //
  void *allocMemory(uint64_t size, uint64_t *address);
  // Allocate a null-terminated string in the VM and return the pointer to use as a call arguments.
  uint64_t copyString(std::string_view s);
  // Copy the data in 's' into the VM along with the pointer-size pair. Returns true on success.
  bool copyToPointerSize(std::string_view s, uint64_t ptr_ptr, uint64_t size_ptr);
  template <typename T> bool setDatatype(uint64_t ptr, const T &t);

  void fail(FailState fail_state, std::string_view message) {
    error(message);
    failed_ = fail_state;
  }
  virtual void error(std::string_view message) { std::cerr << message << "\n"; }
  virtual void unimplemented() { error("unimplemented proxy-wasm API"); }

  AbiVersion abiVersion() const { return abi_version_; }

  const std::unordered_map<std::string, std::string> &envs() { return envs_; }

  // Called to raise the flag which indicates that the context should stop iteration regardless of
  // returned filter status from Proxy-Wasm extensions. For example, we ignore
  // FilterHeadersStatus::Continue after a local reponse is sent by the host.
  void stopNextIteration(bool stop) { stop_iteration_ = stop; };
  bool isNextIterationStopped() { return stop_iteration_; };

  void addAfterVmCallAction(std::function<void()> f) { after_vm_call_actions_.push_back(f); }
  void doAfterVmCallActions() {
    // NB: this may be deleted by a delayed function unless prevented.
    if (!after_vm_call_actions_.empty()) {
      auto self = shared_from_this();
      while (!self->after_vm_call_actions_.empty()) {
        auto f = std::move(self->after_vm_call_actions_.front());
        self->after_vm_call_actions_.pop_front();
        f();
      }
    }
  }

  static const uint32_t kMetricTypeMask = 0x3;    // Enough to cover the 3 types.
  static const uint32_t kMetricIdIncrement = 0x4; // Enough to cover the 3 types.
  bool isCounterMetricId(uint32_t metric_id) {
    return (metric_id & kMetricTypeMask) == static_cast<uint32_t>(MetricType::Counter);
  }
  bool isGaugeMetricId(uint32_t metric_id) {
    return (metric_id & kMetricTypeMask) == static_cast<uint32_t>(MetricType::Gauge);
  }
  bool isHistogramMetricId(uint32_t metric_id) {
    return (metric_id & kMetricTypeMask) == static_cast<uint32_t>(MetricType::Histogram);
  }
  uint32_t nextCounterMetricId() { return next_counter_metric_id_ += kMetricIdIncrement; }
  uint32_t nextGaugeMetricId() { return next_gauge_metric_id_ += kMetricIdIncrement; }
  uint32_t nextHistogramMetricId() { return next_histogram_metric_id_ += kMetricIdIncrement; }

  enum class CalloutType : uint32_t {
    HttpCall = 0,
    GrpcCall = 1,
    GrpcStream = 2,
  };
  static const uint32_t kCalloutTypeMask = 0x3;  // Enough to cover the 3 types.
  static const uint32_t kCalloutIncrement = 0x4; // Enough to cover the 3 types.
  bool isHttpCallId(uint32_t callout_id) {
    return (callout_id & kCalloutTypeMask) == static_cast<uint32_t>(CalloutType::HttpCall);
  }
  bool isGrpcCallId(uint32_t callout_id) {
    return (callout_id & kCalloutTypeMask) == static_cast<uint32_t>(CalloutType::GrpcCall);
  }
  bool isGrpcStreamId(uint32_t callout_id) {
    return (callout_id & kCalloutTypeMask) == static_cast<uint32_t>(CalloutType::GrpcStream);
  }
  uint32_t nextHttpCallId() {
    // TODO(PiotrSikora): re-add rollover protection (requires at least 1 billion callouts).
    return next_http_call_id_ += kCalloutIncrement;
  }
  uint32_t nextGrpcCallId() {
    // TODO(PiotrSikora): re-add rollover protection (requires at least 1 billion callouts).
    return next_grpc_call_id_ += kCalloutIncrement;
  }
  uint32_t nextGrpcStreamId() {
    // TODO(PiotrSikora): re-add rollover protection (requires at least 1 billion callouts).
    return next_grpc_stream_id_ += kCalloutIncrement;
  }

protected:
  friend class ContextBase;
  class ShutdownHandle;

  void establishEnvironment(); // Language specific environments.

  std::string vm_id_;  // User-provided vm_id.
  std::string vm_key_; // vm_id + hash of code.
  std::unique_ptr<WasmVm> wasm_vm_;
  std::optional<Cloneable> started_from_;

  uint32_t next_context_id_ = 1;            // 0 is reserved for the VM context.
  std::shared_ptr<ContextBase> vm_context_; // Context unrelated to any specific root or stream
                                            // (e.g. for global constructors).
  std::unordered_map<std::string, std::unique_ptr<ContextBase>> root_contexts_; // Root contexts.
  std::unordered_map<std::string, std::unique_ptr<ContextBase>> pending_done_;  // Root contexts.
  std::unordered_set<std::unique_ptr<ContextBase>> pending_delete_;             // Root contexts.
  std::unordered_map<uint32_t, ContextBase *> contexts_;                 // Contains all contexts.
  std::unordered_map<uint32_t, std::chrono::milliseconds> timer_period_; // per root_id.
  std::unique_ptr<ShutdownHandle> shutdown_handle_;
  std::unordered_map<std::string, std::string>
      envs_; // environment variables passed through wasi.environ_get

  WasmCallVoid<0> _initialize_; /* WASI reactor (Emscripten v1.39.17+, Rust nightly) */
  WasmCallVoid<0> _start_;      /* WASI command (Emscripten v1.39.0+, TinyGo) */

  WasmCallWord<2> main_;
  WasmCallWord<1> malloc_;

  // Calls into the VM.
  WasmCallWord<2> validate_configuration_;
  WasmCallWord<2> on_vm_start_;
  WasmCallWord<2> on_configure_;
  WasmCallVoid<1> on_tick_;

  WasmCallVoid<2> on_context_create_;

  WasmCallWord<1> on_new_connection_;
  WasmCallWord<3> on_downstream_data_;
  WasmCallWord<3> on_upstream_data_;
  WasmCallVoid<2> on_downstream_connection_close_;
  WasmCallVoid<2> on_upstream_connection_close_;

  WasmCallWord<2> on_request_headers_abi_01_;
  WasmCallWord<3> on_request_headers_abi_02_;
  WasmCallWord<3> on_request_body_;
  WasmCallWord<2> on_request_trailers_;
  WasmCallWord<2> on_request_metadata_;

  WasmCallWord<2> on_response_headers_abi_01_;
  WasmCallWord<3> on_response_headers_abi_02_;
  WasmCallWord<3> on_response_body_;
  WasmCallWord<2> on_response_trailers_;
  WasmCallWord<2> on_response_metadata_;

  WasmCallVoid<5> on_http_call_response_;

  WasmCallVoid<3> on_grpc_receive_;
  WasmCallVoid<3> on_grpc_close_;
  WasmCallVoid<3> on_grpc_create_initial_metadata_;
  WasmCallVoid<3> on_grpc_receive_initial_metadata_;
  WasmCallVoid<3> on_grpc_receive_trailing_metadata_;

  WasmCallVoid<2> on_queue_ready_;
  WasmCallVoid<3> on_foreign_function_;

  WasmCallWord<1> on_done_;
  WasmCallVoid<1> on_log_;
  WasmCallVoid<1> on_delete_;

#define FOR_ALL_MODULE_FUNCTIONS(_f)                                                               \
  _f(validate_configuration) _f(on_vm_start) _f(on_configure) _f(on_tick) _f(on_context_create)    \
      _f(on_new_connection) _f(on_downstream_data) _f(on_upstream_data)                            \
          _f(on_downstream_connection_close) _f(on_upstream_connection_close) _f(on_request_body)  \
              _f(on_request_trailers) _f(on_request_metadata) _f(on_response_body)                 \
                  _f(on_response_trailers) _f(on_response_metadata) _f(on_http_call_response)      \
                      _f(on_grpc_receive) _f(on_grpc_close) _f(on_grpc_receive_initial_metadata)   \
                          _f(on_grpc_receive_trailing_metadata) _f(on_queue_ready) _f(on_done)     \
                              _f(on_log) _f(on_delete)

  // Capabilities which are allowed to be linked to the module. If this is empty, restriction
  // is not enforced.
  AllowedCapabilitiesMap allowed_capabilities_;

  std::shared_ptr<WasmHandleBase> base_wasm_handle_;

  // Used by the base_wasm to enable non-clonable thread local Wasm(s) to be constructed.
  std::string module_bytecode_;
  std::string module_precompiled_;
  std::unordered_map<uint32_t, std::string> function_names_;

  // ABI version.
  AbiVersion abi_version_ = AbiVersion::Unknown;

  std::string vm_configuration_;
  bool stop_iteration_ = false;
  FailState failed_ = FailState::Ok; // Wasm VM fatal error.

  // Plugin Stats/Metrics
  uint32_t next_counter_metric_id_ = static_cast<uint32_t>(MetricType::Counter);
  uint32_t next_gauge_metric_id_ = static_cast<uint32_t>(MetricType::Gauge);
  uint32_t next_histogram_metric_id_ = static_cast<uint32_t>(MetricType::Histogram);

  // HTTP/gRPC callouts.
  uint32_t next_http_call_id_ = static_cast<uint32_t>(CalloutType::HttpCall);
  uint32_t next_grpc_call_id_ = static_cast<uint32_t>(CalloutType::GrpcCall);
  uint32_t next_grpc_stream_id_ = static_cast<uint32_t>(CalloutType::GrpcStream);

  // Actions to be done after the call into the VM returns.
  std::deque<std::function<void()>> after_vm_call_actions_;

  std::shared_ptr<VmIdHandle> vm_id_handle_;
};

using WasmHandleFactory = std::function<std::shared_ptr<WasmHandleBase>(std::string_view vm_id)>;
using WasmHandleCloneFactory =
    std::function<std::shared_ptr<WasmHandleBase>(std::shared_ptr<WasmHandleBase> wasm)>;

// Handle which enables shutdown operations to run post deletion (e.g. post listener drain).
class WasmHandleBase : public std::enable_shared_from_this<WasmHandleBase> {
public:
  explicit WasmHandleBase(std::shared_ptr<WasmBase> wasm_base) : wasm_base_(wasm_base) {}
  ~WasmHandleBase() {
    if (wasm_base_) {
      wasm_base_->startShutdown();
    }
  }

  bool canary(const std::shared_ptr<PluginBase> &plugin,
              const WasmHandleCloneFactory &clone_factory);

  void kill() { wasm_base_ = nullptr; }

  std::shared_ptr<WasmBase> &wasm() { return wasm_base_; }

protected:
  std::shared_ptr<WasmBase> wasm_base_;
  std::unordered_map<std::string, bool> plugin_canary_cache_;
};

std::string makeVmKey(std::string_view vm_id, std::string_view configuration,
                      std::string_view code);

// Returns nullptr on failure (i.e. initialization of the VM fails).
std::shared_ptr<WasmHandleBase> createWasm(const std::string &vm_key, const std::string &code,
                                           const std::shared_ptr<PluginBase> &plugin,
                                           const WasmHandleFactory &factory,
                                           const WasmHandleCloneFactory &clone_factory,
                                           bool allow_precompiled);
// Get an existing ThreadLocal VM matching 'vm_key' or nullptr if there isn't one.
std::shared_ptr<WasmHandleBase> getThreadLocalWasm(std::string_view vm_key);

class PluginHandleBase : public std::enable_shared_from_this<PluginHandleBase> {
public:
  explicit PluginHandleBase(std::shared_ptr<WasmHandleBase> wasm_handle,
                            std::shared_ptr<PluginBase> plugin)
      : plugin_(plugin), wasm_handle_(wasm_handle) {}
  ~PluginHandleBase() {
    if (wasm_handle_) {
      wasm_handle_->wasm()->startShutdown(plugin_->key());
    }
  }

  std::shared_ptr<PluginBase> &plugin() { return plugin_; }
  std::shared_ptr<WasmBase> &wasm() { return wasm_handle_->wasm(); }

protected:
  std::shared_ptr<PluginBase> plugin_;
  std::shared_ptr<WasmHandleBase> wasm_handle_;
};

using PluginHandleFactory = std::function<std::shared_ptr<PluginHandleBase>(
    std::shared_ptr<WasmHandleBase> base_wasm, std::shared_ptr<PluginBase> plugin)>;

// Get an existing ThreadLocal VM matching 'vm_id' or create one using 'base_wavm' by cloning or by
// using it it as a template.
std::shared_ptr<PluginHandleBase> getOrCreateThreadLocalPlugin(
    const std::shared_ptr<WasmHandleBase> &base_handle, const std::shared_ptr<PluginBase> &plugin,
    const WasmHandleCloneFactory &clone_factory, const PluginHandleFactory &plugin_factory);

// Clear Base Wasm cache and the thread-local Wasm sandbox cache for the calling thread.
void clearWasmCachesForTesting();

inline const std::string &WasmBase::vm_configuration() const {
  if (base_wasm_handle_)
    return base_wasm_handle_->wasm()->vm_configuration_;
  return vm_configuration_;
}

inline void *WasmBase::allocMemory(uint64_t size, uint64_t *address) {
  if (!malloc_) {
    return nullptr;
  }
  wasm_vm_->setRestrictedCallback(
      true, {// logging (Proxy-Wasm)
             "env.proxy_log",
             // logging (stdout/stderr)
             "wasi_unstable.fd_write", "wasi_snapshot_preview1.fd_write",
             // time
             "wasi_unstable.clock_time_get", "wasi_snapshot_preview1.clock_time_get"});
  Word a = malloc_(vm_context(), size);
  wasm_vm_->setRestrictedCallback(false);
  if (!a.u64_) {
    return nullptr;
  }
  auto memory = wasm_vm_->getMemory(a.u64_, size);
  if (!memory) {
    return nullptr;
  }
  *address = a.u64_;
  return const_cast<void *>(reinterpret_cast<const void *>(memory.value().data()));
}

inline uint64_t WasmBase::copyString(std::string_view s) {
  if (s.empty()) {
    return 0; // nullptr
  }
  uint64_t pointer = 0;
  uint8_t *m = static_cast<uint8_t *>(allocMemory((s.size() + 1), &pointer));
  memcpy(m, s.data(), s.size());
  m[s.size()] = 0;
  return pointer;
}

inline bool WasmBase::copyToPointerSize(std::string_view s, uint64_t ptr_ptr, uint64_t size_ptr) {
  uint64_t pointer = 0;
  uint64_t size = s.size();
  void *p = nullptr;
  if (size > 0) {
    p = allocMemory(size, &pointer);
    if (!p) {
      return false;
    }
    memcpy(p, s.data(), size);
  }
  if (!wasm_vm_->setWord(ptr_ptr, Word(pointer))) {
    return false;
  }
  if (!wasm_vm_->setWord(size_ptr, Word(size))) {
    return false;
  }
  return true;
}

template <typename T> inline bool WasmBase::setDatatype(uint64_t ptr, const T &t) {
  return wasm_vm_->setMemory(ptr, sizeof(T), &t);
}

} // namespace proxy_wasm
