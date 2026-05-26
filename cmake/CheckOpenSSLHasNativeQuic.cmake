#######################
#
#  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
#  agreements.  See the NOTICE file distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
#  (the "License"); you may not use this file except in compliance with the License.  You may obtain
#  a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software distributed under the License
#  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
#  or implied. See the License for the specific language governing permissions and limitations under
#  the License.
#
#######################

function(CHECK_OPENSSL_HAS_NATIVE_QUIC OUT_VAR OPENSSL_INCLUDE_DIR)
  set(CHECK_PROGRAM
      "
        #include <openssl/ssl.h>
        #include <openssl/quic.h>
        #include <cstdint>

        int main() {
            const SSL_METHOD *method = OSSL_QUIC_server_method();
            SSL *ssl = nullptr;
            uint64_t value = 0;
            return method == nullptr ||
                   SSL_new_listener == nullptr ||
                   SSL_listen == nullptr ||
                   SSL_handle_events == nullptr ||
                   SSL_accept_connection == nullptr ||
                   SSL_accept_stream == nullptr ||
                   SSL_new_stream == nullptr ||
                   SSL_stream_conclude == nullptr ||
                   SSL_get_stream_id == nullptr ||
                   SSL_get_stream_type == nullptr ||
                   SSL_get_stream_read_state == nullptr ||
                   SSL_get_stream_write_buf_avail(ssl, &value) ||
                   SSL_get_conn_close_info == nullptr ||
                   SSL_shutdown_ex == nullptr ||
                   SSL_set_default_stream_mode == nullptr ||
                   SSL_set_blocking_mode == nullptr ||
                   SSL_set_event_handling_mode(ssl, SSL_VALUE_EVENT_HANDLING_MODE_EXPLICIT) ||
                   SSL_set_feature_request_uint(ssl, SSL_VALUE_QUIC_IDLE_TIMEOUT, value) ||
                   SSL_set_incoming_stream_policy == nullptr;
        }
        "
  )
  set(CMAKE_REQUIRED_INCLUDES "${OPENSSL_INCLUDE_DIR}")
  set(CMAKE_REQUIRED_LIBRARIES OpenSSL::SSL OpenSSL::Crypto)
  include(CheckCXXSourceCompiles)
  check_cxx_source_compiles("${CHECK_PROGRAM}" ${OUT_VAR})
  set(${OUT_VAR}
      ${${OUT_VAR}}
      PARENT_SCOPE
  )
endfunction()
