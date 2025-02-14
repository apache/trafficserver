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

add_atsplugin(otel_tracer otel_tracer.cc)
if(CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED OR CMAKE_CXX_LINK_GROUP_USING_RESCAN_SUPPORTED)
  string(JOIN "," opentelemetry_LIBRARIES_CSV ${opentelemetry_LIBRARIES})
  target_link_libraries(
    otel_tracer PRIVATE "$<LINK_GROUP:RESCAN,${opentelemetry_LIBRARIES_CSV},protobuf::libprotobuf,CURL::libcurl>"
  )
elseif(APPLE)
  target_link_libraries(otel_tracer PRIVATE ${opentelemetry_LIBRARIES} protobuf::libprotobuf CURL::libcurl)
else()
  target_link_libraries(
    otel_tracer PRIVATE -Wl,--start-group ${opentelemetry_LIBRARIES} protobuf::libprotobuf CURL::libcurl
                        -Wl,--end-group
  )
endif()
verify_global_plugin(otel_tracer)
