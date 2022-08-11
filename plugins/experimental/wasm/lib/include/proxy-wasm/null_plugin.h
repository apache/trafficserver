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

#include <memory>

#include "include/proxy-wasm/exports.h"
#include "include/proxy-wasm/null_vm_plugin.h"
#include "include/proxy-wasm/wasm.h"
#include "include/proxy-wasm/wasm_api_impl.h"

namespace proxy_wasm {

/**
 * Registry for Plugin implementation.
 */
struct NullPluginRegistry {
  void (*proxy_abi_version_0_1_0_)() = nullptr;
  void (*proxy_abi_version_0_2_0_)() = nullptr;
  void (*proxy_abi_version_0_2_1_)() = nullptr;
  void (*proxy_on_log_)(uint32_t context_id) = nullptr;
  uint32_t (*proxy_validate_configuration_)(uint32_t root_context_id,
                                            uint32_t plugin_configuration_size) = nullptr;
  void (*proxy_on_context_create_)(uint32_t context_id, uint32_t parent_context_id) = nullptr;
  uint32_t (*proxy_on_vm_start_)(uint32_t root_context_id,
                                 uint32_t vm_configuration_size) = nullptr;
  uint32_t (*proxy_on_configure_)(uint32_t root_context_id,
                                  uint32_t plugin_configuration_size) = nullptr;
  void (*proxy_on_tick_)(uint32_t context_id) = nullptr;
  void (*proxy_on_foreign_function_)(uint32_t context_id, uint32_t token,
                                     uint32_t data_size) = nullptr;
  uint32_t (*proxy_on_done_)(uint32_t context_id) = nullptr;
  void (*proxy_on_delete_)(uint32_t context_id) = nullptr;
  std::unordered_map<std::string, null_plugin::RootFactory> root_factories;
  std::unordered_map<std::string, null_plugin::ContextFactory> context_factories;
};

/**
 * Base class for all plugins, subclass to create a new plugin.
 * NB: this class must implement
 */
class NullPlugin : public NullVmPlugin {
public:
  using NewContextFnPtr = std::unique_ptr<ContextBase> (*)(uint32_t /* id */);

  explicit NullPlugin(NullPluginRegistry *registry) : registry_(registry) {}
  NullPlugin(const NullPlugin &other) : registry_(other.registry_) {}

#define _DECLARE_OVERRIDE(_t) void getFunction(std::string_view function_name, _t *f) override;
  FOR_ALL_WASM_VM_EXPORTS(_DECLARE_OVERRIDE)
#undef _DECLARE_OVERRIDE

  bool validateConfiguration(uint64_t root_context_id, uint64_t plugin_configuration_size);
  bool onStart(uint64_t root_context_id, uint64_t vm_configuration_size);
  bool onConfigure(uint64_t root_context_id, uint64_t plugin_configuration_size);
  void onTick(uint64_t root_context_id);
  void onQueueReady(uint64_t root_context_id, uint64_t token);
  void onForeignFunction(uint64_t root_context_id, uint64_t foreign_function_id,
                         uint64_t data_size);

  void onCreate(uint64_t context_id, uint64_t parent_context_id);

  uint64_t onNewConnection(uint64_t context_id);
  uint64_t onDownstreamData(uint64_t context_id, uint64_t data_length, uint64_t end_of_stream);
  uint64_t onUpstreamData(uint64_t context_id, uint64_t data_length, uint64_t end_of_stream);
  void onDownstreamConnectionClose(uint64_t context_id, uint64_t close_type);
  void onUpstreamConnectionClose(uint64_t context_id, uint64_t close_type);

  uint64_t onRequestHeaders(uint64_t context_id, uint64_t headers, uint64_t end_of_stream);
  uint64_t onRequestBody(uint64_t context_id, uint64_t body_buffer_length, uint64_t end_of_stream);
  uint64_t onRequestTrailers(uint64_t context_id, uint64_t trailers);
  uint64_t onRequestMetadata(uint64_t context_id, uint64_t elements);

