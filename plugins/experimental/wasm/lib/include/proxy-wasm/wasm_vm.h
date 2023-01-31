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

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "include/proxy-wasm/word.h"

namespace proxy_wasm {

#include "proxy_wasm_enums.h"

class ContextBase;

// These are templates and its helper for constructing signatures of functions calling into Wasm
// VMs.
// - WasmCallInFuncTypeHelper is a helper for WasmFuncType and shouldn't be used anywhere else than
// WasmFuncType definition.
// - WasmCallInFuncType takes 4 template parameter which are number of argument, return type,
// context type and param type respectively, resolve to a function type.
//   For example `WasmFuncType<3, void, Context*, Word>` resolves to `void(Context*, Word, Word,
//   Word)`
template <size_t N, class ReturnType, class ContextType, class ParamType,
          class FuncBase = ReturnType(ContextType)>
struct WasmCallInFuncTypeHelper {};

template <size_t N, class ReturnType, class ContextType, class ParamType, class... Args>
struct WasmCallInFuncTypeHelper<N, ReturnType, ContextType, ParamType,
                                ReturnType(ContextType, Args...)> {
  // NOLINTNEXTLINE(readability-identifier-naming)
  using type = typename WasmCallInFuncTypeHelper<N - 1, ReturnType, ContextType, ParamType,
                                                 ReturnType(ContextType, Args..., ParamType)>::type;
};

template <class ReturnType, class ContextType, class ParamType, class... Args>
struct WasmCallInFuncTypeHelper<0, ReturnType, ContextType, ParamType,
                                ReturnType(ContextType, Args...)> {
  using type = ReturnType(ContextType, Args...); // NOLINT(readability-identifier-naming)
};

template <size_t N, class ReturnType, class ContextType, class ParamType>
using WasmCallInFuncType =
    typename WasmCallInFuncTypeHelper<N, ReturnType, ContextType, ParamType>::type;

// Calls into the WASM VM.
// 1st arg is always a pointer to Context (Context*).
template <size_t N>
using WasmCallVoid = std::function<WasmCallInFuncType<N, void, ContextBase *, Word>>;
template <size_t N>
using WasmCallWord = std::function<WasmCallInFuncType<N, Word, ContextBase *, Word>>;

#define FOR_ALL_WASM_VM_EXPORTS(_f)                                                                \
  _f(proxy_wasm::WasmCallVoid<0>) _f(proxy_wasm::WasmCallVoid<1>) _f(proxy_wasm::WasmCallVoid<2>)  \
      _f(proxy_wasm::WasmCallVoid<3>) _f(proxy_wasm::WasmCallVoid<5>)                              \
          _f(proxy_wasm::WasmCallWord<1>) _f(proxy_wasm::WasmCallWord<2>)                          \
              _f(proxy_wasm::WasmCallWord<3>)

// These are templates and its helper for constructing signatures of functions callbacks from Wasm
// VMs.
// - WasmCallbackFuncTypeHelper is a helper for WasmFuncType and shouldn't be used anywhere else
// than WasmFuncType definition.
// - WasmCallbackFuncType takes 3 template parameter which are number of argument, return type, and
// param type respectively, resolve to a function type.
//   For example `WasmFuncType<3, Word>` resolves to `void(Word, Word, Word)`
template <size_t N, class ReturnType, class ParamType, class FuncBase = ReturnType()>
struct WasmCallbackFuncTypeHelper {};

template <size_t N, class ReturnType, class ParamType, class... Args>
struct WasmCallbackFuncTypeHelper<N, ReturnType, ParamType, ReturnType(Args...)> {
  // NOLINTNEXTLINE(readability-identifier-naming)
  using type = typename WasmCallbackFuncTypeHelper<N - 1, ReturnType, ParamType,
                                                   ReturnType(Args..., ParamType)>::type;
};

template <class ReturnType, class ParamType, class... Args>
struct WasmCallbackFuncTypeHelper<0, ReturnType, ParamType, ReturnType(Args...)> {
  using type = ReturnType(Args...); // NOLINT(readability-identifier-naming)
};

template <size_t N, class ReturnType, class ParamType>
using WasmCallbackFuncType = typename WasmCallbackFuncTypeHelper<N, ReturnType, ParamType>::type;

// Calls out of the WASM VM.
template <size_t N> using WasmCallbackVoid = WasmCallbackFuncType<N, void, Word> *;
template <size_t N> using WasmCallbackWord = WasmCallbackFuncType<N, Word, Word> *;

// Using the standard g++/clang mangling algorithm:
// https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling-builtin
// Extended with W = Word
// Z = void, j = uint32_t, l = int64_t, m = uint64_t
using WasmCallback_WWl = Word (*)(Word, int64_t);
using WasmCallback_WWlWW = Word (*)(Word, int64_t, Word, Word);
using WasmCallback_WWm = Word (*)(Word, uint64_t);
using WasmCallback_WWmW = Word (*)(Word, uint64_t, Word);
using WasmCallback_WWWWWWllWW = Word (*)(Word, Word, Word, Word, Word, int64_t, int64_t, Word,
                                         Word);
using WasmCallback_dd = double (*)(double);

#define FOR_ALL_WASM_VM_IMPORTS(_f)                                                                \
  _f(proxy_wasm::WasmCallbackVoid<0>) _f(proxy_wasm::WasmCallbackVoid<1>)                          \
      _f(proxy_wasm::WasmCallbackVoid<2>) _f(proxy_wasm::WasmCallbackVoid<3>)                      \
          _f(proxy_wasm::WasmCallbackVoid<4>) _f(proxy_wasm::WasmCallbackWord<0>)                  \
              _f(proxy_wasm::WasmCallbackWord<1>) _f(proxy_wasm::WasmCallbackWord<2>)              \
                  _f(proxy_wasm::WasmCallbackWord<3>) _f(proxy_wasm::WasmCallbackWord<4>)          \
                      _f(proxy_wasm::WasmCallbackWord<5>) _f(proxy_wasm::WasmCallbackWord<6>)      \
                          _f(proxy_wasm::WasmCallbackWord<7>) _f(proxy_wasm::WasmCallbackWord<8>)  \
                              _f(proxy_wasm::WasmCallbackWord<9>)                                  \
                                  _f(proxy_wasm::WasmCallbackWord<10>)                             \
                                      _f(proxy_wasm::WasmCallbackWord<12>)                         \
                                          _f(proxy_wasm::WasmCallback_WWl)                         \
                                              _f(proxy_wasm::WasmCallback_WWlWW)                   \
                                                  _f(proxy_wasm::WasmCallback_WWm)                 \
                                                      _f(proxy_wasm::WasmCallback_WWmW)            \
                                                          _f(proxy_wasm::WasmCallback_WWWWWWllWW)  \
                                                              _f(proxy_wasm::WasmCallback_dd)

enum class Cloneable {
  NotCloneable,      // VMs can not be cloned and should be created from scratch.
  CompiledBytecode,  // VMs can be cloned with compiled bytecode.
  InstantiatedModule // VMs can be cloned from an instantiated module.
};

enum class AbiVersion { ProxyWasm_0_1_0, ProxyWasm_0_2_0, ProxyWasm_0_2_1, Unknown };

class NullPlugin;

// Integrator specific WasmVm operations.
struct WasmVmIntegration {
  virtual ~WasmVmIntegration() {}
  virtual WasmVmIntegration *clone() = 0;
  virtual proxy_wasm::LogLevel getLogLevel() = 0;
  virtual void error(std::string_view message) = 0;
  virtual void trace(std::string_view message) = 0;
  // Get a NullVm implementation of a function.
  // @param function_name is the name of the function with the implementation specific prefix.
  // @param returns_word is true if the function returns a Word and false if it returns void.
  // @param number_of_arguments is the number of Word arguments to the function.
  // @param plugin is the Null VM plugin on which the function will be called.
  // @param ptr_to_function_return is the location to write the function e.g. of type
  // WasmCallWord<3>.
  // @return true if the function was found.  ptr_to_function_return could still be set to nullptr
  // (of the correct type) if the function has no implementation.  Returning true will prevent a
  // "Missing getFunction" error.
  virtual bool getNullVmFunction(std::string_view function_name, bool returns_word,
                                 int number_of_arguments, NullPlugin *plugin,
                                 void *ptr_to_function_return) = 0;
};

enum class FailState : int {
  Ok = 0,
  UnableToCreateVm = 1,
  UnableToCloneVm = 2,
  MissingFunction = 3,
  UnableToInitializeCode = 4,
  StartFailed = 5,
  ConfigureFailed = 6,
  RuntimeError = 7,
};

// Wasm VM instance. Provides the low level WASM interface.
class WasmVm {
public:
  virtual ~WasmVm() = default;
  /**
   * Identify the Wasm engine.
   * @return the name of the underlying Wasm engine.
   */
  virtual std::string_view getEngineName() = 0;

