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

#include "include/proxy-wasm/context.h"
#include "include/proxy-wasm/wasm_vm.h"
#include "include/proxy-wasm/word.h"

namespace proxy_wasm {

class ContextBase;

// Any currently executing Wasm call context.
::proxy_wasm::ContextBase *contextOrEffectiveContext();

extern thread_local ContextBase *current_context_;

/**
 * WasmForeignFunction is used for registering host-specific host functions.
 * A foreign function can be registered via RegisterForeignFunction and available
 * to Wasm modules via proxy_call_foreign_function.
 * @param wasm is the WasmBase which the Wasm module is running on.
 * @param argument is the view to the argument to the function passed by the module.
 * @param alloc_result is used to allocate the result data of this foreign function.
 */
using WasmForeignFunction = std::function<WasmResult(
    WasmBase &wasm, std::string_view argument, std::function<void *(size_t size)> alloc_result)>;

/**
 * Used to get the foreign function registered via RegisterForeignFunction for a given name.
 * @param function_name is the name used to lookup the foreign function table.
 * @return a WasmForeignFunction if registered.
 */
WasmForeignFunction getForeignFunction(std::string_view function_name);

/**
 * RegisterForeignFunction is used to register a foreign function in the lookup table
 * used internally in getForeignFunction.
 */
struct RegisterForeignFunction {
  /**
   * @param function_name is the key for this foreign function.
   * @param f is the function instance.
   */
  RegisterForeignFunction(const std::string &function_name, WasmForeignFunction f);
};

namespace exports {

// ABI functions exported from host to wasm.

Word get_configuration(Word value_ptr_ptr, Word value_size_ptr);
Word get_status(Word code_ptr, Word value_ptr_ptr, Word value_size_ptr);
Word log(Word level, Word address, Word size);
Word get_log_level(Word result_level_uint32_ptr);
Word get_property(Word path_ptr, Word path_size, Word value_ptr_ptr, Word value_size_ptr);
Word set_property(Word key_ptr, Word key_size, Word value_ptr, Word value_size);
Word continue_request();
Word continue_response();
Word continue_stream(Word stream_type);
Word close_stream(Word stream_type);
Word send_local_response(Word response_code, Word response_code_details_ptr,
                         Word response_code_details_size, Word body_ptr, Word body_size,
                         Word additional_response_header_pairs_ptr,
                         Word additional_response_header_pairs_size, Word grpc_status);
Word clear_route_cache();
Word get_shared_data(Word key_ptr, Word key_size, Word value_ptr_ptr, Word value_size_ptr,
                     Word cas_ptr);
Word set_shared_data(Word key_ptr, Word key_size, Word value_ptr, Word value_size, Word cas);
Word register_shared_queue(Word queue_name_ptr, Word queue_name_size, Word token_ptr);
Word resolve_shared_queue(Word vm_id_ptr, Word vm_id_size, Word queue_name_ptr,
                          Word queue_name_size, Word token_ptr);
Word dequeue_shared_queue(Word token, Word data_ptr_ptr, Word data_size_ptr);
Word enqueue_shared_queue(Word token, Word data_ptr, Word data_size);
Word get_buffer_bytes(Word type, Word start, Word length, Word ptr_ptr, Word size_ptr);
Word get_buffer_status(Word type, Word length_ptr, Word flags_ptr);
Word set_buffer_bytes(Word type, Word start, Word length, Word data_ptr, Word data_size);
Word add_header_map_value(Word type, Word key_ptr, Word key_size, Word value_ptr, Word value_size);
Word get_header_map_value(Word type, Word key_ptr, Word key_size, Word value_ptr_ptr,
                          Word value_size_ptr);
Word replace_header_map_value(Word type, Word key_ptr, Word key_size, Word value_ptr,
                              Word value_size);
Word remove_header_map_value(Word type, Word key_ptr, Word key_size);
Word get_header_map_pairs(Word type, Word ptr_ptr, Word size_ptr);
Word set_header_map_pairs(Word type, Word ptr, Word size);
Word get_header_map_size(Word type, Word result_ptr);
Word getRequestBodyBufferBytes(Word start, Word length, Word ptr_ptr, Word size_ptr);
Word get_response_body_buffer_bytes(Word start, Word length, Word ptr_ptr, Word size_ptr);
Word http_call(Word uri_ptr, Word uri_size, Word header_pairs_ptr, Word header_pairs_size,
               Word body_ptr, Word body_size, Word trailer_pairs_ptr, Word trailer_pairs_size,
               Word timeout_milliseconds, Word token_ptr);
Word define_metric(Word metric_type, Word name_ptr, Word name_size, Word metric_id_ptr);
Word increment_metric(Word metric_id, int64_t offset);
Word record_metric(Word metric_id, uint64_t value);
Word get_metric(Word metric_id, Word result_uint64_ptr);
Word grpc_call(Word service_ptr, Word service_size, Word service_name_ptr, Word service_name_size,
               Word method_name_ptr, Word method_name_size, Word initial_metadata_ptr,
               Word initial_metadata_size, Word request_ptr, Word request_size,
               Word timeout_milliseconds, Word token_ptr);
Word grpc_stream(Word service_ptr, Word service_size, Word service_name_ptr, Word service_name_size,
                 Word method_name_ptr, Word method_name_size, Word initial_metadata_ptr,
                 Word initial_metadata_size, Word token_ptr);
Word grpc_cancel(Word token);
Word grpc_close(Word token);
Word grpc_send(Word token, Word message_ptr, Word message_size, Word end_stream);

Word set_tick_period_milliseconds(Word tick_period_milliseconds);
Word get_current_time_nanoseconds(Word result_uint64_ptr);

Word set_effective_context(Word context_id);
Word done();
Word call_foreign_function(Word function_name, Word function_name_size, Word arguments,
                           Word warguments_size, Word results, Word results_size);

// Runtime environment functions exported from envoy to wasm.

Word wasi_unstable_path_open(Word fd, Word dir_flags, Word path, Word path_len, Word oflags,
                             int64_t fs_rights_base, int64_t fg_rights_inheriting, Word fd_flags,
                             Word nwritten_ptr);
Word wasi_unstable_fd_prestat_get(Word fd, Word buf_ptr);
Word wasi_unstable_fd_prestat_dir_name(Word fd, Word path_ptr, Word path_len);
Word wasi_unstable_fd_write(Word fd, Word iovs, Word iovs_len, Word nwritten_ptr);
Word wasi_unstable_fd_read(Word, Word, Word, Word);
Word wasi_unstable_fd_seek(Word, int64_t, Word, Word);
Word wasi_unstable_fd_close(Word);
Word wasi_unstable_fd_fdstat_get(Word fd, Word statOut);
Word wasi_unstable_environ_get(Word, Word);
Word wasi_unstable_environ_sizes_get(Word count_ptr, Word buf_size_ptr);
Word wasi_unstable_args_get(Word argc_ptr, Word argv_buf_size_ptr);
Word wasi_unstable_args_sizes_get(Word argc_ptr, Word argv_buf_size_ptr);
void wasi_unstable_proc_exit(Word);
Word wasi_unstable_clock_time_get(Word, uint64_t, Word);
Word wasi_unstable_random_get(Word, Word);
Word pthread_equal(Word left, Word right);
void emscripten_notify_memory_growth(Word);

// Support for embedders, not exported to Wasm.

#define FOR_ALL_HOST_FUNCTIONS(_f)                                                                 \
  _f(log) _f(get_status) _f(set_property) _f(get_property) _f(send_local_response)                 \
      _f(get_shared_data) _f(set_shared_data) _f(register_shared_queue) _f(resolve_shared_queue)   \
          _f(dequeue_shared_queue) _f(enqueue_shared_queue) _f(get_header_map_value)               \
              _f(add_header_map_value) _f(replace_header_map_value) _f(remove_header_map_value)    \
                  _f(get_header_map_pairs) _f(set_header_map_pairs) _f(get_header_map_size)        \
                      _f(get_buffer_status) _f(get_buffer_bytes) _f(set_buffer_bytes)              \
                          _f(http_call) _f(grpc_call) _f(grpc_stream) _f(grpc_close)               \
                              _f(grpc_cancel) _f(grpc_send) _f(set_tick_period_milliseconds)       \
                                  _f(get_current_time_nanoseconds) _f(define_metric)               \
                                      _f(increment_metric) _f(record_metric) _f(get_metric)        \
                                          _f(set_effective_context) _f(done)                       \
                                              _f(call_foreign_function)

#define FOR_ALL_HOST_FUNCTIONS_ABI_SPECIFIC(_f)                                                    \
  _f(get_configuration) _f(continue_request) _f(continue_response) _f(clear_route_cache)           \
      _f(continue_stream) _f(close_stream) _f(get_log_level)

#define FOR_ALL_WASI_FUNCTIONS(_f)                                                                 \
  _f(fd_write) _f(fd_read) _f(fd_seek) _f(fd_close) _f(fd_fdstat_get) _f(environ_get)              \
      _f(environ_sizes_get) _f(args_get) _f(args_sizes_get) _f(clock_time_get) _f(random_get)      \
          _f(proc_exit) _f(path_open) _f(fd_prestat_get) _f(fd_prestat_dir_name)

// Helpers to generate a stub to pass to VM, in place of a restricted proxy-wasm capability.
#define _CREATE_PROXY_WASM_STUB(_fn)                                                               \
  template <typename F> struct _fn##Stub;                                                          \
  template <typename... Args> struct _fn##Stub<Word(Args...)> {                                    \
    static Word stub(Args...) {                                                                    \
      auto context = contextOrEffectiveContext();                                                  \
      context->wasmVm()->integration()->error(                                                     \
          "Attempted call to restricted proxy-wasm capability: proxy_" #_fn);                      \
      return WasmResult::InternalFailure;                                                          \
    }                                                                                              \
  };
FOR_ALL_HOST_FUNCTIONS(_CREATE_PROXY_WASM_STUB)
FOR_ALL_HOST_FUNCTIONS_ABI_SPECIFIC(_CREATE_PROXY_WASM_STUB)
#undef _CREATE_PROXY_WASM_STUB

// Helpers to generate a stub to pass to VM, in place of a restricted WASI capability.
#define _CREATE_WASI_STUB(_fn)                                                                     \
  template <typename F> struct _fn##Stub;                                                          \
  template <typename... Args> struct _fn##Stub<Word(Args...)> {                                    \
    static Word stub(Args...) {                                                                    \
      auto context = contextOrEffectiveContext();                                                  \
      context->wasmVm()->integration()->error(                                                     \
          "Attempted call to restricted WASI capability: " #_fn);                                  \
      return 76; /* __WASI_ENOTCAPABLE */                                                          \
    }                                                                                              \
  };                                                                                               \
  template <typename... Args> struct _fn##Stub<void(Args...)> {                                    \
    static void stub(Args...) {                                                                    \
      auto context = contextOrEffectiveContext();                                                  \
      context->wasmVm()->integration()->error(                                                     \
          "Attempted call to restricted WASI capability: " #_fn);                                  \
    }                                                                                              \
  };
FOR_ALL_WASI_FUNCTIONS(_CREATE_WASI_STUB)
#undef _CREATE_WASI_STUB

} // namespace exports
} // namespace proxy_wasm
