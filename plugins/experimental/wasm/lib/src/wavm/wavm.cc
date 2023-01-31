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

#include "include/proxy-wasm/wavm.h"
#include "include/proxy-wasm/wasm_vm.h"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

#include "WAVM/IR/Module.h"
#include "WAVM/IR/Operators.h"
#include "WAVM/IR/Types.h"
#include "WAVM/IR/Validate.h"
#include "WAVM/IR/Value.h"
#include "WAVM/Inline/Assert.h"
#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Inline/Errors.h"
#include "WAVM/Inline/Hash.h"
#include "WAVM/Inline/HashMap.h"
#include "WAVM/Inline/IndexMap.h"
#include "WAVM/Inline/IntrusiveSharedPtr.h"
#include "WAVM/Platform/Mutex.h"
#include "WAVM/Platform/Thread.h"
#include "WAVM/Runtime/Intrinsics.h"
#include "WAVM/Runtime/Linker.h"
#include "WAVM/Runtime/Runtime.h"
#include "WAVM/RuntimeABI/RuntimeABI.h"
#include "WAVM/WASM/WASM.h"
#include "WAVM/WASTParse/WASTParse.h"

#ifdef NDEBUG
#define ASSERT(_x) _x
#else
#define ASSERT(_x)                                                                                 \
  do {                                                                                             \
    if (!_x)                                                                                       \
      ::exit(1);                                                                                   \
  } while (0)
#endif

using namespace WAVM;
using namespace WAVM::IR;

namespace WAVM::IR {
template <> constexpr ValueType inferValueType<proxy_wasm::Word>() { return ValueType::i32; }
} // namespace WAVM::IR

namespace proxy_wasm {

// Forward declarations.
template <typename... Args>
void getFunctionWavm(WasmVm *vm, std::string_view function_name,
                     std::function<void(ContextBase *, Args...)> *function);
template <typename R, typename... Args>
void getFunctionWavm(WasmVm *vm, std::string_view function_name,
                     std::function<R(ContextBase *, Args...)> *function);
template <typename R, typename... Args>
void registerCallbackWavm(WasmVm *vm, std::string_view module_name, std::string_view function_name,
                          R (*function)(Args...));

namespace Wavm {

struct Wavm;

namespace {

#define CALL_WITH_CONTEXT(_x, _context, _wavm)                                                     \
  do {                                                                                             \
    try {                                                                                          \
      SaveRestoreContext _saved_context(static_cast<ContextBase *>(_context));                     \
      WAVM::Runtime::catchRuntimeExceptions(                                                       \
          [&] { _x; },                                                                             \
          [&](WAVM::Runtime::Exception *exception) {                                               \
            _wavm->fail(FailState::RuntimeError, getFailMessage(function_name, exception));        \
            throw std::exception();                                                                \
          });                                                                                      \
    } catch (...) {                                                                                \
    }                                                                                              \
  } while (0)

std::string getFailMessage(std::string_view function_name, WAVM::Runtime::Exception *exception) {
  std::string message = "Function: " + std::string(function_name) +
                        " failed: " + WAVM::Runtime::describeExceptionType(exception->type) +
                        "\nProxy-Wasm plugin in-VM backtrace:\n";
  std::vector<std::string> callstack_descriptions =
      WAVM::Runtime::describeCallStack(exception->callStack);

  // Since the first frame is on host and useless for developers, e.g.: `host!envoy+112901013`
  // we start with index 1 here
  for (size_t i = 1; i < callstack_descriptions.size(); i++) {
    std::ostringstream oss;
    std::string description = callstack_descriptions[i];
    if (description.find("wasm!") == std::string::npos) {
      // end of WASM's call stack
      break;
    }
    oss << std::setw(3) << std::setfill(' ') << std::to_string(i);
    message += oss.str() + ": " + description + "\n";
  }

  WAVM::Runtime::destroyException(exception);
  return message;
}

struct WasmUntaggedValue : public WAVM::IR::UntaggedValue {
  WasmUntaggedValue() = default;
  WasmUntaggedValue(I32 inI32) { i32 = inI32; }
  WasmUntaggedValue(I64 inI64) { i64 = inI64; }
  WasmUntaggedValue(U32 inU32) { u32 = inU32; }
  WasmUntaggedValue(Word w) { u32 = static_cast<U32>(w.u64_); }
  WasmUntaggedValue(U64 inU64) { u64 = inU64; }
  WasmUntaggedValue(F32 inF32) { f32 = inF32; }
  WasmUntaggedValue(F64 inF64) { f64 = inF64; }
};

class RootResolver : public WAVM::Runtime::Resolver {
public:
  RootResolver(WAVM::Runtime::Compartment * /*compartment*/, WasmVm *vm) : vm_(vm) {}