  /**
   * Whether or not the VM implementation supports cloning. Cloning is VM system dependent.
   * When a VM is configured a single VM is instantiated to check that the .wasm file is valid and
   * to do VM system specific initialization. In the case of WAVM this is potentially ahead-of-time
   * compilation. Then, if cloning is supported, we clone that VM for each worker, potentially
   * copying and sharing the initialized data structures for efficiency. Otherwise we create an new
   * VM from scratch for each worker.
   * @return one of enum Cloneable with the VMs cloneability.
   */
  virtual Cloneable cloneable() = 0;

  /**
   * Make a worker/thread-specific copy if supported by the underlying VM system (see cloneable()
   * above). If not supported, the caller will need to create a new VM from scratch. If supported,
   * the clone may share compiled code and other read-only data with the source VM.
   * @return a clone of 'this' (e.g. for a different worker/thread).
   */
  virtual std::unique_ptr<WasmVm> clone() = 0;

  /**
   * Load the WASM code from a file. Return true on success. Once the module is loaded it can be
   * queried, e.g. to see which version of emscripten support is required. After loading, the
   * appropriate ABI callbacks can be registered and then the module can be link()ed (see below).
   * @param bytecode the Wasm bytecode or registered NullVm plugin name.
   * @param precompiled (optional) the precompiled Wasm module.
   * @param function_names (optional) an index-to-name mapping for the functions.
   * @return whether or not the load was successful.
   */
  virtual bool load(std::string_view bytecode, std::string_view precompiled,
                    const std::unordered_map<uint32_t, std::string> &function_names) = 0;

