// Copyright 2020 Envoy Project Authors
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
//
#include "include/proxy-wasm/limits.h"
#include "include/proxy-wasm/pairs_util.h"
#include "include/proxy-wasm/wasm.h"

#include <openssl/rand.h>

#include <utility>

namespace proxy_wasm {

thread_local ContextBase *current_context_;
thread_local uint32_t effective_context_id_ = 0;

// Any currently executing Wasm call context.
ContextBase *contextOrEffectiveContext() {
  if (effective_context_id_ == 0) {
    return current_context_;
  }
  auto *effective_context = current_context_->wasm()->getContext(effective_context_id_);
  if (effective_context != nullptr) {
    return effective_context;
  }
  // The effective_context_id_ no longer exists, revert to the true context.
  return current_context_;
};

std::unordered_map<std::string, WasmForeignFunction> &foreignFunctions() {
  static auto *ptr = new std::unordered_map<std::string, WasmForeignFunction>;
  return *ptr;
}

WasmForeignFunction getForeignFunction(std::string_view function_name) {
  auto foreign_functions = foreignFunctions();
  auto it = foreign_functions.find(std::string(function_name));
  if (it != foreign_functions.end()) {
    return it->second;
  }
  return nullptr;
}

RegisterForeignFunction::RegisterForeignFunction(const std::string &name, WasmForeignFunction f) {
  foreignFunctions()[name] = std::move(f);
}

namespace exports {

// General ABI.

Word set_property(Word key_ptr, Word key_size, Word value_ptr, Word value_size) {
  auto *context = contextOrEffectiveContext();
  auto key = context->wasmVm()->getMemory(key_ptr, key_size);
  auto value = context->wasmVm()->getMemory(value_ptr, value_size);
  if (!key || !value) {
    return WasmResult::InvalidMemoryAccess;
  }
  return context->setProperty(key.value(), value.value());
}

// Generic selector
Word get_property(Word path_ptr, Word path_size, Word value_ptr_ptr, Word value_size_ptr) {
  auto *context = contextOrEffectiveContext();
  auto path = context->wasmVm()->getMemory(path_ptr, path_size);
  if (!path.has_value()) {
    return WasmResult::InvalidMemoryAccess;
  }
  std::string value;
  auto result = context->getProperty(path.value(), &value);
  if (result != WasmResult::Ok) {
    return result;
  }
  if (!context->wasm()->copyToPointerSize(value, value_ptr_ptr, value_size_ptr)) {
    return WasmResult::InvalidMemoryAccess;
  }
  return WasmResult::Ok;
}

Word get_configuration(Word value_ptr_ptr, Word value_size_ptr) {
  auto *context = contextOrEffectiveContext();
  auto value = context->getConfiguration();
  if (!context->wasm()->copyToPointerSize(value, value_ptr_ptr, value_size_ptr)) {
    return WasmResult::InvalidMemoryAccess;
  }
  return WasmResult::Ok;
}

Word get_status(Word code_ptr, Word value_ptr_ptr, Word value_size_ptr) {
  auto *context = contextOrEffectiveContext()->root_context();
  auto status = context->getStatus();
  if (!context->wasm()->setDatatype(code_ptr, status.first)) {
    return WasmResult::InvalidMemoryAccess;
  }
  if (!context->wasm()->copyToPointerSize(status.second, value_ptr_ptr, value_size_ptr)) {
    return WasmResult::InvalidMemoryAccess;
  }
  return WasmResult::Ok;
}

// HTTP

// Continue/Reply/Route
Word continue_request() {
  auto *context = contextOrEffectiveContext();
  return context->continueStream(WasmStreamType::Request);
}

Word continue_response() {
  auto *context = contextOrEffectiveContext();
  return context->continueStream(WasmStreamType::Response);
}

Word continue_stream(Word type) {
  auto *context = contextOrEffectiveContext();
  if (type > static_cast<uint64_t>(WasmStreamType::MAX)) {
    return WasmResult::BadArgument;
  }
  return context->continueStream(static_cast<WasmStreamType>(type.u64_));
}

Word close_stream(Word type) {
  auto *context = contextOrEffectiveContext();
  if (type > static_cast<uint64_t>(WasmStreamType::MAX)) {
    return WasmResult::BadArgument;
  }
  return context->closeStream(static_cast<WasmStreamType>(type.u64_));
}

Word send_local_response(Word response_code, Word response_code_details_ptr,
                         Word response_code_details_size, Word body_ptr, Word body_size,
                         Word additional_response_header_pairs_ptr,
                         Word additional_response_header_pairs_size, Word grpc_status) {
  auto *context = contextOrEffectiveContext();
  auto details =
      context->wasmVm()->getMemory(response_code_details_ptr, response_code_details_size);
  auto body = context->wasmVm()->getMemory(body_ptr, body_size);
  auto additional_response_header_pairs = context->wasmVm()->getMemory(
      additional_response_header_pairs_ptr, additional_response_header_pairs_size);
  if (!details || !body || !additional_response_header_pairs) {
    return WasmResult::InvalidMemoryAccess;
  }
  auto additional_headers = PairsUtil::toPairs(additional_response_header_pairs.value());
  context->sendLocalResponse(response_code, body.value(), std::move(additional_headers),
                             grpc_status, details.value());
  context->wasm()->stopNextIteration(true);
  return WasmResult::Ok;
}

Word clear_route_cache() {
  auto *context = contextOrEffectiveContext();
  context->clearRouteCache();
  return WasmResult::Ok;
}

Word set_effective_context(Word context_id) {
  auto *context = contextOrEffectiveContext();
  auto cid = static_cast<uint32_t>(context_id);
  auto *c = context->wasm()->getContext(cid);
  if (c == nullptr) {
    return WasmResult::BadArgument;
  }
  effective_context_id_ = cid;
  return WasmResult::Ok;
}

Word done() {
  auto *context = contextOrEffectiveContext();
  return context->wasm()->done(context);
}

Word call_foreign_function(Word function_name, Word function_name_size, Word arguments,
                           Word arguments_size, Word results, Word results_size) {
  auto *context = contextOrEffectiveContext();
  auto function = context->wasmVm()->getMemory(function_name, function_name_size);
  if (!function) {
    return WasmResult::InvalidMemoryAccess;
  }
  auto args_opt = context->wasmVm()->getMemory(arguments, arguments_size);
  if (!args_opt) {
    return WasmResult::InvalidMemoryAccess;
  }
  auto f = getForeignFunction(function.value());
  if (!f) {
    return WasmResult::NotFound;
  }
  auto &wasm = *context->wasm();
  auto &args = args_opt.value();
  uint64_t address = 0;
  void *result = nullptr;
  size_t result_size = 0;
  auto res = f(wasm, args, [&wasm, &address, &result, &result_size, results](size_t s) -> void * {
    if (results != 0U) {
      result = wasm.allocMemory(s, &address);
    } else {
      // If the caller does not want the results, allocate a temporary buffer for them.
      result = ::malloc(s);
    }
    result_size = s;
    return result;
  });
  if (results != 0U && !context->wasmVm()->setWord(results, Word(address))) {
    return WasmResult::InvalidMemoryAccess;
  }
  if (results_size != 0U && !context->wasmVm()->setWord(results_size, Word(result_size))) {
    return WasmResult::InvalidMemoryAccess;
  }
  if (results == 0U) {
    ::free(result);
  }
  return res;
}

// SharedData
Word get_shared_data(Word key_ptr, Word key_size, Word value_ptr_ptr, Word value_size_ptr,
                     Word cas_ptr) {
  auto *context = contextOrEffectiveContext();
  auto key = context->wasmVm()->getMemory(key_ptr, key_size);
  if (!key) {
    return WasmResult::InvalidMemoryAccess;
  }
  std::pair<std::string, uint32_t> data;
  WasmResult result = context->getSharedData(key.value(), &data);
  if (result != WasmResult::Ok) {
    return result;
  }
  if (!context->wasm()->copyToPointerSize(data.first, value_ptr_ptr, value_size_ptr)) {
    return WasmResult::InvalidMemoryAccess;
  }
  if (!context->wasmVm()->setMemory(cas_ptr, sizeof(uint32_t), &data.second)) {
    return WasmResult::InvalidMemoryAccess;
  }
  return WasmResult::Ok;
}

Word set_shared_data(Word key_ptr, Word key_size, Word value_ptr, Word value_size, Word cas) {
  auto *context = contextOrEffectiveContext();
  auto key = context->wasmVm()->getMemory(key_ptr, key_size);
  auto value = context->wasmVm()->getMemory(value_ptr, value_size);
  if (!key || !value) {
    return WasmResult::InvalidMemoryAccess;
  }
  return context->setSharedData(key.value(), value.value(), cas);
}

Word register_shared_queue(Word queue_name_ptr, Word queue_name_size, Word token_ptr) {
  auto *context = contextOrEffectiveContext();
  auto queue_name = context->wasmVm()->getMemory(queue_name_ptr, queue_name_size);
  if (!queue_name) {
    return WasmResult::InvalidMemoryAccess;
  }
  uint32_t token;
  auto result = context->registerSharedQueue(queue_name.value(), &token);
  if (result != WasmResult::Ok) {
    return result;
  }
  if (!context->wasm()->setDatatype(token_ptr, token)) {
    return WasmResult::InvalidMemoryAccess;
  }
  return WasmResult::Ok;
}

Word dequeue_shared_queue(Word token, Word data_ptr_ptr, Word data_size_ptr) {
  auto *context = contextOrEffectiveContext();
  std::string data;
  WasmResult result = context->dequeueSharedQueue(token.u32(), &data);
  if (result != WasmResult::Ok) {
    return result;
  }
  if (!context->wasm()->copyToPointerSize(data, data_ptr_ptr, data_size_ptr)) {
    return WasmResult::InvalidMemoryAccess;
  }
  return WasmResult::Ok;
}

Word resolve_shared_queue(Word vm_id_ptr, Word vm_id_size, Word queue_name_ptr,
                          Word queue_name_size, Word token_ptr) {
  auto *context = contextOrEffectiveContext();
  auto vm_id = context->wasmVm()->getMemory(vm_id_ptr, vm_id_size);
  auto queue_name = context->wasmVm()->getMemory(queue_name_ptr, queue_name_size);
  if (!vm_id || !queue_name) {
    return WasmResult::InvalidMemoryAccess;
  }
  uint32_t token = 0;
  auto result = context->lookupSharedQueue(vm_id.value(), queue_name.value(), &token);
  if (result != WasmResult::Ok) {
    return result;
  }
  if (!context->wasm()->setDatatype(token_ptr, token)) {
    return WasmResult::InvalidMemoryAccess;
  }
  return WasmResult::Ok;
}

Word enqueue_shared_queue(Word token, Word data_ptr, Word data_size) {
  auto *context = contextOrEffectiveContext();
  auto data = context->wasmVm()->getMemory(data_ptr, data_size);
  if (!data) {
    return WasmResult::InvalidMemoryAccess;
  }
  return context->enqueueSharedQueue(token.u32(), data.value());
}

// Header/Trailer/Metadata Maps
Word add_header_map_value(Word type, Word key_ptr, Word key_size, Word value_ptr, Word value_size) {
  if (type > static_cast<uint64_t>(WasmHeaderMapType::MAX)) {
    return WasmResult::BadArgument;
  }
  auto *context = contextOrEffectiveContext();
  auto key = context->wasmVm()->getMemory(key_ptr, key_size);
  auto value = context->wasmVm()->getMemory(value_ptr, value_size);
  if (!key || !value) {
    return WasmResult::InvalidMemoryAccess;
  }
  return context->addHeaderMapValue(static_cast<WasmHeaderMapType>(type.u64_), key.value(),
                                    value.value());
}

Word get_header_map_value(Word type, Word key_ptr, Word key_size, Word value_ptr_ptr,
                          Word value_size_ptr) {
  if (type > static_cast<uint64_t>(WasmHeaderMapType::MAX)) {
    return WasmResult::BadArgument;
  }
  auto *context = contextOrEffectiveContext();
  auto key = context->wasmVm()->getMemory(key_ptr, key_size);
  if (!key) {
    return WasmResult::InvalidMemoryAccess;
  }
  std::string_view value;
  auto result =
      context->getHeaderMapValue(static_cast<WasmHeaderMapType>(type.u64_), key.value(), &value);
  if (result != WasmResult::Ok) {
    return result;
  }
  if (!context->wasm()->copyToPointerSize(value, value_ptr_ptr, value_size_ptr)) {
    return WasmResult::InvalidMemoryAccess;
  }
  return WasmResult::Ok;
}

Word replace_header_map_value(Word type, Word key_ptr, Word key_size, Word value_ptr,
                              Word value_size) {
  if (type > static_cast<uint64_t>(WasmHeaderMapType::MAX)) {
    return WasmResult::BadArgument;
  }
  auto *context = contextOrEffectiveContext();
  auto key = context->wasmVm()->getMemory(key_ptr, key_size);
  auto value = context->wasmVm()->getMemory(value_ptr, value_size);
  if (!key || !value) {
    return WasmResult::InvalidMemoryAccess;
  }
  return context->replaceHeaderMapValue(static_cast<WasmHeaderMapType>(type.u64_), key.value(),
                                        value.value());
}

Word remove_header_map_value(Word type, Word key_ptr, Word key_size) {
  if (type > static_cast<uint64_t>(WasmHeaderMapType::MAX)) {
    return WasmResult::BadArgument;
  }
  auto *context = contextOrEffectiveContext();
  auto key = context->wasmVm()->getMemory(key_ptr, key_size);
  if (!key) {
    return WasmResult::InvalidMemoryAccess;
  }
  return context->removeHeaderMapValue(static_cast<WasmHeaderMapType>(type.u64_), key.value());
}

Word get_header_map_pairs(Word type, Word ptr_ptr, Word size_ptr) {
  if (type > static_cast<uint64_t>(WasmHeaderMapType::MAX)) {
    return WasmResult::BadArgument;
  }
  auto *context = contextOrEffectiveContext();
  Pairs pairs;
  auto result = context->getHeaderMapPairs(static_cast<WasmHeaderMapType>(type.u64_), &pairs);
  if (result != WasmResult::Ok) {
    return result;
  }
  if (pairs.empty()) {
    if (!context->wasm()->copyToPointerSize("", ptr_ptr, size_ptr)) {
      return WasmResult::InvalidMemoryAccess;
    }
    return WasmResult::Ok;
  }
  uint64_t size = PairsUtil::pairsSize(pairs);
  uint64_t ptr = 0;
  char *buffer = static_cast<char *>(context->wasm()->allocMemory(size, &ptr));
  if (buffer == nullptr) {
    return WasmResult::InvalidMemoryAccess;
  }
  if (!PairsUtil::marshalPairs(pairs, buffer, size)) {
    return WasmResult::InvalidMemoryAccess;
  }
  if (!context->wasmVm()->setWord(ptr_ptr, Word(ptr))) {
    return WasmResult::InvalidMemoryAccess;
  }
  if (!context->wasmVm()->setWord(size_ptr, Word(size))) {
    return WasmResult::InvalidMemoryAccess;
  }
  return WasmResult::Ok;
}

Word set_header_map_pairs(Word type, Word ptr, Word size) {
  if (type > static_cast<uint64_t>(WasmHeaderMapType::MAX)) {
    return WasmResult::BadArgument;
  }
  auto *context = contextOrEffectiveContext();
  auto data = context->wasmVm()->getMemory(ptr, size);
  if (!data) {
    return WasmResult::InvalidMemoryAccess;
  }
  return context->setHeaderMapPairs(static_cast<WasmHeaderMapType>(type.u64_),
                                    PairsUtil::toPairs(data.value()));
}

Word get_header_map_size(Word type, Word result_ptr) {
  if (type > static_cast<uint64_t>(WasmHeaderMapType::MAX)) {
    return WasmResult::BadArgument;
  }
  auto *context = contextOrEffectiveContext();
  uint32_t size;
  auto result = context->getHeaderMapSize(static_cast<WasmHeaderMapType>(type.u64_), &size);
  if (result != WasmResult::Ok) {
    return result;
  }
  if (!context->wasmVm()->setWord(result_ptr, Word(size))) {
    return WasmResult::InvalidMemoryAccess;
  }
  return WasmResult::Ok;
}

// Buffer
Word get_buffer_bytes(Word type, Word start, Word length, Word ptr_ptr, Word size_ptr) {
  if (type > static_cast<uint64_t>(WasmBufferType::MAX)) {
    return WasmResult::BadArgument;
  }
  auto *context = contextOrEffectiveContext();
  auto *buffer = context->getBuffer(static_cast<WasmBufferType>(type.u64_));
  if (buffer == nullptr) {
    return WasmResult::NotFound;
  }
  // Check for overflow.
  if (start > start + length) {
    return WasmResult::BadArgument;
  }
  // Don't overread.
  if (start > buffer->size()) {
    length = 0;
  } else if (start + length > buffer->size()) {
    length = buffer->size() - start;
  }
  if (length == 0) {
    if (!context->wasmVm()->setWord(ptr_ptr, Word(0))) {
      return WasmResult::InvalidMemoryAccess;
    }
    if (!context->wasmVm()->setWord(size_ptr, Word(0))) {
      return WasmResult::InvalidMemoryAccess;
    }
    return WasmResult::Ok;
  }
  return buffer->copyTo(context->wasm(), start, length, ptr_ptr, size_ptr);
}

Word get_buffer_status(Word type, Word length_ptr, Word flags_ptr) {
  if (type > static_cast<uint64_t>(WasmBufferType::MAX)) {
    return WasmResult::BadArgument;
  }
  auto *context = contextOrEffectiveContext();
  auto *buffer = context->getBuffer(static_cast<WasmBufferType>(type.u64_));
  if (buffer == nullptr) {
    return WasmResult::NotFound;
  }
  auto length = buffer->size();
  uint32_t flags = 0;
  if (!context->wasmVm()->setWord(length_ptr, Word(length))) {
    return WasmResult::InvalidMemoryAccess;
  }
  if (!context->wasm()->setDatatype(flags_ptr, flags)) {
    return WasmResult::InvalidMemoryAccess;
  }
  return WasmResult::Ok;
}

Word set_buffer_bytes(Word type, Word start, Word length, Word data_ptr, Word data_size) {
  if (type > static_cast<uint64_t>(WasmBufferType::MAX)) {
    return WasmResult::BadArgument;
  }
  auto *context = contextOrEffectiveContext();
  auto *buffer = context->getBuffer(static_cast<WasmBufferType>(type.u64_));
  if (buffer == nullptr) {
    return WasmResult::NotFound;
  }
  auto data = context->wasmVm()->getMemory(data_ptr, data_size);
  if (!data) {
    return WasmResult::InvalidMemoryAccess;
  }
  return buffer->copyFrom(start, length, data.value());
}

Word http_call(Word uri_ptr, Word uri_size, Word header_pairs_ptr, Word header_pairs_size,
               Word body_ptr, Word body_size, Word trailer_pairs_ptr, Word trailer_pairs_size,
               Word timeout_milliseconds, Word token_ptr) {
  auto *context = contextOrEffectiveContext()->root_context();
  auto uri = context->wasmVm()->getMemory(uri_ptr, uri_size);
  auto body = context->wasmVm()->getMemory(body_ptr, body_size);
  auto header_pairs = context->wasmVm()->getMemory(header_pairs_ptr, header_pairs_size);
  auto trailer_pairs = context->wasmVm()->getMemory(trailer_pairs_ptr, trailer_pairs_size);
  if (!uri || !body || !header_pairs || !trailer_pairs) {
    return WasmResult::InvalidMemoryAccess;
  }
  auto headers = PairsUtil::toPairs(header_pairs.value());
  auto trailers = PairsUtil::toPairs(trailer_pairs.value());
  uint32_t token = 0;
  // NB: try to write the token to verify the memory before starting the async
  // operation.
  if (!context->wasm()->setDatatype(token_ptr, token)) {
    return WasmResult::InvalidMemoryAccess;
  }
  auto result =
      context->httpCall(uri.value(), headers, body.value(), trailers, timeout_milliseconds, &token);
  context->wasm()->setDatatype(token_ptr, token);
  return result;
}

Word define_metric(Word metric_type, Word name_ptr, Word name_size, Word metric_id_ptr) {
  auto *context = contextOrEffectiveContext();
  auto name = context->wasmVm()->getMemory(name_ptr, name_size);
  if (!name) {
    return WasmResult::InvalidMemoryAccess;
  }
  uint32_t metric_id = 0;
  auto result =
      context->defineMetric(static_cast<uint32_t>(metric_type.u64_), name.value(), &metric_id);
  if (result != WasmResult::Ok) {
    return result;
  }
  if (!context->wasm()->setDatatype(metric_id_ptr, metric_id)) {
    return WasmResult::InvalidMemoryAccess;
  }
  return WasmResult::Ok;
}

Word increment_metric(Word metric_id, int64_t offset) {
  auto *context = contextOrEffectiveContext();
  return context->incrementMetric(metric_id, offset);
}

Word record_metric(Word metric_id, uint64_t value) {
  auto *context = contextOrEffectiveContext();
  return context->recordMetric(metric_id, value);
}

Word get_metric(Word metric_id, Word result_uint64_ptr) {
  auto *context = contextOrEffectiveContext();
  uint64_t value = 0;
  auto result = context->getMetric(metric_id, &value);
  if (result != WasmResult::Ok) {
    return result;
  }
  if (!context->wasm()->setDatatype(result_uint64_ptr, value)) {
    return WasmResult::InvalidMemoryAccess;
  }
  return WasmResult::Ok;
}

Word grpc_call(Word service_ptr, Word service_size, Word service_name_ptr, Word service_name_size,
               Word method_name_ptr, Word method_name_size, Word initial_metadata_ptr,
               Word initial_metadata_size, Word request_ptr, Word request_size,
               Word timeout_milliseconds, Word token_ptr) {
  auto *context = contextOrEffectiveContext()->root_context();
  auto service = context->wasmVm()->getMemory(service_ptr, service_size);
  auto service_name = context->wasmVm()->getMemory(service_name_ptr, service_name_size);
  auto method_name = context->wasmVm()->getMemory(method_name_ptr, method_name_size);
  auto initial_metadata_pairs =
      context->wasmVm()->getMemory(initial_metadata_ptr, initial_metadata_size);
  auto request = context->wasmVm()->getMemory(request_ptr, request_size);
  if (!service || !service_name || !method_name || !initial_metadata_pairs || !request) {
    return WasmResult::InvalidMemoryAccess;
  }
  uint32_t token = 0;
  auto initial_metadata = PairsUtil::toPairs(initial_metadata_pairs.value());
  auto result = context->grpcCall(service.value(), service_name.value(), method_name.value(),
                                  initial_metadata, request.value(),
                                  std::chrono::milliseconds(timeout_milliseconds), &token);
  if (result != WasmResult::Ok) {
    return result;
  }
  if (!context->wasm()->setDatatype(token_ptr, token)) {
    return WasmResult::InvalidMemoryAccess;
  }
  return WasmResult::Ok;
}

Word grpc_stream(Word service_ptr, Word service_size, Word service_name_ptr, Word service_name_size,
                 Word method_name_ptr, Word method_name_size, Word initial_metadata_ptr,
                 Word initial_metadata_size, Word token_ptr) {
  auto *context = contextOrEffectiveContext()->root_context();
  auto service = context->wasmVm()->getMemory(service_ptr, service_size);
  auto service_name = context->wasmVm()->getMemory(service_name_ptr, service_name_size);
  auto method_name = context->wasmVm()->getMemory(method_name_ptr, method_name_size);
  auto initial_metadata_pairs =
      context->wasmVm()->getMemory(initial_metadata_ptr, initial_metadata_size);
  if (!service || !service_name || !method_name || !initial_metadata_pairs) {
    return WasmResult::InvalidMemoryAccess;
  }
  uint32_t token = 0;
  auto initial_metadata = PairsUtil::toPairs(initial_metadata_pairs.value());
  auto result = context->grpcStream(service.value(), service_name.value(), method_name.value(),
                                    initial_metadata, &token);
  if (result != WasmResult::Ok) {
    return result;
  }
  if (!context->wasm()->setDatatype(token_ptr, token)) {
    return WasmResult::InvalidMemoryAccess;
  }
  return WasmResult::Ok;
}

Word grpc_cancel(Word token) {
  auto *context = contextOrEffectiveContext()->root_context();
  return context->grpcCancel(token);
}

Word grpc_close(Word token) {
  auto *context = contextOrEffectiveContext()->root_context();
  return context->grpcClose(token);
}

Word grpc_send(Word token, Word message_ptr, Word message_size, Word end_stream) {
  auto *context = contextOrEffectiveContext()->root_context();
  auto message = context->wasmVm()->getMemory(message_ptr, message_size);
  if (!message) {
    return WasmResult::InvalidMemoryAccess;
  }
  return context->grpcSend(token, message.value(), end_stream != 0U);
}

// __wasi_errno_t path_open(__wasi_fd_t fd, __wasi_lookupflags_t dirflags, const char *path,
// size_t path_len, __wasi_oflags_t oflags, __wasi_rights_t fs_rights_base, __wasi_rights_t
// fs_rights_inheriting, __wasi_fdflags_t fdflags, __wasi_fd_t *retptr0)
Word wasi_unstable_path_open(Word /*fd*/, Word /*dir_flags*/, Word /*path*/, Word /*path_len*/,
                             Word /*oflags*/, int64_t /*fs_rights_base*/,
                             int64_t /*fg_rights_inheriting*/, Word /*fd_flags*/,
                             Word /*nwritten_ptr*/) {
  return 44; // __WASI_ERRNO_NOENT
}

// __wasi_errno_t __wasi_fd_prestat_get(__wasi_fd_t fd, __wasi_prestat_t *retptr0)
Word wasi_unstable_fd_prestat_get(Word /*fd*/, Word /*buf_ptr*/) {
  return 8; // __WASI_ERRNO_BADF
}

// __wasi_errno_t __wasi_fd_prestat_dir_name(__wasi_fd_t fd, uint8_t * path, __wasi_size_t path_len)
Word wasi_unstable_fd_prestat_dir_name(Word /*fd*/, Word /*path_ptr*/, Word /*path_len*/) {
  return 52; // __WASI_ERRNO_ENOSYS
}

// Implementation of writev-like() syscall that redirects stdout/stderr to Envoy
// logs.
Word writevImpl(Word fd, Word iovs, Word iovs_len, Word *nwritten_ptr) {
  auto *context = contextOrEffectiveContext();

  // Read syscall args.
  uint64_t log_level;
  switch (fd) {
  case 1 /* stdout */:
    log_level = 2; // LogLevel::info
    break;
  case 2 /* stderr */:
    log_level = 4; // LogLevel::error
    break;
  default:
    return 8; // __WASI_EBADF
  }

  std::string s;
  for (size_t i = 0; i < iovs_len; i++) {
    auto memslice =
        context->wasmVm()->getMemory(iovs + i * 2 * sizeof(uint32_t), 2 * sizeof(uint32_t));
    if (!memslice) {
      return 21; // __WASI_EFAULT
    }
    const auto *iovec = reinterpret_cast<const uint32_t *>(memslice.value().data());
    if (iovec[1] != 0U /* buf_len */) {
      const auto buf = wasmtoh(iovec[0], context->wasmVm()->usesWasmByteOrder());
      const auto buf_len = wasmtoh(iovec[1], context->wasmVm()->usesWasmByteOrder());
      memslice = context->wasmVm()->getMemory(buf, buf_len);
      if (!memslice) {
        return 21; // __WASI_EFAULT
      }
      s.append(memslice.value().data(), memslice.value().size());
    }
  }

  size_t written = s.size();
  if (written != 0U) {
    // Remove trailing newline from the logs, if any.
    if (s[written - 1] == '\n') {
      s.erase(written - 1);
    }
    if (context->log(log_level, s) != WasmResult::Ok) {
      return 8; // __WASI_EBADF
    }
  }
  *nwritten_ptr = Word(written);
  return 0; // __WASI_ESUCCESS
}

// __wasi_errno_t __wasi_fd_write(_wasi_fd_t fd, const _wasi_ciovec_t *iov,
// size_t iovs_len, size_t* nwritten);
Word wasi_unstable_fd_write(Word fd, Word iovs, Word iovs_len, Word nwritten_ptr) {
  auto *context = contextOrEffectiveContext();

  Word nwritten(0);
  auto result = writevImpl(fd, iovs, iovs_len, &nwritten);
  if (result != 0) { // __WASI_ESUCCESS
    return result;
  }
  if (!context->wasmVm()->setWord(nwritten_ptr, Word(nwritten))) {
    return 21; // __WASI_EFAULT
  }
  return 0; // __WASI_ESUCCESS
}

// __wasi_errno_t __wasi_fd_read(_wasi_fd_t fd, const __wasi_iovec_t *iovs,
//    size_t iovs_len, __wasi_size_t *nread);
Word wasi_unstable_fd_read(Word /*fd*/, Word /*iovs_ptr*/, Word /*iovs_len*/, Word /*nread_ptr*/) {
  // Don't support reading of any files.
  return 52; // __WASI_ERRNO_ENOSYS
}

// __wasi_errno_t __wasi_fd_seek(__wasi_fd_t fd, __wasi_filedelta_t offset,
// __wasi_whence_t whence,__wasi_filesize_t *newoffset);
Word wasi_unstable_fd_seek(Word /*fd*/, int64_t /*offset*/, Word /*whence*/,
                           Word /*newoffset_ptr*/) {
  auto *context = contextOrEffectiveContext();
  context->error("wasi_unstable fd_seek");
  return 0;
}

// __wasi_errno_t __wasi_fd_close(__wasi_fd_t fd);
Word wasi_unstable_fd_close(Word /*fd*/) {
  auto *context = contextOrEffectiveContext();
  context->error("wasi_unstable fd_close");
  return 0;
}

// __wasi_errno_t __wasi_fd_fdstat_get(__wasi_fd_t fd, __wasi_fdstat_t *stat)
Word wasi_unstable_fd_fdstat_get(Word fd, Word statOut) {
  // We will only support this interface on stdout and stderr
  if (fd != 1 && fd != 2) {
    return 8; // __WASI_EBADF;
  }

  // The last word points to a 24-byte structure, which we
  // are mostly going to zero out.
  uint64_t wasi_fdstat[3];
  wasi_fdstat[0] = 0;
  wasi_fdstat[1] = 64; // This sets "fs_rights_base" to __WASI_RIGHTS_FD_WRITE
  wasi_fdstat[2] = 0;

  auto *context = contextOrEffectiveContext();
  context->wasmVm()->setMemory(statOut, 3 * sizeof(uint64_t), &wasi_fdstat);

  return 0; // __WASI_ESUCCESS
}

// __wasi_errno_t __wasi_environ_get(char **environ, char *environ_buf);
Word wasi_unstable_environ_get(Word environ_array_ptr, Word environ_buf) {
  auto *context = contextOrEffectiveContext();
  auto word_size = context->wasmVm()->getWordSize();
  const auto &envs = context->wasm()->envs();
  for (const auto &e : envs) {
    if (!context->wasmVm()->setWord(environ_array_ptr, environ_buf)) {
      return 21; // __WASI_EFAULT
    }

    std::string data;
    data.reserve(e.first.size() + e.second.size() + 2);
    data.append(e.first);
    data.append("=");
    data.append(e.second);
    data.append({0x0});
    if (!context->wasmVm()->setMemory(environ_buf, data.size(), data.c_str())) {
      return 21; // __WASI_EFAULT
    }
    environ_buf = environ_buf.u64_ + data.size();
    environ_array_ptr = environ_array_ptr.u64_ + word_size;
  }

  return 0; // __WASI_ESUCCESS
}

// __wasi_errno_t __wasi_environ_sizes_get(size_t *environ_count, size_t
// *environ_buf_size);
Word wasi_unstable_environ_sizes_get(Word count_ptr, Word buf_size_ptr) {
  auto *context = contextOrEffectiveContext();
  const auto &envs = context->wasm()->envs();
  if (!context->wasmVm()->setWord(count_ptr, Word(envs.size()))) {
    return 21; // __WASI_EFAULT
  }

  size_t size = 0;
  for (const auto &e : envs) {
    // len(key) + len(value) + 1('=') + 1(null terminator)
    size += e.first.size() + e.second.size() + 2;
  }
  if (!context->wasmVm()->setWord(buf_size_ptr, Word(size))) {
    return 21; // __WASI_EFAULT
  }
  return 0; // __WASI_ESUCCESS
}

// __wasi_errno_t __wasi_args_get(uint8_t **argv, uint8_t *argv_buf);
Word wasi_unstable_args_get(Word /*argv_array_ptr*/, Word /*argv_buf_ptr*/) {
  return 0; // __WASI_ESUCCESS
}

// __wasi_errno_t __wasi_args_sizes_get(size_t *argc, size_t *argv_buf_size);
Word wasi_unstable_args_sizes_get(Word argc_ptr, Word argv_buf_size_ptr) {
  auto *context = contextOrEffectiveContext();
  if (!context->wasmVm()->setWord(argc_ptr, Word(0))) {
    return 21; // __WASI_EFAULT
  }
  if (!context->wasmVm()->setWord(argv_buf_size_ptr, Word(0))) {
    return 21; // __WASI_EFAULT
  }
  return 0; // __WASI_ESUCCESS
}

// __wasi_errno_t __wasi_clock_time_get(uint32_t id, uint64_t precision, uint64_t* time);
Word wasi_unstable_clock_time_get(Word clock_id, uint64_t /*precision*/,
                                  Word result_time_uint64_ptr) {

  uint64_t result = 0;
  auto *context = contextOrEffectiveContext();
  switch (clock_id) {
  case 0 /* realtime */:
    result = context->getCurrentTimeNanoseconds();
    break;
  case 1 /* monotonic */:
    result = context->getMonotonicTimeNanoseconds();
    break;
  default:
    // process_cputime_id and thread_cputime_id are not supported yet.
    return 58; // __WASI_ENOTSUP
  }
  if (!context->wasm()->setDatatype(result_time_uint64_ptr, result)) {
    return 21; // __WASI_EFAULT
  }
  return 0; // __WASI_ESUCCESS
}

// __wasi_errno_t __wasi_random_get(uint8_t *buf, size_t buf_len);
Word wasi_unstable_random_get(Word result_buf_ptr, Word buf_len) {
  if (buf_len > PROXY_WASM_HOST_WASI_RANDOM_GET_MAX_SIZE_BYTES) {
    return 28; // __WASI_EINVAL
  }
  if (buf_len == 0) {
    return 0; // __WASI_ESUCCESS
  }
  auto *context = contextOrEffectiveContext();
  std::vector<uint8_t> random(buf_len);
  RAND_bytes(random.data(), random.size());
  if (!context->wasmVm()->setMemory(result_buf_ptr, random.size(), random.data())) {
    return 21; // __WASI_EFAULT
  }
  return 0; // __WASI_ESUCCESS
}

// void __wasi_proc_exit(__wasi_exitcode_t rval);
void wasi_unstable_proc_exit(Word /*exit_code*/) {
  auto *context = contextOrEffectiveContext();
  context->error("wasi_unstable proc_exit");
}

Word pthread_equal(Word left, Word right) { return static_cast<uint64_t>(left == right); }

void emscripten_notify_memory_growth(Word /*memory_index*/) {}

Word set_tick_period_milliseconds(Word period_milliseconds) {
  TimerToken token = 0;
  return contextOrEffectiveContext()->setTimerPeriod(std::chrono::milliseconds(period_milliseconds),
                                                     &token);
}

Word get_current_time_nanoseconds(Word result_uint64_ptr) {
  auto *context = contextOrEffectiveContext();
  uint64_t result = context->getCurrentTimeNanoseconds();
  if (!context->wasm()->setDatatype(result_uint64_ptr, result)) {
    return WasmResult::InvalidMemoryAccess;
  }
  return WasmResult::Ok;
}

Word log(Word level, Word address, Word size) {
  if (level > static_cast<uint64_t>(LogLevel::Max)) {
    return WasmResult::BadArgument;
  }
  auto *context = contextOrEffectiveContext();
  auto message = context->wasmVm()->getMemory(address, size);
  if (!message) {
    return WasmResult::InvalidMemoryAccess;
  }
  return context->log(level, message.value());
}

Word get_log_level(Word result_level_uint32_ptr) {
  auto *context = contextOrEffectiveContext();
  uint32_t level = context->getLogLevel();
  if (!context->wasm()->setDatatype(result_level_uint32_ptr, level)) {
    return WasmResult::InvalidMemoryAccess;
  }
  return WasmResult::Ok;
}

} // namespace exports
} // namespace proxy_wasm
