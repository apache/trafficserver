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

#include "include/proxy-wasm/null_plugin.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <atomic>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "include/proxy-wasm/null_plugin.h"
#include "include/proxy-wasm/null_vm.h"
#include "include/proxy-wasm/wasm.h"

namespace proxy_wasm {

void NullPlugin::getFunction(std::string_view function_name, WasmCallVoid<0> *f) {
  if (function_name == "_initialize") {
    *f = nullptr;
  } else if (function_name == "_start") {
    *f = nullptr;
  } else if (!wasm_vm_->integration()->getNullVmFunction(function_name, false, 0, this, f)) {
    error("Missing getFunction for: " + std::string(function_name));
    *f = nullptr;
  }
}

void NullPlugin::getFunction(std::string_view function_name, WasmCallVoid<1> *f) {
  auto *plugin = this;
  if (function_name == "proxy_on_tick") {
    *f = [plugin](ContextBase *context, Word context_id) {
      SaveRestoreContext saved_context(context);
      plugin->onTick(context_id);
    };
  } else if (function_name == "proxy_on_log") {
    *f = [plugin](ContextBase *context, Word context_id) {
      SaveRestoreContext saved_context(context);
      plugin->onLog(context_id);
    };
  } else if (function_name == "proxy_on_delete") {
    *f = [plugin](ContextBase *context, Word context_id) {
      SaveRestoreContext saved_context(context);
      plugin->onDelete(context_id);
    };
  } else if (!wasm_vm_->integration()->getNullVmFunction(function_name, false, 1, this, f)) {
    error("Missing getFunction for: " + std::string(function_name));
    *f = nullptr;
  }
}

void NullPlugin::getFunction(std::string_view function_name, WasmCallVoid<2> *f) {
  auto *plugin = this;
  if (function_name == "proxy_on_context_create") {
    *f = [plugin](ContextBase *context, Word context_id, Word parent_context_id) {
      SaveRestoreContext saved_context(context);
      plugin->onCreate(context_id, parent_context_id);
    };
  } else if (function_name == "proxy_on_downstream_connection_close") {
    *f = [plugin](ContextBase *context, Word context_id, Word peer_type) {
      SaveRestoreContext saved_context(context);
      plugin->onDownstreamConnectionClose(context_id, peer_type);
    };
  } else if (function_name == "proxy_on_upstream_connection_close") {
    *f = [plugin](ContextBase *context, Word context_id, Word peer_type) {
      SaveRestoreContext saved_context(context);
      plugin->onUpstreamConnectionClose(context_id, peer_type);
    };
  } else if (function_name == "proxy_on_queue_ready") {
    *f = [plugin](ContextBase *context, Word context_id, Word token) {
      SaveRestoreContext saved_context(context);
      plugin->onQueueReady(context_id, token);
    };
  } else if (!wasm_vm_->integration()->getNullVmFunction(function_name, false, 2, this, f)) {
    error("Missing getFunction for: " + std::string(function_name));
    *f = nullptr;
  }
}

void NullPlugin::getFunction(std::string_view function_name, WasmCallVoid<3> *f) {
  auto *plugin = this;
  if (function_name == "proxy_on_grpc_close") {
    *f = [plugin](ContextBase *context, Word context_id, Word token, Word status_code) {
      SaveRestoreContext saved_context(context);
      plugin->onGrpcClose(context_id, token, status_code);
    };
  } else if (function_name == "proxy_on_grpc_receive") {
    *f = [plugin](ContextBase *context, Word context_id, Word token, Word body_size) {
      SaveRestoreContext saved_context(context);
      plugin->onGrpcReceive(context_id, token, body_size);
    };
  } else if (function_name == "proxy_on_grpc_receive_initial_metadata") {
    *f = [plugin](ContextBase *context, Word context_id, Word token, Word headers) {
      SaveRestoreContext saved_context(context);
      plugin->onGrpcReceiveInitialMetadata(context_id, token, headers);
    };
  } else if (function_name == "proxy_on_grpc_receive_trailing_metadata") {
    *f = [plugin](ContextBase *context, Word context_id, Word token, Word trailers) {
      SaveRestoreContext saved_context(context);
      plugin->onGrpcReceiveTrailingMetadata(context_id, token, trailers);
    };
  } else if (function_name == "proxy_on_foreign_function") {
    *f = [plugin](ContextBase *context, Word context_id, Word foreign_function_id, Word data_size) {
      SaveRestoreContext saved_context(context);
      plugin->onForeignFunction(context_id, foreign_function_id, data_size);
    };
  } else if (!wasm_vm_->integration()->getNullVmFunction(function_name, false, 3, this, f)) {
    error("Missing getFunction for: " + std::string(function_name));
    *f = nullptr;
  }
}

void NullPlugin::getFunction(std::string_view function_name, WasmCallVoid<5> *f) {
  auto *plugin = this;
  if (function_name == "proxy_on_http_call_response") {
    *f = [plugin](ContextBase *context, Word context_id, Word token, Word headers, Word body_size,
                  Word trailers) {
      SaveRestoreContext saved_context(context);
      plugin->onHttpCallResponse(context_id, token, headers, body_size, trailers);
    };
  } else if (!wasm_vm_->integration()->getNullVmFunction(function_name, false, 5, this, f)) {
    error("Missing getFunction for: " + std::string(function_name));
    *f = nullptr;
  }
}

void NullPlugin::getFunction(std::string_view function_name, WasmCallWord<1> *f) {
  auto *plugin = this;
  if (function_name == "malloc") {
    *f = [](ContextBase * /*context*/, Word size) -> Word {
      return Word(reinterpret_cast<uint64_t>(::malloc(size)));
    };
  } else if (function_name == "proxy_on_new_connection") {
    *f = [plugin](ContextBase *context, Word context_id) -> Word {
      SaveRestoreContext saved_context(context);
      return Word(plugin->onNewConnection(context_id));
    };
  } else if (function_name == "proxy_on_done") {
    *f = [plugin](ContextBase *context, Word context_id) {
      SaveRestoreContext saved_context(context);
      return Word(plugin->onDone(context_id));
    };
  } else if (!wasm_vm_->integration()->getNullVmFunction(function_name, true, 1, this, f)) {
    error("Missing getFunction for: " + std::string(function_name));
    *f = nullptr;
  }
}

void NullPlugin::getFunction(std::string_view function_name, WasmCallWord<2> *f) {
  auto *plugin = this;
  if (function_name == "main") {
    *f = nullptr;
  } else if (function_name == "proxy_on_vm_start") {
    *f = [plugin](ContextBase *context, Word context_id, Word configuration_size) {
      SaveRestoreContext saved_context(context);
      return Word(static_cast<uint64_t>(plugin->onStart(context_id, configuration_size)));
    };
  } else if (function_name == "proxy_on_configure") {
    *f = [plugin](ContextBase *context, Word context_id, Word configuration_size) {
      SaveRestoreContext saved_context(context);
      return Word(static_cast<uint64_t>(plugin->onConfigure(context_id, configuration_size)));
    };
  } else if (function_name == "proxy_validate_configuration") {
    *f = [plugin](ContextBase *context, Word context_id, Word configuration_size) {
      SaveRestoreContext saved_context(context);
      return Word(
          static_cast<uint64_t>(plugin->validateConfiguration(context_id, configuration_size)));
    };
  } else if (function_name == "proxy_on_request_trailers") {
    *f = [plugin](ContextBase *context, Word context_id, Word trailers) -> Word {
      SaveRestoreContext saved_context(context);
      return Word(plugin->onRequestTrailers(context_id, trailers));
    };
  } else if (function_name == "proxy_on_request_metadata") {
    *f = [plugin](ContextBase *context, Word context_id, Word elements) -> Word {
      SaveRestoreContext saved_context(context);
      return Word(plugin->onRequestMetadata(context_id, elements));
    };
  } else if (function_name == "proxy_on_response_trailers") {
    *f = [plugin](ContextBase *context, Word context_id, Word trailers) -> Word {
      SaveRestoreContext saved_context(context);
      return Word(plugin->onResponseTrailers(context_id, trailers));
    };
  } else if (function_name == "proxy_on_response_metadata") {
    *f = [plugin](ContextBase *context, Word context_id, Word elements) -> Word {
      SaveRestoreContext saved_context(context);
      return Word(plugin->onResponseMetadata(context_id, elements));
    };
  } else if (!wasm_vm_->integration()->getNullVmFunction(function_name, true, 2, this, f)) {
    error("Missing getFunction for: " + std::string(function_name));
    *f = nullptr;
  }
}

void NullPlugin::getFunction(std::string_view function_name, WasmCallWord<3> *f) {
  auto *plugin = this;
  if (function_name == "proxy_on_downstream_data") {
    *f = [plugin](ContextBase *context, Word context_id, Word body_buffer_length,
                  Word end_of_stream) -> Word {
      SaveRestoreContext saved_context(context);
      return Word(plugin->onDownstreamData(context_id, body_buffer_length, end_of_stream));
    };
  } else if (function_name == "proxy_on_upstream_data") {
    *f = [plugin](ContextBase *context, Word context_id, Word body_buffer_length,
                  Word end_of_stream) -> Word {
      SaveRestoreContext saved_context(context);
      return Word(plugin->onUpstreamData(context_id, body_buffer_length, end_of_stream));
    };
  } else if (function_name == "proxy_on_request_headers") {
    *f = [plugin](ContextBase *context, Word context_id, Word headers, Word end_of_stream) -> Word {
      SaveRestoreContext saved_context(context);
      return Word(plugin->onRequestHeaders(context_id, headers, end_of_stream));
    };
  } else if (function_name == "proxy_on_request_body") {
    *f = [plugin](ContextBase *context, Word context_id, Word body_buffer_length,
                  Word end_of_stream) -> Word {
      SaveRestoreContext saved_context(context);
      return Word(plugin->onRequestBody(context_id, body_buffer_length, end_of_stream));
    };
  } else if (function_name == "proxy_on_response_headers") {
    *f = [plugin](ContextBase *context, Word context_id, Word headers, Word end_of_stream) -> Word {
      SaveRestoreContext saved_context(context);
      return Word(plugin->onResponseHeaders(context_id, headers, end_of_stream));
    };
  } else if (function_name == "proxy_on_response_body") {
    *f = [plugin](ContextBase *context, Word context_id, Word body_buffer_length,
                  Word end_of_stream) -> Word {
      SaveRestoreContext saved_context(context);
      return Word(plugin->onResponseBody(context_id, body_buffer_length, end_of_stream));
    };
  } else if (!wasm_vm_->integration()->getNullVmFunction(function_name, true, 3, this, f)) {
    error("Missing getFunction for: " + std::string(function_name));
    *f = nullptr;
  }
}

null_plugin::Context *NullPlugin::ensureContext(uint64_t context_id, uint64_t root_context_id) {
  auto e = context_map_.insert(std::make_pair(context_id, nullptr));
  if (e.second) {
    auto *root_base = context_map_[root_context_id].get();
    null_plugin::RootContext *root = (root_base != nullptr) ? root_base->asRoot() : nullptr;
    std::string root_id = (root != nullptr) ? std::string(root->root_id()) : "";
    auto factory = registry_->context_factories[root_id];
    if (!factory) {
      error("no context factory for root_id: " + root_id);
      return nullptr;
    }
    e.first->second = factory(context_id, root);
  }
  return e.first->second->asContext();
}

null_plugin::RootContext *NullPlugin::ensureRootContext(uint64_t context_id) {
  auto root_id_opt = null_plugin::getProperty({"plugin_root_id"});
  if (!root_id_opt) {
    error("unable to get root_id");
    return nullptr;
  }
  auto root_id = std::move(root_id_opt.value());
  auto it = context_map_.find(context_id);
  if (it != context_map_.end()) {
    return it->second->asRoot();
  }
  auto root_id_string = root_id->toString();
  auto factory = registry_->root_factories[root_id_string];
  null_plugin::RootContext *root_context;
  if (factory) {
    auto context = factory(context_id, root_id->view());
    root_context = context->asRoot();
    root_context_map_[root_id_string] = root_context;
    context_map_[context_id] = std::move(context);
  } else {
    // Default handlers.
    auto context = std::make_unique<null_plugin::RootContext>(static_cast<uint32_t>(context_id),
                                                              root_id->view());
    root_context = context->asRoot();
    context_map_[context_id] = std::move(context);
  }
  return root_context;
}

null_plugin::ContextBase *NullPlugin::getContextBase(uint64_t context_id) {
  auto it = context_map_.find(context_id);
  if (it == context_map_.end() ||
      !(it->second->asContext() != nullptr || it->second->asRoot() != nullptr)) {
    error("no base context context_id: " + std::to_string(context_id));
    return nullptr;
  }
  return it->second.get();
}

null_plugin::Context *NullPlugin::getContext(uint64_t context_id) {
  auto it = context_map_.find(context_id);
  if (it == context_map_.end() || (it->second->asContext() == nullptr)) {
    error("no context context_id: " + std::to_string(context_id));
    return nullptr;
  }
  return it->second->asContext();
}

null_plugin::RootContext *NullPlugin::getRootContext(uint64_t context_id) {
  auto it = context_map_.find(context_id);
  if (it == context_map_.end() || (it->second->asRoot() == nullptr)) {
    error("no root context_id: " + std::to_string(context_id));
    return nullptr;
  }
  return it->second->asRoot();
}

null_plugin::RootContext *NullPlugin::getRoot(std::string_view root_id) {
  auto it = root_context_map_.find(std::string(root_id));
  if (it == root_context_map_.end()) {
    return nullptr;
  }
  return it->second;
}

bool NullPlugin::validateConfiguration(uint64_t root_context_id, uint64_t configuration_size) {
  return getRootContext(root_context_id)->validateConfiguration(configuration_size);
}

bool NullPlugin::onStart(uint64_t root_context_id, uint64_t vm_configuration_size) {
  if (registry_->proxy_on_vm_start_ != nullptr) {
    return registry_->proxy_on_vm_start_(root_context_id, vm_configuration_size) != 0U;
  }
  return getRootContext(root_context_id)->onStart(vm_configuration_size);
}

bool NullPlugin::onConfigure(uint64_t root_context_id, uint64_t plugin_configuration_size) {
  if (registry_->proxy_on_configure_ != nullptr) {
    return registry_->proxy_on_configure_(root_context_id, plugin_configuration_size) != 0U;
  }
  return getRootContext(root_context_id)->onConfigure(plugin_configuration_size);
}

void NullPlugin::onTick(uint64_t root_context_id) {
  if (registry_->proxy_on_tick_ != nullptr) {
    return registry_->proxy_on_tick_(root_context_id);
  }
  getRootContext(root_context_id)->onTick();
}

void NullPlugin::onCreate(uint64_t context_id, uint64_t parent_context_id) {
  if (registry_->proxy_on_context_create_ != nullptr) {
    registry_->proxy_on_context_create_(context_id, parent_context_id);
    return;
  }
  if (parent_context_id != 0U) {
    ensureContext(context_id, parent_context_id)->onCreate();
  } else {
    ensureRootContext(context_id)->onCreate();
  }
}

uint64_t NullPlugin::onNewConnection(uint64_t context_id) {
  return static_cast<uint64_t>(getContext(context_id)->onNewConnection());
}

uint64_t NullPlugin::onDownstreamData(uint64_t context_id, uint64_t data_length,
                                      uint64_t end_of_stream) {
  return static_cast<uint64_t>(
      getContext(context_id)
          ->onDownstreamData(static_cast<size_t>(data_length), end_of_stream != 0));
}

uint64_t NullPlugin::onUpstreamData(uint64_t context_id, uint64_t data_length,
                                    uint64_t end_of_stream) {
  return static_cast<uint64_t>(
      getContext(context_id)->onUpstreamData(static_cast<size_t>(data_length), end_of_stream != 0));
}

void NullPlugin::onDownstreamConnectionClose(uint64_t context_id, uint64_t close_type) {
  getContext(context_id)->onDownstreamConnectionClose(static_cast<CloseType>(close_type));
}

void NullPlugin::onUpstreamConnectionClose(uint64_t context_id, uint64_t close_type) {
  getContext(context_id)->onUpstreamConnectionClose(static_cast<CloseType>(close_type));
}

uint64_t NullPlugin::onRequestHeaders(uint64_t context_id, uint64_t headers,
                                      uint64_t end_of_stream) {
  return static_cast<uint64_t>(
      getContext(context_id)->onRequestHeaders(headers, end_of_stream != 0));
}

uint64_t NullPlugin::onRequestBody(uint64_t context_id, uint64_t body_buffer_length,
                                   uint64_t end_of_stream) {
  return static_cast<uint64_t>(
      getContext(context_id)
          ->onRequestBody(static_cast<size_t>(body_buffer_length), end_of_stream != 0));
}

uint64_t NullPlugin::onRequestTrailers(uint64_t context_id, uint64_t trailers) {
  return static_cast<uint64_t>(getContext(context_id)->onRequestTrailers(trailers));
}

uint64_t NullPlugin::onRequestMetadata(uint64_t context_id, uint64_t elements) {
  return static_cast<uint64_t>(getContext(context_id)->onRequestMetadata(elements));
}

uint64_t NullPlugin::onResponseHeaders(uint64_t context_id, uint64_t headers,
                                       uint64_t end_of_stream) {
  return static_cast<uint64_t>(
      getContext(context_id)->onResponseHeaders(headers, end_of_stream != 0));
}

uint64_t NullPlugin::onResponseBody(uint64_t context_id, uint64_t body_buffer_length,
                                    uint64_t end_of_stream) {
  return static_cast<uint64_t>(
      getContext(context_id)
          ->onResponseBody(static_cast<size_t>(body_buffer_length), end_of_stream != 0));
}

uint64_t NullPlugin::onResponseTrailers(uint64_t context_id, uint64_t trailers) {
  return static_cast<uint64_t>(getContext(context_id)->onResponseTrailers(trailers));
}

uint64_t NullPlugin::onResponseMetadata(uint64_t context_id, uint64_t elements) {
  return static_cast<uint64_t>(getContext(context_id)->onResponseMetadata(elements));
}

void NullPlugin::onHttpCallResponse(uint64_t context_id, uint64_t token, uint64_t headers,
                                    uint64_t body_size, uint64_t trailers) {
  getRootContext(context_id)->onHttpCallResponse(token, headers, body_size, trailers);
}

void NullPlugin::onGrpcReceive(uint64_t context_id, uint64_t token, size_t body_size) {
  getRootContext(context_id)->onGrpcReceive(token, body_size);
}

void NullPlugin::onGrpcClose(uint64_t context_id, uint64_t token, uint64_t status_code) {
  getRootContext(context_id)->onGrpcClose(token, static_cast<GrpcStatus>(status_code));
}

void NullPlugin::onGrpcReceiveInitialMetadata(uint64_t context_id, uint64_t token,
                                              uint64_t headers) {
  getRootContext(context_id)->onGrpcReceiveInitialMetadata(token, headers);
}

void NullPlugin::onGrpcReceiveTrailingMetadata(uint64_t context_id, uint64_t token,
                                               uint64_t trailers) {
  getRootContext(context_id)->onGrpcReceiveTrailingMetadata(token, trailers);
}

void NullPlugin::onQueueReady(uint64_t context_id, uint64_t token) {
  getRootContext(context_id)->onQueueReady(token);
}

void NullPlugin::onForeignFunction(uint64_t context_id, uint64_t foreign_function_id,
                                   uint64_t data_size) {
  if (registry_->proxy_on_foreign_function_ != nullptr) {
    return registry_->proxy_on_foreign_function_(context_id, foreign_function_id, data_size);
  }
  getContextBase(context_id)->onForeignFunction(foreign_function_id, data_size);
}

void NullPlugin::onLog(uint64_t context_id) {
  if (registry_->proxy_on_log_ != nullptr) {
    registry_->proxy_on_log_(context_id);
    return;
  }
  getContextBase(context_id)->onLog();
}

uint64_t NullPlugin::onDone(uint64_t context_id) {
  if (registry_->proxy_on_done_ != nullptr) {
    return registry_->proxy_on_done_(context_id);
  }
  return getContextBase(context_id)->onDoneBase() ? 1 : 0;
}

void NullPlugin::onDelete(uint64_t context_id) {
  if (registry_->proxy_on_delete_ != nullptr) {
    registry_->proxy_on_delete_(context_id);
    return;
  }
  getContextBase(context_id)->onDelete();
  context_map_.erase(context_id);
}

namespace null_plugin {

RootContext *nullVmGetRoot(std::string_view root_id) {
  auto *null_vm = dynamic_cast<NullVm *>(current_context_->wasmVm());
  return dynamic_cast<NullPlugin *>(null_vm->plugin_.get())->getRoot(root_id);
}

Context *nullVmGetContext(uint32_t context_id) {
  auto *null_vm = dynamic_cast<NullVm *>(current_context_->wasmVm());
  return dynamic_cast<NullPlugin *>(null_vm->plugin_.get())->getContext(context_id);
}

RootContext *getRoot(std::string_view root_id) { return nullVmGetRoot(root_id); }

Context *getContext(uint32_t context_id) { return nullVmGetContext(context_id); }

} // namespace null_plugin
} // namespace proxy_wasm
