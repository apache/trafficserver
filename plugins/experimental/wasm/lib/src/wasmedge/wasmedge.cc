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

#include "include/proxy-wasm/wasmedge.h"
#include "include/proxy-wasm/wasm_vm.h"
#include "src/wasmedge/types.h"

#include "wasmedge/wasmedge.h"

#include <array>
#include <cassert>
#include <cstring>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace proxy_wasm {
namespace WasmEdge {

// Helper templates to make values.
template <typename T> WasmEdge_Value makeVal(T t);
template <> WasmEdge_Value makeVal<Word>(Word t) {
  return WasmEdge_ValueGenI32(static_cast<int32_t>(t.u64_));
}
template <> WasmEdge_Value makeVal(uint32_t t) {
  return WasmEdge_ValueGenI32(static_cast<int32_t>(t));
}
template <> WasmEdge_Value makeVal(uint64_t t) {
  return WasmEdge_ValueGenI64(static_cast<int64_t>(t));
}
template <> WasmEdge_Value makeVal(double t) { return WasmEdge_ValueGenF64(t); }

// Helper function to print values.
std::string printValue(const WasmEdge_Value &value) {
  switch (value.Type) {
  case WasmEdge_ValType_I32:
    return std::to_string(WasmEdge_ValueGetI32(value));
  case WasmEdge_ValType_I64:
    return std::to_string(WasmEdge_ValueGetI64(value));
  case WasmEdge_ValType_F32:
    return std::to_string(WasmEdge_ValueGetF32(value));
  case WasmEdge_ValType_F64:
    return std::to_string(WasmEdge_ValueGetF64(value));
  default:
    return "unknown";
  }
}

std::string printValues(const WasmEdge_Value *values, size_t size) {
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

// Helper function to print valtype.
const char *printValType(WasmEdge_ValType kind) {
  switch (kind) {
  case WasmEdge_ValType_I32:
    return "i32";
  case WasmEdge_ValType_I64:
    return "i64";
  case WasmEdge_ValType_F32:
    return "f32";
  case WasmEdge_ValType_F64:
    return "f64";
  case WasmEdge_ValType_ExternRef:
    return "anyref";
  case WasmEdge_ValType_FuncRef:
    return "funcref";
  default:
    return "unknown";
  }
}

// Helper function to print valtype array.
std::string printValTypes(const WasmEdge_ValType *types, size_t size) {
  if (size == 0) {
    return "void";
  }

  std::string s;
  s.reserve(size * 8 /* max size + " " */ - 1);
  for (size_t i = 0; i < size; i++) {
    if (i != 0U) {
      s.append(" ");
    }
    s.append(printValType(types[i]));
  }
  return s;
}

template <typename T> struct ConvertWordType {
  using type = T; // NOLINT(readability-identifier-naming)
};
template <> struct ConvertWordType<Word> {
  using type = uint32_t; // NOLINT(readability-identifier-naming)
};

// Helper templates to convert arg to valtype.
template <typename T> enum WasmEdge_ValType convArgToValType();
template <> enum WasmEdge_ValType convArgToValType<Word>() { return WasmEdge_ValType_I32; }
template <> enum WasmEdge_ValType convArgToValType<uint32_t>() { return WasmEdge_ValType_I32; }
template <> enum WasmEdge_ValType convArgToValType<int64_t>() { return WasmEdge_ValType_I64; }
template <> enum WasmEdge_ValType convArgToValType<uint64_t>() { return WasmEdge_ValType_I64; }
template <> enum WasmEdge_ValType convArgToValType<double>() { return WasmEdge_ValType_F64; }

// Helper templates to convert valtype to arg.
template <typename T> T convValTypeToArg(WasmEdge_Value val);
template <> uint32_t convValTypeToArg<uint32_t>(WasmEdge_Value val) {
  return static_cast<uint32_t>(WasmEdge_ValueGetI32(val));
}
template <> Word convValTypeToArg<Word>(WasmEdge_Value val) { return WasmEdge_ValueGetI32(val); }
template <> int64_t convValTypeToArg<int64_t>(WasmEdge_Value val) {
  return WasmEdge_ValueGetI64(val);
}
template <> uint64_t convValTypeToArg<uint64_t>(WasmEdge_Value val) {
  return static_cast<uint64_t>(WasmEdge_ValueGetI64(val));
}
template <> double convValTypeToArg<double>(WasmEdge_Value val) {
  return WasmEdge_ValueGetF64(val);
}

// Helper templates to convert valtypes to args tuple.
template <typename T, std::size_t... I>
constexpr T convValTypesToArgsTupleImpl(const WasmEdge_Value *arr,
                                        std::index_sequence<I...> /*comptime*/) {
  return std::make_tuple(
      convValTypeToArg<typename ConvertWordType<std::tuple_element_t<I, T>>::type>(arr[I])...);
}

template <typename T, typename Is = std::make_index_sequence<std::tuple_size<T>::value>>
constexpr T convValTypesToArgsTuple(const WasmEdge_Value *arr) {
  return convValTypesToArgsTupleImpl<T>(arr, Is());
}

// Helper templates to convert args tuple to valtypes.
template <typename T, std::size_t... I>
uint32_t convArgsTupleToValTypesImpl(std::vector<enum WasmEdge_ValType> &types,
                                     std::index_sequence<I...> /*comptime*/) {
  auto size = std::tuple_size<T>::value;
  if (size > 0) {
    auto ps = std::array<enum WasmEdge_ValType, std::tuple_size<T>::value>{
        convArgToValType<typename std::tuple_element<I, T>::type>()...};
    types.resize(size);
    std::copy_n(ps.data(), size, types.data());
  }
  return size;
}

template <typename T, typename Is = std::make_index_sequence<std::tuple_size<T>::value>>
uint32_t convArgsTupleToValTypes(std::vector<enum WasmEdge_ValType> &types) {
  return convArgsTupleToValTypesImpl<T>(types, Is());
}

// Helper templates to create WasmEdge_FunctionTypeContext.
template <typename R, typename T> WasmEdge_FunctionTypeContext *newWasmEdgeFuncType() {
  std::vector<enum WasmEdge_ValType> params;
  std::vector<enum WasmEdge_ValType> returns;
  uint32_t param_nums = convArgsTupleToValTypes<T>(params);
  uint32_t return_nums = convArgsTupleToValTypes<std::tuple<R>>(returns);
  auto *ftype = WasmEdge_FunctionTypeCreate(params.data(), param_nums, returns.data(), return_nums);
  return ftype;
}

template <typename T> WasmEdge_FunctionTypeContext *newWasmEdgeFuncType() {
  std::vector<enum WasmEdge_ValType> params;
  uint32_t param_nums = convArgsTupleToValTypes<T>(params);
  auto *ftype = WasmEdge_FunctionTypeCreate(params.data(), param_nums, nullptr, 0);
  return ftype;
}

struct HostFuncData {
  HostFuncData(const std::string_view modname, const std::string_view name)
      : modname_(modname), name_(name), callback_(nullptr), raw_func_(nullptr), vm_(nullptr) {}
  ~HostFuncData() = default;

  std::string modname_, name_;
  WasmEdge_HostFunc_t callback_;
  void *raw_func_;
  WasmVm *vm_;
};

using HostFuncDataPtr = std::unique_ptr<HostFuncData>;

struct HostModuleData {
  HostModuleData(const std::string_view modname) {
    cxt_ = WasmEdge_ModuleInstanceCreate(WasmEdge_StringWrap(modname.data(), modname.length()));
  }
  ~HostModuleData() { WasmEdge_ModuleInstanceDelete(cxt_); }

  WasmEdge_ModuleInstanceContext *cxt_;
};

using HostModuleDataPtr = std::unique_ptr<HostModuleData>;

class WasmEdge : public WasmVm {
public:
  WasmEdge() {
    loader_ = WasmEdge_LoaderCreate(nullptr);
    validator_ = WasmEdge_ValidatorCreate(nullptr);
    executor_ = WasmEdge_ExecutorCreate(nullptr, nullptr);
    store_ = nullptr;
    ast_module_ = nullptr;
    module_ = nullptr;
    memory_ = nullptr;
  }

  std::string_view getEngineName() override { return "wasmedge"; }
  std::string_view getPrecompiledSectionName() override { return ""; }

  Cloneable cloneable() override { return Cloneable::NotCloneable; }
  std::unique_ptr<WasmVm> clone() override { return nullptr; }

  bool load(std::string_view bytecode, std::string_view precompiled,
            const std::unordered_map<uint32_t, std::string> &function_names) override;
  bool link(std::string_view debug_name) override;
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
private:
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

  void terminate() override {}

  WasmEdgeLoaderPtr loader_;
  WasmEdgeValidatorPtr validator_;
  WasmEdgeExecutorPtr executor_;
  WasmEdgeStorePtr store_;
  WasmEdgeASTModulePtr ast_module_;
  WasmEdgeModulePtr module_;
  WasmEdge_MemoryInstanceContext *memory_;

  std::unordered_map<std::string, HostFuncDataPtr> host_functions_;
  std::unordered_map<std::string, HostModuleDataPtr> host_modules_;
  std::unordered_set<std::string> module_functions_;
};

bool WasmEdge::load(std::string_view bytecode, std::string_view /*precompiled*/,
                    const std::unordered_map<uint32_t, std::string> & /*function_names*/) {
  WasmEdge_ASTModuleContext *mod = nullptr;
  WasmEdge_Result res = WasmEdge_LoaderParseFromBuffer(
      loader_.get(), &mod, reinterpret_cast<const uint8_t *>(bytecode.data()), bytecode.size());
  if (!WasmEdge_ResultOK(res)) {
    return false;
  }
  res = WasmEdge_ValidatorValidate(validator_.get(), mod);
  if (!WasmEdge_ResultOK(res)) {
    WasmEdge_ASTModuleDelete(mod);
    return false;
  }
  ast_module_ = mod;
  return true;
}

bool WasmEdge::link(std::string_view /*debug_name*/) {
  assert(ast_module_ != nullptr);

  // Create store and register imports.
  if (store_ == nullptr) {
    store_ = WasmEdge_StoreCreate();
  }
  if (store_ == nullptr) {
    return false;
  }
  WasmEdge_Result res;
  for (auto &&it : host_modules_) {
    res = WasmEdge_ExecutorRegisterImport(executor_.get(), store_.get(), it.second->cxt_);
    if (!WasmEdge_ResultOK(res)) {
      fail(FailState::UnableToInitializeCode,
           std::string("Failed to link Wasm module due to import: ") + it.first);
      return false;
    }
  }
  // Instantiate module.
  WasmEdge_ModuleInstanceContext *mod = nullptr;
  res = WasmEdge_ExecutorInstantiate(executor_.get(), &mod, store_.get(), ast_module_.get());
  if (!WasmEdge_ResultOK(res)) {
    fail(FailState::UnableToInitializeCode,
         std::string("Failed to link Wasm module: ") + std::string(WasmEdge_ResultGetMessage(res)));
    return false;
  }
  // Get the function and memory exports.
  uint32_t memory_num = WasmEdge_ModuleInstanceListMemoryLength(mod);
  if (memory_num > 0) {
    WasmEdge_String name;
    WasmEdge_ModuleInstanceListMemory(mod, &name, 1);
    memory_ = WasmEdge_ModuleInstanceFindMemory(mod, name);
    if (memory_ == nullptr) {
      WasmEdge_ModuleInstanceDelete(mod);
      return false;
    }
  }
  uint32_t func_num = WasmEdge_ModuleInstanceListFunctionLength(mod);
  if (func_num > 0) {
    std::vector<WasmEdge_String> names(func_num);
    WasmEdge_ModuleInstanceListFunction(mod, &names[0], func_num);
    for (auto i = 0; i < func_num; i++) {
      module_functions_.insert(std::string(names[i].Buf, names[i].Length));
    }
  }
  module_ = mod;
  return true;
}

uint64_t WasmEdge::getMemorySize() {
  if (memory_ != nullptr) {
    return 65536ULL * WasmEdge_MemoryInstanceGetPageSize(memory_);
  }
  return 0;
}

std::optional<std::string_view> WasmEdge::getMemory(uint64_t pointer, uint64_t size) {
  char *ptr = reinterpret_cast<char *>(WasmEdge_MemoryInstanceGetPointer(memory_, pointer, size));
  if (ptr == nullptr) {
    return std::nullopt;
  }
  return std::string_view(ptr, size);
}

bool WasmEdge::setMemory(uint64_t pointer, uint64_t size, const void *data) {
  auto res = WasmEdge_MemoryInstanceSetData(memory_, reinterpret_cast<const uint8_t *>(data),
                                            pointer, size);
  return WasmEdge_ResultOK(res);
}

bool WasmEdge::getWord(uint64_t pointer, Word *word) {
  constexpr auto size = sizeof(uint32_t);
  uint32_t word32;
  auto res =
      WasmEdge_MemoryInstanceGetData(memory_, reinterpret_cast<uint8_t *>(&word32), pointer, size);
  if (WasmEdge_ResultOK(res)) {
    word->u64_ = word32;
    return true;
  }
  return false;
}

bool WasmEdge::setWord(uint64_t pointer, Word word) {
  constexpr auto size = sizeof(uint32_t);
  uint32_t word32 = word.u32();
  auto res =
      WasmEdge_MemoryInstanceSetData(memory_, reinterpret_cast<uint8_t *>(&word32), pointer, size);
  return WasmEdge_ResultOK(res);
}

template <typename... Args>
void WasmEdge::registerHostFunctionImpl(std::string_view module_name,
                                        std::string_view function_name, void (*function)(Args...)) {
  auto it = host_modules_.find(std::string(module_name));
  if (it == host_modules_.end()) {
    host_modules_.emplace(module_name, std::make_unique<HostModuleData>(module_name));
    it = host_modules_.find(std::string(module_name));
  }

  auto data = std::make_unique<HostFuncData>(module_name, function_name);
  auto *func_type = newWasmEdgeFuncType<std::tuple<Args...>>();
  data->vm_ = this;
  data->raw_func_ = reinterpret_cast<void *>(function);
  data->callback_ = [](void *data, WasmEdge_MemoryInstanceContext * /*MemCxt*/,
                       const WasmEdge_Value *Params,
                       WasmEdge_Value * /*Returns*/) -> WasmEdge_Result {
    auto *func_data = reinterpret_cast<HostFuncData *>(data);
    const bool log = func_data->vm_->cmpLogLevel(LogLevel::trace);
    if (log) {
      func_data->vm_->integration()->trace("[vm->host] " + func_data->modname_ + "." +
                                           func_data->name_ + "(" +
                                           printValues(Params, sizeof...(Args)) + ")");
    }
    auto args = convValTypesToArgsTuple<std::tuple<Args...>>(Params);
    auto fn = reinterpret_cast<void (*)(Args...)>(func_data->raw_func_);
    std::apply(fn, args);
    if (log) {
      func_data->vm_->integration()->trace("[vm<-host] " + func_data->modname_ + "." +
                                           func_data->name_ + " return: void");
    }
    return WasmEdge_Result_Success;
  };

  auto *hostfunc_cxt = WasmEdge_FunctionInstanceCreate(func_type, data->callback_, data.get(), 0);
  WasmEdge_FunctionTypeDelete(func_type);
  if (hostfunc_cxt == nullptr) {
    fail(FailState::MissingFunction, "Failed to allocate host function instance");
    return;
  }

  WasmEdge_ModuleInstanceAddFunction(
      it->second->cxt_, WasmEdge_StringWrap(function_name.data(), function_name.length()),
      hostfunc_cxt);
  host_functions_.insert_or_assign(std::string(module_name) + "." + std::string(function_name),
                                   std::move(data));
}

template <typename R, typename... Args>
void WasmEdge::registerHostFunctionImpl(std::string_view module_name,
                                        std::string_view function_name, R (*function)(Args...)) {
  auto it = host_modules_.find(std::string(module_name));
  if (it == host_modules_.end()) {
    host_modules_.emplace(module_name, std::make_unique<HostModuleData>(module_name));
    it = host_modules_.find(std::string(module_name));
  }

  auto data = std::make_unique<HostFuncData>(module_name, function_name);
  auto *func_type = newWasmEdgeFuncType<R, std::tuple<Args...>>();
  data->vm_ = this;
  data->raw_func_ = reinterpret_cast<void *>(function);
  data->callback_ = [](void *data, WasmEdge_MemoryInstanceContext * /*MemCxt*/,
                       const WasmEdge_Value *Params, WasmEdge_Value *Returns) -> WasmEdge_Result {
    auto *func_data = reinterpret_cast<HostFuncData *>(data);
    const bool log = func_data->vm_->cmpLogLevel(LogLevel::trace);
    if (log) {
      func_data->vm_->integration()->trace("[vm->host] " + func_data->modname_ + "." +
                                           func_data->name_ + "(" +
                                           printValues(Params, sizeof...(Args)) + ")");
    }
    auto args = convValTypesToArgsTuple<std::tuple<Args...>>(Params);
    auto fn = reinterpret_cast<R (*)(Args...)>(func_data->raw_func_);
    R res = std::apply(fn, args);
    Returns[0] = makeVal<R>(res);
    if (log) {
      func_data->vm_->integration()->trace("[vm<-host] " + func_data->modname_ + "." +
                                           func_data->name_ + " return: " + std::to_string(res));
    }
    return WasmEdge_Result_Success;
  };

  auto *hostfunc_cxt = WasmEdge_FunctionInstanceCreate(func_type, data->callback_, data.get(), 0);
  WasmEdge_FunctionTypeDelete(func_type);
  if (hostfunc_cxt == nullptr) {
    fail(FailState::MissingFunction, "Failed to allocate host function instance");
    return;
  }

  WasmEdge_ModuleInstanceAddFunction(
      it->second->cxt_, WasmEdge_StringWrap(function_name.data(), function_name.length()),
      hostfunc_cxt);
  host_functions_.insert_or_assign(std::string(module_name) + "." + std::string(function_name),
                                   std::move(data));
}

template <typename... Args>
void WasmEdge::getModuleFunctionImpl(std::string_view function_name,
                                     std::function<void(ContextBase *, Args...)> *function) {
  auto *func_cxt = WasmEdge_ModuleInstanceFindFunction(
      module_.get(), WasmEdge_StringWrap(function_name.data(), function_name.length()));
  if (!func_cxt) {
    *function = nullptr;
    return;
  }

  std::vector<enum WasmEdge_ValType> exp_args;
  std::vector<enum WasmEdge_ValType> exp_returns;
  convArgsTupleToValTypes<std::tuple<Args...>>(exp_args);
  convArgsTupleToValTypes<std::tuple<>>(exp_returns);
  const auto *functype_cxt = WasmEdge_FunctionInstanceGetFunctionType(func_cxt);
  std::vector<enum WasmEdge_ValType> act_args(
      WasmEdge_FunctionTypeGetParametersLength(functype_cxt));
  std::vector<enum WasmEdge_ValType> act_returns(
      WasmEdge_FunctionTypeGetReturnsLength(functype_cxt));
  WasmEdge_FunctionTypeGetParameters(functype_cxt, act_args.data(), act_args.size());
  WasmEdge_FunctionTypeGetReturns(functype_cxt, act_returns.data(), act_returns.size());

  if (exp_args != act_args || exp_returns != act_returns) {
    fail(FailState::UnableToInitializeCode,
         "Bad function signature for: " + std::string(function_name) +
             ", want: " + printValTypes(exp_args.data(), exp_args.size()) + " -> " +
             printValTypes(exp_returns.data(), exp_returns.size()) +
             ", but the module exports: " + printValTypes(act_args.data(), act_args.size()) +
             " -> " + printValTypes(act_returns.data(), act_returns.size()));
    return;
  }

  *function = [function_name, func_cxt, this](ContextBase *context, Args... args) -> void {
    WasmEdge_Value params[] = {makeVal(args)...};
    const bool log = cmpLogLevel(LogLevel::trace);
    if (log) {
      integration()->trace("[host->vm] " + std::string(function_name) + "(" +
                           printValues(params, sizeof...(Args)) + ")");
    }
    SaveRestoreContext saved_context(context);
    WasmEdge_Result res =
        WasmEdge_ExecutorInvoke(executor_.get(), func_cxt, params, sizeof...(Args), nullptr, 0);
    if (!WasmEdge_ResultOK(res)) {
      fail(FailState::RuntimeError, "Function: " + std::string(function_name) + " failed:\n" +
                                        WasmEdge_ResultGetMessage(res));
      return;
    }
    if (log) {
      integration()->trace("[host<-vm] " + std::string(function_name) + " return: void");
    }
  };
}

template <typename R, typename... Args>
void WasmEdge::getModuleFunctionImpl(std::string_view function_name,
                                     std::function<R(ContextBase *, Args...)> *function) {
  auto *func_cxt = WasmEdge_ModuleInstanceFindFunction(
      module_.get(), WasmEdge_StringWrap(function_name.data(), function_name.length()));
  if (!func_cxt) {
    *function = nullptr;
    return;
  }

  std::vector<enum WasmEdge_ValType> exp_args;
  std::vector<enum WasmEdge_ValType> exp_returns;
  convArgsTupleToValTypes<std::tuple<Args...>>(exp_args);
  convArgsTupleToValTypes<std::tuple<R>>(exp_returns);
  const auto *functype_cxt = WasmEdge_FunctionInstanceGetFunctionType(func_cxt);
  std::vector<enum WasmEdge_ValType> act_args(
      WasmEdge_FunctionTypeGetParametersLength(functype_cxt));
  std::vector<enum WasmEdge_ValType> act_returns(
      WasmEdge_FunctionTypeGetReturnsLength(functype_cxt));
  WasmEdge_FunctionTypeGetParameters(functype_cxt, act_args.data(), act_args.size());
  WasmEdge_FunctionTypeGetReturns(functype_cxt, act_returns.data(), act_returns.size());

  if (exp_args != act_args || exp_returns != act_returns) {
    fail(FailState::UnableToInitializeCode,
         "Bad function signature for: " + std::string(function_name) +
             ", want: " + printValTypes(exp_args.data(), exp_args.size()) + " -> " +
             printValTypes(exp_returns.data(), exp_returns.size()) +
             ", but the module exports: " + printValTypes(act_args.data(), act_args.size()) +
             " -> " + printValTypes(act_returns.data(), act_returns.size()));
    return;
  }

  *function = [function_name, func_cxt, this](ContextBase *context, Args... args) -> R {
    WasmEdge_Value params[] = {makeVal(args)...};
    WasmEdge_Value results[1];
    const bool log = cmpLogLevel(LogLevel::trace);
    if (log) {
      integration()->trace("[host->vm] " + std::string(function_name) + "(" +
                           printValues(params, sizeof...(Args)) + ")");
    }
    SaveRestoreContext saved_context(context);
    WasmEdge_Result res =
        WasmEdge_ExecutorInvoke(executor_.get(), func_cxt, params, sizeof...(Args), results, 1);
    if (!WasmEdge_ResultOK(res)) {
      fail(FailState::RuntimeError, "Function: " + std::string(function_name) + " failed:\n" +
                                        WasmEdge_ResultGetMessage(res));
      return R{};
    }
    R ret = convValTypeToArg<R>(results[0]);
    if (log) {
      integration()->trace("[host<-vm] " + std::string(function_name) +
                           " return: " + std::to_string(ret));
    }
    return ret;
  };
}

} // namespace WasmEdge

std::unique_ptr<WasmVm> createWasmEdgeVm() { return std::make_unique<WasmEdge::WasmEdge>(); }

} // namespace proxy_wasm
