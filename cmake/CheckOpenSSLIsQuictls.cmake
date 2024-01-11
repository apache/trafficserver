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

function(CHECK_OPENSSL_IS_QUICTLS OUT_VAR OPENSSL_INCLUDE_DIR)
  set(CHECK_PROGRAM
      "
        #include <openssl/quic.h>

        #ifndef OPENSSL_INFO_QUIC
        #error check failed
        #endif

        int main() {
            return 0;
        }
        "
  )
  set(CMAKE_REQUIRED_INCLUDES "${OPENSSL_INCLUDE_DIR}")
  include(CheckCXXSourceCompiles)
  check_cxx_source_compiles("${CHECK_PROGRAM}" ${OUT_VAR})
endfunction()
