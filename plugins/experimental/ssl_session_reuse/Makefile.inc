#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

# Only build if LIB_HIREDIS is set to a non-empty value
pkglib_LTLIBRARIES += experimental/ssl_session_reuse/ssl_session_reuse.la

experimental_ssl_session_reuse_ssl_session_reuse_la_SOURCES = \
         experimental/ssl_session_reuse/src/openssl_utils.cc \
         experimental/ssl_session_reuse/src/session_process.cc  \
         experimental/ssl_session_reuse/src/ssl_init.cc \
         experimental/ssl_session_reuse/src/config.cc \
         experimental/ssl_session_reuse/src/redis_endpoint.cc  \
         experimental/ssl_session_reuse/src/simple_pool.cc \
         experimental/ssl_session_reuse/src/ssl_key_utils.cc \
         experimental/ssl_session_reuse/src/connection.cc  \
         experimental/ssl_session_reuse/src/publish.cc \
         experimental/ssl_session_reuse/src/subscriber.cc \
  	 experimental/ssl_session_reuse/src/ats_ssl_plugin.cc

experimental_ssl_session_reuse_ssl_session_reuse_la_LIBADD = @LIB_HIREDIS@ 