  ~RootResolver() override { module_name_to_instance_map_.clear(); }

  bool resolve(const std::string &module_name, const std::string &export_name, ExternType type,
               WAVM::Runtime::Object *&out_object) override {
    auto *named_instance = module_name_to_instance_map_.get(module_name);
    if (named_instance != nullptr) {
      out_object = getInstanceExport(*named_instance, export_name);
      if (out_object != nullptr) {
        if (!isA(out_object, type)) {
          vm_->fail(FailState::UnableToInitializeCode,
                    "Failed to load WASM module due to a type mismatch in an import: " +
                        std::string(module_name) + "." + export_name + " " +
                        asString(WAVM::Runtime::getExternType(out_object)) +
                        " but was expecting type: " + asString(type));
          return false;
        }
        return true;
      }
    }
    for (auto *r : resolvers_) {
      if (r->resolve(module_name, export_name, type, out_object)) {
        return true;
      }
    }
    vm_->fail(FailState::MissingFunction,
              "Failed to load Wasm module due to a missing import: " + std::string(module_name) +
                  "." + std::string(export_name) + " " + asString(type));
    return false;
  }

  HashMap<std::string, WAVM::Runtime::Instance *> &moduleNameToInstanceMap() {
    return module_name_to_instance_map_;
  }

  void addResolver(WAVM::Runtime::Resolver *r) { resolvers_.push_back(r); }

private:
  WasmVm *vm_;
  HashMap<std::string, WAVM::Runtime::Instance *> module_name_to_instance_map_{};
  std::vector<WAVM::Runtime::Resolver *> resolvers_{};
};

const uint64_t WasmPageSize = 1 << 16;

} // namespace

template <typename T> struct NativeWord { using type = T; };
template <> struct NativeWord<Word> { using type = uint32_t; };

template <typename T> typename NativeWord<T>::type ToNative(const T &t) { return t; }
template <> typename NativeWord<Word>::type ToNative(const Word &t) { return t.u32(); }

struct PairHash {
  template <typename T, typename U> std::size_t operator()(const std::pair<T, U> &x) const {
    return std::hash<T>()(x.first) + std::hash<U>()(x.second);
  }
};

struct Wavm : public WasmVm {
  Wavm() = default;
  ~Wavm() override;

  // WasmVm
  std::string_view getEngineName() override { return "wavm"; }
  Cloneable cloneable() override { return Cloneable::InstantiatedModule; };
  std::unique_ptr<WasmVm> clone() override;
  bool load(std::string_view bytecode, std::string_view precompiled,
            const std::unordered_map<uint32_t, std::string> &function_names) override;
  bool link(std::string_view debug_name) override;
  uint64_t getMemorySize() override;
  std::optional<std::string_view> getMemory(uint64_t pointer, uint64_t size) override;
  bool setMemory(uint64_t pointer, uint64_t size, const void *data) override;
  bool getWord(uint64_t pointer, Word *data) override;
  bool setWord(uint64_t pointer, Word data) override;
  size_t getWordSize() override { return sizeof(uint32_t); };
  std::string_view getPrecompiledSectionName() override;

#define _GET_FUNCTION(_T)                                                                          \
  void getFunction(std::string_view function_name, _T *f) override {                               \
    getFunctionWavm(this, function_name, f);                                                       \
  };
  FOR_ALL_WASM_VM_EXPORTS(_GET_FUNCTION)
#undef _GET_FUNCTION

#define _REGISTER_CALLBACK(_T)                                                                     \
  void registerCallback(std::string_view module_name, std::string_view function_name, _T,          \
                        typename ConvertFunctionTypeWordToUint32<_T>::type f) override {           \
    registerCallbackWavm(this, module_name, function_name, f);                                     \
  };
  FOR_ALL_WASM_VM_IMPORTS(_REGISTER_CALLBACK)
#undef _REGISTER_CALLBACK

  void terminate() override {}
  bool usesWasmByteOrder() override { return true; }

