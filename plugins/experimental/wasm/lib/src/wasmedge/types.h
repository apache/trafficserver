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
#include "wasmedge/wasmedge.h"

namespace proxy_wasm::WasmEdge {

using WasmEdgeStorePtr = common::CSmartPtr<WasmEdge_StoreContext, WasmEdge_StoreDelete>;
using WasmEdgeVMPtr = common::CSmartPtr<WasmEdge_VMContext, WasmEdge_VMDelete>;
using WasmEdgeLoaderPtr = common::CSmartPtr<WasmEdge_LoaderContext, WasmEdge_LoaderDelete>;
using WasmEdgeValidatorPtr = common::CSmartPtr<WasmEdge_ValidatorContext, WasmEdge_ValidatorDelete>;
using WasmEdgeExecutorPtr = common::CSmartPtr<WasmEdge_ExecutorContext, WasmEdge_ExecutorDelete>;
using WasmEdgeASTModulePtr = common::CSmartPtr<WasmEdge_ASTModuleContext, WasmEdge_ASTModuleDelete>;
using WasmEdgeModulePtr =
    common::CSmartPtr<WasmEdge_ModuleInstanceContext, WasmEdge_ModuleInstanceDelete>;

} // namespace proxy_wasm::WasmEdge
