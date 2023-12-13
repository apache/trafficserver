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

add_executable(traffic_crashlog procinfo.cc backtrace.cc traffic_crashlog.cc)

target_link_libraries(traffic_crashlog PRIVATE ts::inkevent ts::records ts::tscore ts::tsutil)

if(TS_USE_REMOTE_UNWINDING)
  target_link_libraries(traffic_crashlog PRIVATE unwind::unwind)
endif()

install(TARGETS traffic_crashlog)