  IR::Module ir_module_;
  WAVM::Runtime::ModuleRef module_ = nullptr;
  WAVM::Runtime::GCPointer<WAVM::Runtime::Instance> module_instance_;
  WAVM::Runtime::Memory *memory_{};
  WAVM::Runtime::GCPointer<WAVM::Runtime::Compartment> compartment_;
  WAVM::Runtime::GCPointer<WAVM::Runtime::Context> context_;
  std::map<std::string, Intrinsics::Module> intrinsic_modules_{};
  std::map<std::string, WAVM::Runtime::GCPointer<WAVM::Runtime::Instance>>
      intrinsic_module_instances_{};
  std::vector<std::unique_ptr<Intrinsics::Function>> host_functions_{};
  uint8_t *memory_base_ = nullptr;
};

Wavm::~Wavm() {
  module_instance_ = nullptr;
  context_ = nullptr;
  intrinsic_module_instances_.clear();
  intrinsic_modules_.clear();
  host_functions_.clear();
  if (compartment_ != nullptr) {
    ASSERT(tryCollectCompartment(std::move(compartment_)));
  }
}

std::unique_ptr<WasmVm> Wavm::clone() {
  auto wavm = std::make_unique<Wavm>();
  if (wavm == nullptr) {
    return nullptr;
  }

  wavm->compartment_ = WAVM::Runtime::cloneCompartment(compartment_);
  if (wavm->compartment_ == nullptr) {
    return nullptr;
  }

  wavm->context_ = WAVM::Runtime::cloneContext(context_, wavm->compartment_);
  if (wavm->context_ == nullptr) {
    return nullptr;
  }

  wavm->memory_ = WAVM::Runtime::remapToClonedCompartment(memory_, wavm->compartment_);
  wavm->memory_base_ = WAVM::Runtime::getMemoryBaseAddress(wavm->memory_);
  wavm->module_instance_ =
      WAVM::Runtime::remapToClonedCompartment(module_instance_, wavm->compartment_);

  for (auto &p : intrinsic_module_instances_) {
    wavm->intrinsic_module_instances_.emplace(
        p.first, WAVM::Runtime::remapToClonedCompartment(p.second, wavm->compartment_));
  }

  auto *integration_clone = integration()->clone();
  if (integration_clone == nullptr) {
    return nullptr;
  }
  wavm->integration().reset(integration_clone);

  return wavm;
}

bool Wavm::load(std::string_view bytecode, std::string_view precompiled,
                const std::unordered_map<uint32_t, std::string> & /*function_names*/) {
  compartment_ = WAVM::Runtime::createCompartment();
  if (compartment_ == nullptr) {
    return false;
  }

  context_ = WAVM::Runtime::createContext(compartment_);
  if (context_ == nullptr) {
    return false;
  }

  if (!WASM::loadBinaryModule(reinterpret_cast<const unsigned char *>(bytecode.data()),
                              bytecode.size(), ir_module_)) {
    return false;
  }

  if (!precompiled.empty()) {
    module_ = WAVM::Runtime::loadPrecompiledModule(
        ir_module_, {precompiled.data(), precompiled.data() + precompiled.size()});
    if (module_ == nullptr) {
      return false;
    }

  } else {
    module_ = WAVM::Runtime::compileModule(ir_module_);
    if (module_ == nullptr) {
      return false;
    }
  }

  return true;
}

bool Wavm::link(std::string_view debug_name) {
  RootResolver rootResolver(compartment_, this);
  for (auto &p : intrinsic_modules_) {
    auto *instance = Intrinsics::instantiateModule(compartment_, {&intrinsic_modules_[p.first]},
                                                   std::string(p.first));
    if (instance == nullptr) {
      return false;
    }
    intrinsic_module_instances_.emplace(p.first, instance);
    rootResolver.moduleNameToInstanceMap().set(p.first, instance);
  }

  WAVM::Runtime::LinkResult link_result = linkModule(ir_module_, rootResolver);
  if (!link_result.missingImports.empty()) {
    for (auto &i : link_result.missingImports) {
      integration()->error("Missing Wasm import " + i.moduleName + " " + i.exportName);
    }
    fail(FailState::MissingFunction, "Failed to load Wasm module due to a missing import(s)");
    return false;
  }

  module_instance_ = instantiateModule(
      compartment_, module_, std::move(link_result.resolvedImports), std::string(debug_name));
  if (module_instance_ == nullptr) {
    return false;
  }

  memory_ = getDefaultMemory(module_instance_);
  if (memory_ == nullptr) {
    return false;
  }

  memory_base_ = WAVM::Runtime::getMemoryBaseAddress(memory_);

  return true;
}

uint64_t Wavm::getMemorySize() { return WAVM::Runtime::getMemoryNumPages(memory_) * WasmPageSize; }

std::optional<std::string_view> Wavm::getMemory(uint64_t pointer, uint64_t size) {
  auto memory_num_bytes = WAVM::Runtime::getMemoryNumPages(memory_) * WasmPageSize;
  if (pointer + size > memory_num_bytes) {
    return std::nullopt;
  }
  return std::string_view(reinterpret_cast<char *>(memory_base_ + pointer), size);
}

bool Wavm::setMemory(uint64_t pointer, uint64_t size, const void *data) {
  auto memory_num_bytes = WAVM::Runtime::getMemoryNumPages(memory_) * WasmPageSize;
  if (pointer + size > memory_num_bytes) {
    return false;
  }
  auto *p = reinterpret_cast<char *>(memory_base_ + pointer);
  memcpy(p, data, size);
  return true;
}

bool Wavm::getWord(uint64_t pointer, Word *data) {
  auto memory_num_bytes = WAVM::Runtime::getMemoryNumPages(memory_) * WasmPageSize;
  if (pointer + sizeof(uint32_t) > memory_num_bytes) {
    return false;
  }
  auto *p = reinterpret_cast<char *>(memory_base_ + pointer);
  uint32_t data32;
  memcpy(&data32, p, sizeof(uint32_t));
  data->u64_ = wasmtoh(data32, true);
  return true;
}

bool Wavm::setWord(uint64_t pointer, Word data) {
  uint32_t data32 = htowasm(data.u32(), true);
  return setMemory(pointer, sizeof(uint32_t), &data32);
}

std::string_view Wavm::getPrecompiledSectionName() { return "wavm.precompiled_object"; }

} // namespace Wavm

std::unique_ptr<WasmVm> createWavmVm() { return std::make_unique<proxy_wasm::Wavm::Wavm>(); }

template <typename R, typename... Args>
IR::FunctionType inferHostFunctionType(R (*/*func*/)(Args...)) {
  return IR::FunctionType(IR::inferResultType<R>(), IR::TypeTuple({IR::inferValueType<Args>()...}),
                          IR::CallingConvention::c);
}

using namespace Wavm;

template <typename R, typename... Args>
void registerCallbackWavm(WasmVm *vm, std::string_view module_name, std::string_view function_name,
                          R (*f)(Args...)) {
  auto *wavm = dynamic_cast<proxy_wasm::Wavm::Wavm *>(vm);
  wavm->host_functions_.emplace_back(new Intrinsics::Function(
      &wavm->intrinsic_modules_[std::string(module_name)], function_name.data(),
      reinterpret_cast<void *>(f), inferHostFunctionType(f)));
}

template <typename R, typename... Args>
IR::FunctionType inferStdFunctionType(std::function<R(ContextBase *, Args...)> * /*func*/) {
  return IR::FunctionType(IR::inferResultType<R>(), IR::TypeTuple({IR::inferValueType<Args>()...}));
}

static bool checkFunctionType(WAVM::Runtime::Function *f, IR::FunctionType t) {
  return getFunctionType(f) == t;
}

template <typename R, typename... Args>
void getFunctionWavm(WasmVm *vm, std::string_view function_name,
                     std::function<R(ContextBase *, Args...)> *function) {
  auto *wavm = dynamic_cast<proxy_wasm::Wavm::Wavm *>(vm);
  auto *f =
      asFunctionNullable(getInstanceExport(wavm->module_instance_, std::string(function_name)));
  if (!f) {
    f = asFunctionNullable(getInstanceExport(wavm->module_instance_, std::string(function_name)));
  }
  if (!f) {
    *function = nullptr;
    return;
  }
  if (!checkFunctionType(f, inferStdFunctionType(function))) {
    *function = nullptr;
    wavm->fail(FailState::UnableToInitializeCode,
               "Bad function signature for: " + std::string(function_name));
    return;
  }
  *function = [wavm, f, function_name](ContextBase *context, Args... args) -> R {
    WasmUntaggedValue values[] = {args...};
    WasmUntaggedValue return_value;
    CALL_WITH_CONTEXT(
        invokeFunction(wavm->context_, f, getFunctionType(f), &values[0], &return_value), context,
        wavm);
    if (wavm->isFailed()) {
      return 0;
    }
    return static_cast<uint32_t>(return_value.i32);
  };
}

template <typename... Args>
void getFunctionWavm(WasmVm *vm, std::string_view function_name,
                     std::function<void(ContextBase *, Args...)> *function) {
  auto *wavm = dynamic_cast<proxy_wasm::Wavm::Wavm *>(vm);
  auto *f =
      asFunctionNullable(getInstanceExport(wavm->module_instance_, std::string(function_name)));
  if (!f) {
    f = asFunctionNullable(getInstanceExport(wavm->module_instance_, std::string(function_name)));
  }
  if (!f) {
    *function = nullptr;
    return;
  }
  if (!checkFunctionType(f, inferStdFunctionType(function))) {
    *function = nullptr;
    wavm->fail(FailState::UnableToInitializeCode,
               "Bad function signature for: " + std::string(function_name));
    return;
  }
  *function = [wavm, f, function_name](ContextBase *context, Args... args) {
    WasmUntaggedValue values[] = {args...};
    CALL_WITH_CONTEXT(invokeFunction(wavm->context_, f, getFunctionType(f), &values[0]), context,
                      wavm);
  };
}

} // namespace proxy_wasm
