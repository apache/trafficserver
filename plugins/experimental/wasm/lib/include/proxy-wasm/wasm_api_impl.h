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

// Required by "proxy_wasm_api.h" included within null_plugin namespace.

#ifdef PROXY_WASM_PROTOBUF
#include "google/protobuf/message_lite.h"
#endif

#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "include/proxy-wasm/exports.h"
#include "include/proxy-wasm/word.h"

namespace proxy_wasm {
namespace null_plugin {

#include "proxy_wasm_enums.h"
#include "proxy_wasm_common.h"

#define WS(_x) Word(static_cast<uint64_t>(_x))
#define WR(_x) Word(reinterpret_cast<uint64_t>(_x))

inline WasmResult wordToWasmResult(Word w) { return static_cast<WasmResult>(w.u64_); }

// Configuration and Status
inline WasmResult proxy_get_configuration(const char **configuration_ptr,
                                          size_t *configuration_size) {
  return wordToWasmResult(
      exports::get_configuration(WR(configuration_ptr), WR(configuration_size)));
}

inline WasmResult proxy_get_status(uint32_t *code_ptr, const char **ptr, size_t *size) {
  return wordToWasmResult(exports::get_status(WR(code_ptr), WR(ptr), WR(size)));
}

// Logging
inline WasmResult proxy_log(LogLevel level, const char *logMessage, size_t messageSize) {
  return wordToWasmResult(exports::log(WS(level), WR(logMessage), WS(messageSize)));
}
inline WasmResult proxy_get_log_level(LogLevel *level) {
  return wordToWasmResult(exports::get_log_level(WR(level)));
}

// Timer
inline WasmResult proxy_set_tick_period_milliseconds(uint64_t millisecond) {
  return wordToWasmResult(exports::set_tick_period_milliseconds(Word(millisecond)));
}
inline WasmResult proxy_get_current_time_nanoseconds(uint64_t *result) {
  return wordToWasmResult(exports::get_current_time_nanoseconds(WR(result)));
}

// State accessors
inline WasmResult proxy_get_property(const char *path_ptr, size_t path_size,
                                     const char **value_ptr_ptr, size_t *value_size_ptr) {
  return wordToWasmResult(
      exports::get_property(WR(path_ptr), WS(path_size), WR(value_ptr_ptr), WR(value_size_ptr)));
}
inline WasmResult proxy_set_property(const char *key_ptr, size_t key_size, const char *value_ptr,
                                     size_t value_size) {
  return wordToWasmResult(
      exports::set_property(WR(key_ptr), WS(key_size), WR(value_ptr), WS(value_size)));
}

// Continue
inline WasmResult proxy_continue_request() { return wordToWasmResult(exports::continue_request()); }
inline WasmResult proxy_continue_response() {
  return wordToWasmResult(exports::continue_response());
}
inline WasmResult proxy_continue_stream(WasmStreamType stream_type) {
  return wordToWasmResult(exports::continue_stream(WS(stream_type)));
}
inline WasmResult proxy_close_stream(WasmStreamType stream_type) {
  return wordToWasmResult(exports::close_stream(WS(stream_type)));
}
inline WasmResult
proxy_send_local_response(uint32_t response_code, const char *response_code_details_ptr,
                          size_t response_code_details_size, const char *body_ptr, size_t body_size,
                          const char *additional_response_header_pairs_ptr,
                          size_t additional_response_header_pairs_size, uint32_t grpc_status) {
  return wordToWasmResult(exports::send_local_response(
      WS(response_code), WR(response_code_details_ptr), WS(response_code_details_size),
      WR(body_ptr), WS(body_size), WR(additional_response_header_pairs_ptr),
      WS(additional_response_header_pairs_size), WS(grpc_status)));
}

inline WasmResult proxy_clear_route_cache() {
  return wordToWasmResult(exports::clear_route_cache());
}

// SharedData
inline WasmResult proxy_get_shared_data(const char *key_ptr, size_t key_size,
                                        const char **value_ptr, size_t *value_size, uint32_t *cas) {
  return wordToWasmResult(
      exports::get_shared_data(WR(key_ptr), WS(key_size), WR(value_ptr), WR(value_size), WR(cas)));
}
//  If cas != 0 and cas != the current cas for 'key' return false, otherwise set the value and
//  return true.
inline WasmResult proxy_set_shared_data(const char *key_ptr, size_t key_size, const char *value_ptr,
                                        size_t value_size, uint64_t cas) {
  return wordToWasmResult(
      exports::set_shared_data(WR(key_ptr), WS(key_size), WR(value_ptr), WS(value_size), WS(cas)));
}

// SharedQueue
// Note: Registering the same queue_name will overwrite the old registration while preseving any
// pending data. Consequently it should typically be followed by a call to
// proxy_dequeue_shared_queue. Returns unique token for the queue.
inline WasmResult proxy_register_shared_queue(const char *queue_name_ptr, size_t queue_name_size,
                                              uint32_t *token) {
  return wordToWasmResult(
      exports::register_shared_queue(WR(queue_name_ptr), WS(queue_name_size), WR(token)));
}
// Returns unique token for the queue.
inline WasmResult proxy_resolve_shared_queue(const char *vm_id_ptr, size_t vm_id_size,
                                             const char *queue_name_ptr, size_t queue_name_size,
                                             uint32_t *token) {
  return wordToWasmResult(exports::resolve_shared_queue(
      WR(vm_id_ptr), WS(vm_id_size), WR(queue_name_ptr), WS(queue_name_size), WR(token)));
}
// Returns true on end-of-stream (no more data available).
inline WasmResult proxy_dequeue_shared_queue(uint32_t token, const char **data_ptr,
                                             size_t *data_size) {
  return wordToWasmResult(exports::dequeue_shared_queue(WS(token), WR(data_ptr), WR(data_size)));
}
// Returns false if the queue was not found and the data was not enqueued.
inline WasmResult proxy_enqueue_shared_queue(uint32_t token, const char *data_ptr,
                                             size_t data_size) {
  return wordToWasmResult(exports::enqueue_shared_queue(WS(token), WR(data_ptr), WS(data_size)));
}

// Buffer
inline WasmResult proxy_get_buffer_bytes(WasmBufferType type, uint64_t start, uint64_t length,
                                         const char **ptr, size_t *size) {
  return wordToWasmResult(
      exports::get_buffer_bytes(WS(type), WS(start), WS(length), WR(ptr), WR(size)));
}

inline WasmResult proxy_get_buffer_status(WasmBufferType type, size_t *length_ptr,
                                          uint32_t *flags_ptr) {
  return wordToWasmResult(exports::get_buffer_status(WS(type), WR(length_ptr), WR(flags_ptr)));
}

inline WasmResult proxy_set_buffer_bytes(WasmBufferType type, uint64_t start, uint64_t length,
                                         const char *data, size_t size) {
  return wordToWasmResult(
      exports::set_buffer_bytes(WS(type), WS(start), WS(length), WR(data), WS(size)));
}

// Headers/Trailers/Metadata Maps
inline WasmResult proxy_add_header_map_value(WasmHeaderMapType type, const char *key_ptr,
                                             size_t key_size, const char *value_ptr,
                                             size_t value_size) {
  return wordToWasmResult(exports::add_header_map_value(WS(type), WR(key_ptr), WS(key_size),
                                                        WR(value_ptr), WS(value_size)));
}
inline WasmResult proxy_get_header_map_value(WasmHeaderMapType type, const char *key_ptr,
                                             size_t key_size, const char **value_ptr,
                                             size_t *value_size) {
  return wordToWasmResult(exports::get_header_map_value(WS(type), WR(key_ptr), WS(key_size),
                                                        WR(value_ptr), WR(value_size)));
}
inline WasmResult proxy_get_header_map_pairs(WasmHeaderMapType type, const char **ptr,
                                             size_t *size) {
  return wordToWasmResult(exports::get_header_map_pairs(WS(type), WR(ptr), WR(size)));
}
inline WasmResult proxy_set_header_map_pairs(WasmHeaderMapType type, const char *ptr, size_t size) {
  return wordToWasmResult(exports::set_header_map_pairs(WS(type), WR(ptr), WS(size)));
}
inline WasmResult proxy_replace_header_map_value(WasmHeaderMapType type, const char *key_ptr,
                                                 size_t key_size, const char *value_ptr,
                                                 size_t value_size) {
  return wordToWasmResult(exports::replace_header_map_value(WS(type), WR(key_ptr), WS(key_size),
                                                            WR(value_ptr), WS(value_size)));
}
inline WasmResult proxy_remove_header_map_value(WasmHeaderMapType type, const char *key_ptr,
                                                size_t key_size) {
  return wordToWasmResult(exports::remove_header_map_value(WS(type), WR(key_ptr), WS(key_size)));
}
inline WasmResult proxy_get_header_map_size(WasmHeaderMapType type, size_t *size) {
  return wordToWasmResult(exports::get_header_map_size(WS(type), WR(size)));
}

// HTTP
// Returns token, used in callback onHttpCallResponse
inline WasmResult proxy_http_call(const char *uri_ptr, size_t uri_size, void *header_pairs_ptr,
                                  size_t header_pairs_size, const char *body_ptr, size_t body_size,
                                  void *trailer_pairs_ptr, size_t trailer_pairs_size,
                                  uint64_t timeout_milliseconds, uint32_t *token_ptr) {
  return wordToWasmResult(exports::http_call(WR(uri_ptr), WS(uri_size), WR(header_pairs_ptr),
                                             WS(header_pairs_size), WR(body_ptr), WS(body_size),
                                             WR(trailer_pairs_ptr), WS(trailer_pairs_size),
                                             WS(timeout_milliseconds), WR(token_ptr)));
}
// gRPC
// Returns token, used in gRPC callbacks (onGrpc...)
inline WasmResult proxy_grpc_call(const char *service_ptr, size_t service_size,
                                  const char *service_name_ptr, size_t service_name_size,
                                  const char *method_name_ptr, size_t method_name_size,
                                  void *initial_metadata_ptr, size_t initial_metadata_size,
                                  const char *request_ptr, size_t request_size,
                                  uint64_t timeout_milliseconds, uint32_t *token_ptr) {
  return wordToWasmResult(
      exports::grpc_call(WR(service_ptr), WS(service_size), WR(service_name_ptr),
                         WS(service_name_size), WR(method_name_ptr), WS(method_name_size),
                         WR(initial_metadata_ptr), WS(initial_metadata_size), WR(request_ptr),
                         WS(request_size), WS(timeout_milliseconds), WR(token_ptr)));
}
inline WasmResult proxy_grpc_stream(const char *service_ptr, size_t service_size,
                                    const char *service_name_ptr, size_t service_name_size,
                                    const char *method_name_ptr, size_t method_name_size,
                                    void *initial_metadata_ptr, size_t initial_metadata_size,
                                    uint32_t *token_ptr) {
  return wordToWasmResult(
      exports::grpc_stream(WR(service_ptr), WS(service_size), WR(service_name_ptr),
                           WS(service_name_size), WR(method_name_ptr), WS(method_name_size),
                           WR(initial_metadata_ptr), WS(initial_metadata_size), WR(token_ptr)));
}
inline WasmResult proxy_grpc_cancel(uint64_t token) {
  return wordToWasmResult(exports::grpc_cancel(WS(token)));
}
inline WasmResult proxy_grpc_close(uint64_t token) {
  return wordToWasmResult(exports::grpc_close(WS(token)));
}
inline WasmResult proxy_grpc_send(uint64_t token, const char *message_ptr, size_t message_size,
                                  uint64_t end_stream) {
  return wordToWasmResult(
      exports::grpc_send(WS(token), WR(message_ptr), WS(message_size), WS(end_stream)));
}

// Metrics
// Returns a metric_id which can be used to report a metric. On error returns 0.
inline WasmResult proxy_define_metric(MetricType type, const char *name_ptr, size_t name_size,
                                      uint32_t *metric_id) {
  return wordToWasmResult(
      exports::define_metric(WS(type), WR(name_ptr), WS(name_size), WR(metric_id)));
}
inline WasmResult proxy_increment_metric(uint32_t metric_id, int64_t offset) {
  return wordToWasmResult(exports::increment_metric(WS(metric_id), offset));
}
inline WasmResult proxy_record_metric(uint32_t metric_id, uint64_t value) {
  return wordToWasmResult(exports::record_metric(WS(metric_id), value));
}
inline WasmResult proxy_get_metric(uint32_t metric_id, uint64_t *value) {
  return wordToWasmResult(exports::get_metric(WS(metric_id), WR(value)));
}

// System
inline WasmResult proxy_set_effective_context(uint64_t context_id) {
  return wordToWasmResult(exports::set_effective_context(WS(context_id)));
}
inline WasmResult proxy_done() { return wordToWasmResult(exports::done()); }

inline WasmResult proxy_call_foreign_function(const char *function_name, size_t function_name_size,
                                              const char *arguments, size_t arguments_size,
                                              char **results, size_t *results_size) {
  return wordToWasmResult(exports::call_foreign_function(WR(function_name), WS(function_name_size),
                                                         WR(arguments), WS(arguments_size),
                                                         WR(results), WR(results_size)));
}

#undef WS
#undef WR

#include "proxy_wasm_api.h"

RootContext *getRoot(std::string_view root_id);
Context *getContext(uint32_t context_id);

} // namespace null_plugin
} // namespace proxy_wasm
