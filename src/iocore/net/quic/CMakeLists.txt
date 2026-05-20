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

add_library(
  quic STATIC
  QUICApplication.cc
  QUICApplicationMap.cc
  QUICConfig.cc
  QUICContext.cc
  QUICConnectionTable.cc
  QUICGlobals.cc
  QUICTypes.cc
  QUICIntUtil.cc
  QUICStream.cc
  QUICStream.cc
  QUICStreamManager.cc
  QUICStreamAdapter.cc
  QUICStreamVCAdapter.cc
)
add_library(ts::quic ALIAS quic)

target_link_libraries(quic PUBLIC ts::inkevent ts::inknet ts::tscore OpenSSL::Crypto OpenSSL::SSL quiche::quiche)

if(TS_OPENSSL_QUIC_TLS_CBS_COMPAT)
  add_library(openssl_quic_compat STATIC OpenSSLQuicCompat.cc)
  add_library(ts::openssl_quic_compat ALIAS openssl_quic_compat)
  target_link_libraries(openssl_quic_compat PUBLIC OpenSSL::SSL OpenSSL::Crypto)
  set_property(
    TARGET quiche::quiche
    APPEND
    PROPERTY INTERFACE_LINK_LIBRARIES ts::openssl_quic_compat
  )
endif()