  /**
   * Link the WASM code to the host-provided functions, e.g. the ABI. Prior to linking, the module
   * should be loaded and the ABI callbacks registered (see above). Linking should be done once
   * after load().
   * @param debug_name user-provided name for use in log and error messages.
   * @return whether or not the link was successful.
   */
  virtual bool link(std::string_view debug_name) = 0;

  /**
   * Get size of the currently allocated memory in the VM.
   * @return the size of memory in bytes.
   */
  virtual uint64_t getMemorySize() = 0;

  /**
   * Convert a block of memory in the VM to a std::string_view.
   * @param pointer the offset into VM memory of the requested VM memory block.
   * @param size the size of the requested VM memory block.
   * @return if std::nullopt then the pointer/size pair were invalid, otherwise returns
   * a host std::string_view pointing to the pointer/size pair in VM memory.
   */
  virtual std::optional<std::string_view> getMemory(uint64_t pointer, uint64_t size) = 0;

  /**
   * Set a block of memory in the VM, returns true on success, false if the pointer/size is invalid.
   * @param pointer the offset into VM memory describing the start of a region of VM memory.
   * @param size the size of the region of VM memory.
   * @return whether or not the pointer/size pair was a valid VM memory block.
   */
  virtual bool setMemory(uint64_t pointer, uint64_t size, const void *data) = 0;

  /**
   * Get a VM native Word (e.g. sizeof(void*) or sizeof(size_t)) from VM memory, returns true on
   * success, false if the pointer is invalid. WASM-32 VMs have 32-bit native words and WASM-64 VMs
   * (not yet supported) will have 64-bit words as does the Null VM (compiled into a 64-bit proxy).
   * This function can be used to chase pointers in VM memory.
   * @param pointer the offset into VM memory describing the start of VM native word size block.
   * @param data a pointer to a Word whose contents will be filled from the VM native word at
   * 'pointer'.
   * @return whether or not the pointer was to a valid VM memory block of VM native word size.
   */
  virtual bool getWord(uint64_t pointer, Word *data) = 0;