  uint64_t onResponseHeaders(uint64_t context_id, uint64_t headers, uint64_t end_of_stream);
  uint64_t onResponseBody(uint64_t context_id, uint64_t body_buffer_length, uint64_t end_of_stream);
  uint64_t onResponseTrailers(uint64_t context_id, uint64_t trailers);
  uint64_t onResponseMetadata(uint64_t context_id, uint64_t elements);

  void onHttpCallResponse(uint64_t context_id, uint64_t token, uint64_t headers, uint64_t body_size,
                          uint64_t trailers);

  void onGrpcReceive(uint64_t context_id, uint64_t token, size_t body_size);
  void onGrpcClose(uint64_t context_id, uint64_t token, uint64_t status_code);
  void onGrpcReceiveInitialMetadata(uint64_t context_id, uint64_t token, uint64_t headers);
  void onGrpcReceiveTrailingMetadata(uint64_t context_id, uint64_t token, uint64_t trailers);

  void onLog(uint64_t context_id);
  uint64_t onDone(uint64_t context_id);
  void onDelete(uint64_t context_id);

  null_plugin::RootContext *getRoot(std::string_view root_id);
  null_plugin::Context *getContext(uint64_t context_id);

  void error(std::string_view message) { wasm_vm_->integration()->error(message); }

  null_plugin::Context *ensureContext(uint64_t context_id, uint64_t root_context_id);
  null_plugin::RootContext *ensureRootContext(uint64_t context_id);
  null_plugin::RootContext *getRootContext(uint64_t context_id);
  null_plugin::ContextBase *getContextBase(uint64_t context_id);

private:
  NullPluginRegistry *registry_{};
  std::unordered_map<std::string, null_plugin::RootContext *> root_context_map_;
  std::unordered_map<int64_t, std::unique_ptr<null_plugin::ContextBase>> context_map_;
};

#define PROXY_WASM_NULL_PLUGIN_REGISTRY                                                            \
  extern NullPluginRegistry *context_registry_;                                                    \
  struct RegisterContextFactory {                                                                  \
    explicit RegisterContextFactory(null_plugin::ContextFactory context_factory,                   \
                                    null_plugin::RootFactory root_factory,                         \
                                    std::string_view root_id = "") {                               \
      if (!context_registry_) {                                                                    \
        context_registry_ = new NullPluginRegistry;                                                \
      }                                                                                            \
      context_registry_->context_factories[std::string(root_id)] = context_factory;                \
      context_registry_->root_factories[std::string(root_id)] = root_factory;                      \
    }                                                                                              \
    explicit RegisterContextFactory(null_plugin::ContextFactory context_factory,                   \
                                    std::string_view root_id = "") {                               \
      if (!context_registry_) {                                                                    \
        context_registry_ = new NullPluginRegistry;                                                \
      }                                                                                            \
      context_registry_->context_factories[std::string(root_id)] = context_factory;                \
    }                                                                                              \
    explicit RegisterContextFactory(null_plugin::RootFactory root_factory,                         \
                                    std::string_view root_id = "") {                               \
      if (!context_registry_) {                                                                    \
        context_registry_ = new NullPluginRegistry;                                                \
      }                                                                                            \
      context_registry_->root_factories[std::string(root_id)] = root_factory;                      \
    }                                                                                              \
  };

#define START_WASM_PLUGIN(_name)                                                                   \
  namespace proxy_wasm {                                                                           \
  namespace null_plugin {                                                                          \
  namespace _name {                                                                                \
  PROXY_WASM_NULL_PLUGIN_REGISTRY

#define END_WASM_PLUGIN                                                                            \
  }                                                                                                \
  }                                                                                                \
  }

#define WASM_EXPORT(_t, _f, _a)                                                                    \
  _t _f _a;                                                                                        \
  static int register_export_##_f() {                                                              \
    if (!context_registry_) {                                                                      \
      context_registry_ = new NullPluginRegistry;                                                  \
    }                                                                                              \
    context_registry_->_f##_ = _f;                                                                 \
    return 0;                                                                                      \
  };                                                                                               \
  static int register_export_##_f##_ = register_export_##_f();                                     \
  _t _f _a

} // namespace proxy_wasm
