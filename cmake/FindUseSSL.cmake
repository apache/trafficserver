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

if (USE_SSL)
  find_path(SSL_INCLUDE_DIR NAMES openssl/ssl.h PATHS ${USE_SSL}/include NO_DEFAULT_PATH)
  find_path(SSL_LIBRARY_DIR NAMES libssl.so libcrypto.so PATHS ${USE_SSL}/lib ${USE_SSL}/lib64 NO_DEFAULT_PATH)

  if (SSL_INCLUDE_DIR AND SSL_LIBRARY_DIR)
    set(SSL_FOUND TRUE)
    find_library(SSL_CRYPTO_LIBRARY NAMES crypto PATHS ${SSL_LIBRARY_DIR} NO_DEFAULT_PATH)
    find_library(SSL_SSL_LIBRARY NAMES ssl PATHS ${SSL_LIBRARY_DIR} NO_DEFAULT_PATH)
    set(SSL_LIBRARIES ${SSL_CRYPTO_LIBRARY} ${SSL_SSL_LIBRARY})
  endif()
endif(USE_SSL)
