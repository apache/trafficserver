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
 
#include <string>
#include <cstdint>
 
namespace proxy_wasm {
 
namespace internal {
 
// isolate those includes to prevent ::proxy_wasm namespace pollution with std
// namespace definitions.
#include "proxy_wasm_common.h"
#include "proxy_wasm_enums.h"
 
} // namespace internal
 
// proxy_wasm_common.h
using WasmResult = internal::WasmResult;
using WasmHeaderMapType = internal::WasmHeaderMapType;
using WasmBufferType = internal::WasmBufferType;
using WasmBufferFlags = internal::WasmBufferFlags;
using WasmStreamType = internal::WasmStreamType;
 
// proxy_wasm_enums.h
using LogLevel = internal::LogLevel;
using FilterStatus = internal::FilterStatus;
using FilterHeadersStatus = internal::FilterHeadersStatus;
using FilterMetadataStatus = internal::FilterMetadataStatus;
using FilterTrailersStatus = internal::FilterTrailersStatus;
using FilterDataStatus = internal::FilterDataStatus;
using GrpcStatus = internal::GrpcStatus;
using MetricType = internal::MetricType;
using CloseType = internal::CloseType;
 
} // namespace proxy_wasm
