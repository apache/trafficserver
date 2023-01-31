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

#include "include/proxy-wasm/v8.h"

#include <cassert>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "include/proxy-wasm/limits.h"

#include "include/v8-version.h"
#include "include/v8.h"
#include "src/flags/flags.h"
#include "src/wasm/c-api.h"
#include "wasm-api/wasm.hh"

namespace proxy_wasm {
namespace v8 {

wasm::Engine *engine() {
  static std::once_flag init;
  static wasm::own<wasm::Engine> engine;

  std::call_once(init, []() {
    ::v8::internal::v8_flags.liftoff = false;
    ::v8::internal::v8_flags.wasm_max_mem_pages =
        PROXY_WASM_HOST_MAX_WASM_MEMORY_SIZE_BYTES / PROXY_WASM_HOST_WASM_MEMORY_PAGE_SIZE_BYTES;
    ::v8::V8::EnableWebAssemblyTrapHandler(true);
    engine = wasm::Engine::make();
  });

  return engine.get();
}

struct FuncData {
  FuncData(std::string name) : name_(std::move(name)) {}

  std::string name_;
  wasm::own<wasm::Func> callback_;
  void *raw_func_{};
  WasmVm *vm_{};
};

using FuncDataPtr = std::unique_ptr<FuncData>;

class V8 : public WasmVm {
public:
  V8() = default;

  // WasmVm
  std::string_view getEngineName() override { return "v8"; }

  bool load(std::string_view bytecode, std::string_view precompiled,
            const std::unordered_map<uint32_t, std::string> &function_names) override;
  std::string_view getPrecompiledSectionName() override;
  bool link(std::string_view debug_name) override;

  Cloneable cloneable() override { return Cloneable::CompiledBytecode; }
  std::unique_ptr<WasmVm> clone() override;

  uint64_t getMemorySize() override;
  std::optional<std::string_view> getMemory(uint64_t pointer, uint64_t size) override;
  bool setMemory(uint64_t pointer, uint64_t size, const void *data) override;
  bool getWord(uint64_t pointer, Word *word) override;
  bool setWord(uint64_t pointer, Word word) override;
  size_t getWordSize() override { return sizeof(uint32_t); };

#define _REGISTER_HOST_FUNCTION(T)                                                                 \
  void registerCallback(std::string_view module_name, std::string_view function_name, T,           \
                        typename ConvertFunctionTypeWordToUint32<T>::type f) override {            \
    registerHostFunctionImpl(module_name, function_name, f);                                       \
  };
  FOR_ALL_WASM_VM_IMPORTS(_REGISTER_HOST_FUNCTION)
#undef _REGISTER_HOST_FUNCTION

#define _GET_MODULE_FUNCTION(T)                                                                    \
  void getFunction(std::string_view function_name, T *f) override {                                \
    getModuleFunctionImpl(function_name, f);                                                       \
  };
  FOR_ALL_WASM_VM_EXPORTS(_GET_MODULE_FUNCTION)
#undef _GET_MODULE_FUNCTION

  void terminate() override;
  bool usesWasmByteOrder() override { return true; }

private:
  wasm::own<wasm::Trap> trap(std::string message);

  std::string getFailMessage(std::string_view function_name, wasm::own<wasm::Trap> trap);

  template <typename... Args>
  void registerHostFunctionImpl(std::string_view module_name, std::string_view function_name,
                                void (*function)(Args...));

  template <typename R, typename... Args>
  void registerHostFunctionImpl(std::string_view module_name, std::string_view function_name,
                                R (*function)(Args...));

  template <typename... Args>
  void getModuleFunctionImpl(std::string_view function_name,
                             std::function<void(ContextBase *, Args...)> *function);

  template <typename R, typename... Args>
  void getModuleFunctionImpl(std::string_view function_name,
                             std::function<R(ContextBase *, Args...)> *function);

  wasm::own<wasm::Store> store_;
  wasm::own<wasm::Module> module_;
  wasm::own<wasm::Shared<wasm::Module>> shared_module_;
  wasm::own<wasm::Instance> instance_;
  wasm::own<wasm::Memory> memory_;
  wasm::own<wasm::Table> table_;