  /**
   * Set a Word in the VM, returns true on success, false if the pointer is invalid.
   * See getWord above for details. This function can be used (for example) to set indirect pointer
   * return values (e.g. proxy_getHeaderHapValue(... const char** value_ptr, size_t* value_size).
   * @param pointer the offset into VM memory describing the start of VM native word size block.
   * @param data a Word whose contents will be written in VM native word size at 'pointer'.
   * @return whether or not the pointer was to a valid VM memory block of VM native word size.
   */
  virtual bool setWord(uint64_t pointer, Word data) = 0;

  /**
   * @return the Word size in this VM.
   */
  virtual size_t getWordSize() = 0;

  /**
   * Get the name of the custom section that contains precompiled module.
   * @return the name of the custom section that contains precompiled module.
   */
  virtual std::string_view getPrecompiledSectionName() = 0;

  /**
   * Get typed function exported by the WASM module.
   */
#define _GET_FUNCTION(_T) virtual void getFunction(std::string_view function_name, _T *f) = 0;
  FOR_ALL_WASM_VM_EXPORTS(_GET_FUNCTION)
#undef _GET_FUNCTION

  /**
   * Register typed callbacks exported by the host environment.
   */
#define _REGISTER_CALLBACK(_T)                                                                     \
  virtual void registerCallback(std::string_view moduleName, std::string_view function_name, _T f, \
                                typename ConvertFunctionTypeWordToUint32<_T>::type) = 0;
  FOR_ALL_WASM_VM_IMPORTS(_REGISTER_CALLBACK)
#undef _REGISTER_CALLBACK

  /**
   * Terminate execution of this WasmVM. It shouldn't be used after being terminated.
   */
  virtual void terminate() = 0;

  /**
   * Byte order flag (host or wasm).
   * @return 'false' for a null VM and 'true' for a wasm VM.
   */
  virtual bool usesWasmByteOrder() = 0;

  bool isFailed() { return failed_ != FailState::Ok; }
  void fail(FailState fail_state, std::string_view message) {
    integration()->error(message);
    failed_ = fail_state;
    for (auto &callback : fail_callbacks_) {
      callback(fail_state);
    }
  }
  void addFailCallback(std::function<void(FailState)> fail_callback) {
    fail_callbacks_.push_back(fail_callback);
  }

  bool isHostFunctionAllowed(const std::string &name) {
    return !restricted_callback_ || allowed_hostcalls_.find(name) != allowed_hostcalls_.end();
  }

  void setRestrictedCallback(bool restricted,
                             std::unordered_set<std::string> allowed_hostcalls = {}) {
    restricted_callback_ = restricted;
    allowed_hostcalls_ = std::move(allowed_hostcalls);
  }

  // Integrator operations.
  std::unique_ptr<WasmVmIntegration> &integration() { return integration_; }
  bool cmpLogLevel(proxy_wasm::LogLevel level) { return integration_->getLogLevel() <= level; }

protected:
  std::unique_ptr<WasmVmIntegration> integration_;
  FailState failed_ = FailState::Ok;
  std::vector<std::function<void(FailState)>> fail_callbacks_;

private:
  bool restricted_callback_{false};
  std::unordered_set<std::string> allowed_hostcalls_{};
};

// Thread local state set during a call into a WASM VM so that calls coming out of the
// VM can be attributed correctly to calling Filter. We use thread_local instead of ThreadLocal
// because this state is live only during the calls and does not need to be initialized consistently
// over all workers as with ThreadLocal data.
extern thread_local ContextBase *current_context_;

// Requested effective context set by code within the VM to request that the calls coming out of the
// VM be attributed to another filter, for example if a control plane gRPC comes back to the
// RootContext which effects some set of waiting filters.
extern thread_local uint32_t effective_context_id_;

// Helper to save and restore thread local VM call context information to support reentrant calls.
// NB: this happens for example when a call from the VM invokes a handler which needs to _malloc
// memory in the VM.
struct SaveRestoreContext {
  explicit SaveRestoreContext(ContextBase *context) {
    saved_context = current_context_;
    saved_effective_context_id_ = effective_context_id_;
    current_context_ = context;
    effective_context_id_ = 0; // No effective context id.
  }
  ~SaveRestoreContext() {
    current_context_ = saved_context;
    effective_context_id_ = saved_effective_context_id_;
  }
  ContextBase *saved_context;
  uint32_t saved_effective_context_id_;
};

} // namespace proxy_wasm
