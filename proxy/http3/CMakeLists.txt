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

add_library(http3 STATIC
    Http09App.cc
    Http3.cc
    Http3Config.cc
    Http3App.cc
    Http3Types.cc
    Http3SessionAccept.cc
    Http3Session.cc
    Http3Transaction.cc
    Http3DebugNames.cc
    Http3Frame.cc
    Http3FrameCollector.cc
    Http3FrameDispatcher.cc
    Http3HeaderFramer.cc
    Http3DataFramer.cc
    Http3HeaderVIOAdaptor.cc
    Http3ProtocolEnforcer.cc
    Http3StreamDataVIOAdaptor.cc
    QPACK.cc
)
add_library(ts::http3 ALIAS http3)

target_include_directories(http3 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(http3 PUBLIC ts::quic PRIVATE ts::proxy)