  std::unordered_map<std::string, FuncDataPtr> host_functions_;
  std::unordered_map<std::string, wasm::own<wasm::Func>> module_functions_;
  std::unordered_map<uint32_t, std::string> function_names_index_;
};

// Helper functions.

static std::string printValue(const wasm::Val &value) {
  switch (value.kind()) {
  case wasm::I32:
    return std::to_string(value.get<uint32_t>());
  case wasm::I64:
    return std::to_string(value.get<uint64_t>());
  case wasm::F32:
    return std::to_string(value.get<float>());
  case wasm::F64:
    return std::to_string(value.get<double>());
  default:
    return "unknown";
  }
}

static std::string printValues(const wasm::Val values[], size_t size) {
  if (size == 0) {
    return "";
  }

  std::string s;
  for (size_t i = 0; i < size; i++) {
    if (i != 0U) {
      s.append(", ");
    }
    s.append(printValue(values[i]));
  }
  return s;
}

static const char *printValKind(wasm::ValKind kind) {
  switch (kind) {
  case wasm::I32:
    return "i32";
  case wasm::I64:
    return "i64";
  case wasm::F32:
    return "f32";
  case wasm::F64:
    return "f64";
  case wasm::ANYREF:
    return "anyref";
  case wasm::FUNCREF:
    return "funcref";
  default:
    return "unknown";
  }
}

static std::string printValTypes(const wasm::ownvec<wasm::ValType> &types) {
  if (types.size() == 0) {
    return "void";
  }

  std::string s;
  s.reserve(types.size() * 8 /* max size + " " */ - 1);
  for (size_t i = 0; i < types.size(); i++) {
    if (i != 0U) {
      s.append(" ");
    }
    s.append(printValKind(types[i]->kind()));
  }
  return s;
}

static bool equalValTypes(const wasm::ownvec<wasm::ValType> &left,
                          const wasm::ownvec<wasm::ValType> &right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (size_t i = 0; i < left.size(); i++) {
    if (left[i]->kind() != right[i]->kind()) {
      return false;
    }
  }
  return true;
}

// Template magic.

template <typename T> struct ConvertWordType {
  using type = T; // NOLINT(readability-identifier-naming)
};
template <> struct ConvertWordType<Word> {
  using type = uint32_t; // NOLINT(readability-identifier-naming)
};

template <typename T> wasm::Val makeVal(T t) { return wasm::Val::make(t); }
template <> wasm::Val makeVal(Word t) { return wasm::Val::make(static_cast<uint32_t>(t.u64_)); }

template <typename T> constexpr auto convertArgToValKind();
template <> constexpr auto convertArgToValKind<Word>() { return wasm::I32; };
template <> constexpr auto convertArgToValKind<uint32_t>() { return wasm::I32; };
template <> constexpr auto convertArgToValKind<int64_t>() { return wasm::I64; };
template <> constexpr auto convertArgToValKind<uint64_t>() { return wasm::I64; };
template <> constexpr auto convertArgToValKind<double>() { return wasm::F64; };

template <typename T, std::size_t... I>
constexpr auto convertArgsTupleToValTypesImpl(std::index_sequence<I...> /*comptime*/) {
  return wasm::ownvec<wasm::ValType>::make(
      wasm::ValType::make(convertArgToValKind<typename std::tuple_element<I, T>::type>())...);
}

template <typename T> constexpr auto convertArgsTupleToValTypes() {
  return convertArgsTupleToValTypesImpl<T>(std::make_index_sequence<std::tuple_size<T>::value>());
}

template <typename T, typename U, std::size_t... I>
constexpr T convertValTypesToArgsTupleImpl(const U &arr, std::index_sequence<I...> /*comptime*/) {
  return std::make_tuple(
      (arr[I]
           .template get<
               typename ConvertWordType<typename std::tuple_element<I, T>::type>::type>())...);
}

template <typename T, typename U> constexpr T convertValTypesToArgsTuple(const U &arr) {
  return convertValTypesToArgsTupleImpl<T>(arr,
                                           std::make_index_sequence<std::tuple_size<T>::value>());
}

// V8 implementation.

bool V8::load(std::string_view bytecode, std::string_view precompiled,
              const std::unordered_map<uint32_t, std::string> &function_names) {
  store_ = wasm::Store::make(engine());
  if (store_ == nullptr) {
    return false;
  }

  if (!precompiled.empty()) {
    auto vec = wasm::vec<byte_t>::make_uninitialized(precompiled.size());
    ::memcpy(vec.get(), precompiled.data(), precompiled.size());
    module_ = wasm::Module::deserialize(store_.get(), vec);
    if (module_ == nullptr) {
      return false;
    }

  } else {
    auto vec = wasm::vec<byte_t>::make_uninitialized(bytecode.size());
    ::memcpy(vec.get(), bytecode.data(), bytecode.size());
    module_ = wasm::Module::make(store_.get(), vec);
    if (module_ == nullptr) {
      return false;
    }
  }

  shared_module_ = module_->share();
  if (shared_module_ == nullptr) {
    return false;
  }

  function_names_index_ = function_names;

  return true;
}

std::unique_ptr<WasmVm> V8::clone() {
  assert(shared_module_ != nullptr);

  auto clone = std::make_unique<V8>();
  if (clone == nullptr) {
    return nullptr;
  }

  clone->store_ = wasm::Store::make(engine());
  if (clone->store_ == nullptr) {
    return nullptr;
  }

  clone->module_ = wasm::Module::obtain(clone->store_.get(), shared_module_.get());
  if (clone->module_ == nullptr) {
    return nullptr;
  }

  auto *integration_clone = integration()->clone();
  if (integration_clone == nullptr) {
    return nullptr;
  }
  clone->integration().reset(integration_clone);

  clone->function_names_index_ = function_names_index_;

  return clone;
}

#if defined(__linux__) && defined(__x86_64__)
#define WEE8_PLATFORM "linux_x86_64"
#else
#define WEE8_PLATFORM ""
#endif

std::string_view V8::getPrecompiledSectionName() {
  static const auto name =
      sizeof(WEE8_PLATFORM) - 1 > 0
          ? ("precompiled_wee8_v" + std::to_string(V8_MAJOR_VERSION) + "." +
             std::to_string(V8_MINOR_VERSION) + "." + std::to_string(V8_BUILD_NUMBER) + "." +
             std::to_string(V8_PATCH_LEVEL) + "_" + WEE8_PLATFORM)
          : "";
  return name;
}

bool V8::link(std::string_view /*debug_name*/) {
  assert(module_ != nullptr);

  const auto import_types = module_.get()->imports();
  std::vector<const wasm::Extern *> imports;

  for (size_t i = 0; i < import_types.size(); i++) {
    std::string_view module(import_types[i]->module().get(), import_types[i]->module().size());
    std::string_view name(import_types[i]->name().get(), import_types[i]->name().size());
    const auto *import_type = import_types[i]->type();

    switch (import_type->kind()) {

    case wasm::EXTERN_FUNC: {
      auto it = host_functions_.find(std::string(module) + "." + std::string(name));
      if (it == host_functions_.end()) {
        fail(FailState::UnableToInitializeCode,
             std::string("Failed to load Wasm module due to a missing import: ") +
                 std::string(module) + "." + std::string(name));
        return false;
      }
      auto *func = it->second->callback_.get();
      if (!equalValTypes(import_type->func()->params(), func->type()->params()) ||
          !equalValTypes(import_type->func()->results(), func->type()->results())) {
        fail(FailState::UnableToInitializeCode,
             std::string("Failed to load Wasm module due to an import type mismatch: ") +
                 std::string(module) + "." + std::string(name) +
                 ", want: " + printValTypes(import_type->func()->params()) + " -> " +
                 printValTypes(import_type->func()->results()) +
                 ", but host exports: " + printValTypes(func->type()->params()) + " -> " +
                 printValTypes(func->type()->results()));
        return false;
      }
      imports.push_back(func);
    } break;

    case wasm::EXTERN_GLOBAL: {
      // TODO(PiotrSikora): add support when/if needed.
      fail(FailState::UnableToInitializeCode,
           "Failed to load Wasm module due to a missing import: " + std::string(module) + "." +
               std::string(name));
      return false;
    } break;

    case wasm::EXTERN_MEMORY: {
      assert(memory_ == nullptr);
      auto type = wasm::MemoryType::make(import_type->memory()->limits());
      if (type == nullptr) {
        return false;
      }
      memory_ = wasm::Memory::make(store_.get(), type.get());
      if (memory_ == nullptr) {
        return false;
      }
      imports.push_back(memory_.get());
    } break;

    case wasm::EXTERN_TABLE: {
      assert(table_ == nullptr);
      auto type =
          wasm::TableType::make(wasm::ValType::make(import_type->table()->element()->kind()),
                                import_type->table()->limits());
      if (type == nullptr) {
        return false;
      }
      table_ = wasm::Table::make(store_.get(), type.get());
      if (table_ == nullptr) {
        return false;
      }
      imports.push_back(table_.get());
    } break;
    }
  }

  if (import_types.size() != imports.size()) {
    return false;
  }

  instance_ = wasm::Instance::make(store_.get(), module_.get(), imports.data());
  if (instance_ == nullptr) {
    fail(FailState::UnableToInitializeCode, "Failed to create new Wasm instance");
    return false;
  }

  const auto export_types = module_.get()->exports();
  const auto exports = instance_.get()->exports();
  assert(export_types.size() == exports.size());

  for (size_t i = 0; i < export_types.size(); i++) {
    std::string_view name(export_types[i]->name().get(), export_types[i]->name().size());
    const auto *export_type = export_types[i]->type();
    auto *export_item = exports[i].get();
    assert(export_type->kind() == export_item->kind());

    switch (export_type->kind()) {

    case wasm::EXTERN_FUNC: {
      assert(export_item->func() != nullptr);
      module_functions_.insert_or_assign(std::string(name), export_item->func()->copy());
    } break;

    case wasm::EXTERN_GLOBAL: {
      // TODO(PiotrSikora): add support when/if needed.
    } break;

    case wasm::EXTERN_MEMORY: {
      assert(export_item->memory() != nullptr);
      assert(memory_ == nullptr);
      memory_ = exports[i]->memory()->copy();
      if (memory_ == nullptr) {
        return false;
      }
    } break;

    case wasm::EXTERN_TABLE: {
      // TODO(PiotrSikora): add support when/if needed.
    } break;
    }
  }

  return true;
}

uint64_t V8::getMemorySize() { return memory_->data_size(); }

std::optional<std::string_view> V8::getMemory(uint64_t pointer, uint64_t size) {
  assert(memory_ != nullptr);
  // Make sure we're operating in a wasm32 memory space.
  if (pointer > UINT32_MAX || size > UINT32_MAX || pointer + size > UINT32_MAX) {
    return std::nullopt;
  }
  if (pointer + size > memory_->data_size()) {
    return std::nullopt;
  }
  return std::string_view(memory_->data() + pointer, size);
}

bool V8::setMemory(uint64_t pointer, uint64_t size, const void *data) {
  assert(memory_ != nullptr);
  // Make sure we're operating in a wasm32 memory space.
  if (pointer > UINT32_MAX || size > UINT32_MAX || pointer + size > UINT32_MAX) {
    return false;
  }
  if (pointer + size > memory_->data_size()) {
    return false;
  }
  ::memcpy(memory_->data() + pointer, data, size);
  return true;
}

bool V8::getWord(uint64_t pointer, Word *word) {
  constexpr auto size = sizeof(uint32_t);
  // Make sure we're operating in a wasm32 memory space.
  if (pointer > UINT32_MAX || pointer + size > UINT32_MAX) {
    return false;
  }
  if (pointer + size > memory_->data_size()) {
    return false;
  }
  uint32_t word32;
  ::memcpy(&word32, memory_->data() + pointer, size);
  word->u64_ = wasmtoh(word32, true);
  return true;
}

bool V8::setWord(uint64_t pointer, Word word) {
  constexpr auto size = sizeof(uint32_t);
  // Make sure we're operating in a wasm32 memory space.
  if (pointer > UINT32_MAX || pointer + size > UINT32_MAX) {
    return false;
  }
  if (pointer + size > memory_->data_size()) {
    return false;
  }
  uint32_t word32 = htowasm(word.u32(), true);
  ::memcpy(memory_->data() + pointer, &word32, size);
  return true;
}

wasm::own<wasm::Trap> V8::trap(std::string message) {
  return wasm::Trap::make(store_.get(), wasm::Message::make(std::move(message)));
}

template <typename... Args>
void V8::registerHostFunctionImpl(std::string_view module_name, std::string_view function_name,
                                  void (*function)(Args...)) {
  auto data =
      std::make_unique<FuncData>(std::string(module_name) + "." + std::string(function_name));
  auto type = wasm::FuncType::make(convertArgsTupleToValTypes<std::tuple<Args...>>(),
                                   convertArgsTupleToValTypes<std::tuple<>>());
  auto func = wasm::Func::make(
      store_.get(), type.get(),
      [](void *data, const wasm::Val params[], wasm::Val /*results*/[]) -> wasm::own<wasm::Trap> {
        auto *func_data = reinterpret_cast<FuncData *>(data);
        const bool log = func_data->vm_->cmpLogLevel(LogLevel::trace);
        if (log) {
          func_data->vm_->integration()->trace("[vm->host] " + func_data->name_ + "(" +
                                               printValues(params, sizeof...(Args)) + ")");
        }
        if (!func_data->vm_->isHostFunctionAllowed(func_data->name_)) {
          return dynamic_cast<V8 *>(func_data->vm_)->trap("restricted_callback");
        }
        auto args = convertValTypesToArgsTuple<std::tuple<Args...>>(params);
        auto function = reinterpret_cast<void (*)(Args...)>(func_data->raw_func_);
        std::apply(function, args);
        if (log) {
          func_data->vm_->integration()->trace("[vm<-host] " + func_data->name_ + " return: void");
        }
        return nullptr;
      },
      data.get());

  data->vm_ = this;
  data->callback_ = std::move(func);
  data->raw_func_ = reinterpret_cast<void *>(function);
  host_functions_.insert_or_assign(std::string(module_name) + "." + std::string(function_name),
                                   std::move(data));
}

template <typename R, typename... Args>
void V8::registerHostFunctionImpl(std::string_view module_name, std::string_view function_name,
                                  R (*function)(Args...)) {
  auto data =
      std::make_unique<FuncData>(std::string(module_name) + "." + std::string(function_name));
  auto type = wasm::FuncType::make(convertArgsTupleToValTypes<std::tuple<Args...>>(),
                                   convertArgsTupleToValTypes<std::tuple<R>>());
  auto func = wasm::Func::make(
      store_.get(), type.get(),
      [](void *data, const wasm::Val params[], wasm::Val results[]) -> wasm::own<wasm::Trap> {
        auto *func_data = reinterpret_cast<FuncData *>(data);
        const bool log = func_data->vm_->cmpLogLevel(LogLevel::trace);
        if (log) {
          func_data->vm_->integration()->trace("[vm->host] " + func_data->name_ + "(" +
                                               printValues(params, sizeof...(Args)) + ")");
        }
        if (!func_data->vm_->isHostFunctionAllowed(func_data->name_)) {
          return dynamic_cast<V8 *>(func_data->vm_)->trap("restricted_callback");
        }
        auto args = convertValTypesToArgsTuple<std::tuple<Args...>>(params);
        auto function = reinterpret_cast<R (*)(Args...)>(func_data->raw_func_);
        R rvalue = std::apply(function, args);
        results[0] = makeVal(rvalue);
        if (log) {
          func_data->vm_->integration()->trace("[vm<-host] " + func_data->name_ +
                                               " return: " + std::to_string(rvalue));
        }
        return nullptr;
      },
      data.get());

  data->vm_ = this;
  data->callback_ = std::move(func);
  data->raw_func_ = reinterpret_cast<void *>(function);
  host_functions_.insert_or_assign(std::string(module_name) + "." + std::string(function_name),
                                   std::move(data));
}

template <typename... Args>
void V8::getModuleFunctionImpl(std::string_view function_name,
                               std::function<void(ContextBase *, Args...)> *function) {
  auto it = module_functions_.find(std::string(function_name));
  if (it == module_functions_.end()) {
    *function = nullptr;
    return;
  }
  const wasm::Func *func = it->second.get();
  auto arg_valtypes = convertArgsTupleToValTypes<std::tuple<Args...>>();
  auto result_valtypes = convertArgsTupleToValTypes<std::tuple<>>();
  if (!equalValTypes(func->type()->params(), arg_valtypes) ||
      !equalValTypes(func->type()->results(), result_valtypes)) {
    fail(FailState::UnableToInitializeCode,
         "Bad function signature for: " + std::string(function_name) +
             ", want: " + printValTypes(arg_valtypes) + " -> " + printValTypes(result_valtypes) +
             ", but the module exports: " + printValTypes(func->type()->params()) + " -> " +
             printValTypes(func->type()->results()));
    *function = nullptr;
    return;
  }
  *function = [func, function_name, this](ContextBase *context, Args... args) -> void {
    const bool log = cmpLogLevel(LogLevel::trace);
    SaveRestoreContext saved_context(context);
    wasm::own<wasm::Trap> trap = nullptr;

    // Workaround for MSVC++ not supporting zero-sized arrays.
    if constexpr (sizeof...(args) > 0) {
      wasm::Val params[] = {makeVal(args)...};
      if (log) {
        integration()->trace("[host->vm] " + std::string(function_name) + "(" +
                             printValues(params, sizeof...(Args)) + ")");
      }
      trap = func->call(params, nullptr);
    } else {
      if (log) {
        integration()->trace("[host->vm] " + std::string(function_name) + "()");
      }
      trap = func->call(nullptr, nullptr);
    }

    if (trap) {
      fail(FailState::RuntimeError, getFailMessage(std::string(function_name), std::move(trap)));
      return;
    }
    if (log) {
      integration()->trace("[host<-vm] " + std::string(function_name) + " return: void");
    }
  };
}

template <typename R, typename... Args>
void V8::getModuleFunctionImpl(std::string_view function_name,
                               std::function<R(ContextBase *, Args...)> *function) {
  auto it = module_functions_.find(std::string(function_name));
  if (it == module_functions_.end()) {
    *function = nullptr;
    return;
  }
  const wasm::Func *func = it->second.get();
  auto arg_valtypes = convertArgsTupleToValTypes<std::tuple<Args...>>();
  auto result_valtypes = convertArgsTupleToValTypes<std::tuple<R>>();
  if (!equalValTypes(func->type()->params(), arg_valtypes) ||
      !equalValTypes(func->type()->results(), result_valtypes)) {
    fail(FailState::UnableToInitializeCode,
         "Bad function signature for: " + std::string(function_name) +
             ", want: " + printValTypes(arg_valtypes) + " -> " + printValTypes(result_valtypes) +
             ", but the module exports: " + printValTypes(func->type()->params()) + " -> " +
             printValTypes(func->type()->results()));
    *function = nullptr;
    return;
  }
  *function = [func, function_name, this](ContextBase *context, Args... args) -> R {
    const bool log = cmpLogLevel(LogLevel::trace);
    SaveRestoreContext saved_context(context);
    wasm::Val results[1];
    wasm::own<wasm::Trap> trap = nullptr;

    // Workaround for MSVC++ not supporting zero-sized arrays.
    if constexpr (sizeof...(args) > 0) {
      wasm::Val params[] = {makeVal(args)...};
      if (log) {
        integration()->trace("[host->vm] " + std::string(function_name) + "(" +
                             printValues(params, sizeof...(Args)) + ")");
      }
      trap = func->call(params, results);
    } else {
      if (log) {
        integration()->trace("[host->vm] " + std::string(function_name) + "()");
      }
      trap = func->call(nullptr, results);
    }

    if (trap) {
      fail(FailState::RuntimeError, getFailMessage(std::string(function_name), std::move(trap)));
      return R{};
    }
    R rvalue = results[0].get<typename ConvertWordTypeToUint32<R>::type>();
    if (log) {
      integration()->trace("[host<-vm] " + std::string(function_name) +
                           " return: " + std::to_string(rvalue));
    }
    return rvalue;
  };
}

void V8::terminate() {
  auto *store_impl = reinterpret_cast<wasm::StoreImpl *>(store_.get());
  auto *isolate = store_impl->isolate();
  isolate->TerminateExecution();
  while (isolate->IsExecutionTerminating()) {
    std::this_thread::yield();
  }
}

std::string V8::getFailMessage(std::string_view function_name, wasm::own<wasm::Trap> trap) {
  auto message = "Function: " + std::string(function_name) + " failed: ";
  message += std::string(trap->message().get(), trap->message().size());

  if (function_names_index_.empty()) {
    return message;
  }

  auto trace = trap->trace();
  message += "\nProxy-Wasm plugin in-VM backtrace:";
  for (size_t i = 0; i < trace.size(); ++i) {
    auto *frame = trace[i].get();
    std::ostringstream oss;
    oss << std::setw(3) << std::setfill(' ') << std::to_string(i);
    message += "\n" + oss.str() + ": ";

    std::stringstream address;
    address << std::hex << frame->module_offset();
    message += " 0x" + address.str() + " - ";

    auto func_index = frame->func_index();
    auto it = function_names_index_.find(func_index);
    if (it != function_names_index_.end()) {
      message += it->second;
    } else {
      message += "unknown(function index:" + std::to_string(func_index) + ")";
    }
  }
  return message;
}

} // namespace v8

std::unique_ptr<WasmVm> createV8Vm() { return std::make_unique<v8::V8>(); }

} // namespace proxy_wasm
