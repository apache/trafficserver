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

#include "src/common/types.h"
#include "include/wasm.h"

namespace proxy_wasm::wasmtime {

using WasmEnginePtr = common::CSmartPtr<wasm_engine_t, wasm_engine_delete>;
using WasmFuncPtr = common::CSmartPtr<wasm_func_t, wasm_func_delete>;
using WasmStorePtr = common::CSmartPtr<wasm_store_t, wasm_store_delete>;
using WasmModulePtr = common::CSmartPtr<wasm_module_t, wasm_module_delete>;
using WasmSharedModulePtr = common::CSmartPtr<wasm_shared_module_t, wasm_shared_module_delete>;
using WasmMemoryPtr = common::CSmartPtr<wasm_memory_t, wasm_memory_delete>;
using WasmTablePtr = common::CSmartPtr<wasm_table_t, wasm_table_delete>;
using WasmInstancePtr = common::CSmartPtr<wasm_instance_t, wasm_instance_delete>;
using WasmFunctypePtr = common::CSmartPtr<wasm_functype_t, wasm_functype_delete>;
using WasmTrapPtr = common::CSmartPtr<wasm_trap_t, wasm_trap_delete>;
using WasmExternPtr = common::CSmartPtr<wasm_extern_t, wasm_extern_delete>;

using WasmByteVec =
    common::CSmartType<wasm_byte_vec_t, wasm_byte_vec_new_empty, wasm_byte_vec_delete>;
using WasmImporttypeVec = common::CSmartType<wasm_importtype_vec_t, wasm_importtype_vec_new_empty,
                                             wasm_importtype_vec_delete>;
using WasmExportTypeVec = common::CSmartType<wasm_exporttype_vec_t, wasm_exporttype_vec_new_empty,
                                             wasm_exporttype_vec_delete>;
using WasmExternVec =
    common::CSmartType<wasm_extern_vec_t, wasm_extern_vec_new_empty, wasm_extern_vec_delete>;
using WasmValtypeVec =
    common::CSmartType<wasm_valtype_vec_t, wasm_valtype_vec_new_empty, wasm_valtype_vec_delete>;

} // namespace proxy_wasm::wasmtime
