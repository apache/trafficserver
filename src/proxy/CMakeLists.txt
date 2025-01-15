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
  proxy STATIC
  private/SSLProxySession.cc
  CacheControl.cc
  ControlBase.cc
  ControlMatcher.cc
  HostStatus.cc
  HttpAPIHooks.cc
  IPAllow.cc
  ParentConsistentHash.cc
  ParentRoundRobin.cc
  ParentSelectionStrategy.cc
  ParentSelection.cc
  Plugin.cc
  PluginVC.cc
  ProtocolProbeSessionAccept.cc
  ProxySession.cc
  ProxyTransaction.cc
  ReverseProxy.cc
  Transform.cc
  FetchSM.cc
  PluginHttpConnect.cc
)
add_library(ts::proxy ALIAS proxy)

target_link_libraries(
  proxy
  PUBLIC ts::inkcache ts::inkevent ts::tsutil ts::tscore
  PRIVATE ts::rpcpublichandlers ts::jsonrpc_protocol ts::inkutils ts::tsapibackend
)

add_subdirectory(hdrs)
add_subdirectory(shared)
add_subdirectory(http)
add_subdirectory(http2)
add_subdirectory(logging)

if(TS_USE_QUIC)
  add_subdirectory(http3)
endif()
