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

add_library(inkdns STATIC DNS.cc DNSConnection.cc DNSEventIO.cc Inline.cc SplitDNS.cc)
add_library(ts::inkdns ALIAS inkdns)

target_include_directories(
  inkdns
  PRIVATE ${CMAKE_SOURCE_DIR}/include/iocore/dns
          ${CMAKE_SOURCE_DIR}/include/iocore/io_uring
          ${CMAKE_SOURCE_DIR}/include/iocore/net
          ${CMAKE_SOURCE_DIR}/include/iocore/eventsystem
          ${CMAKE_SOURCE_DIR}/include/proxy/http
          ${CMAKE_SOURCE_DIR}/include/proxy/hdrs
          # iocore/net private header
          ${CMAKE_SOURCE_DIR}/src/iocore/net
          ${CMAKE_SOURCE_DIR}/src/iocore/dns
)

target_link_libraries(inkdns PUBLIC ts::inkevent ts::inkhostdb ts::proxy ts::tscore)
