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

set(PACKAGE_NAME "Apache Traffic Server")
if(NOT DEFINED PACKAGE_VERSION)
  set(PACKAGE_VERSION ${TS_VERSION_STRING})
endif()
set(PACKAGE_BUGREPORT "dev@trafficserver.apache.org")

# TODO: Use layouts
set(prefix ${CMAKE_INSTALL_PREFIX})
set(exp_bindir ${CMAKE_INSTALL_PREFIX}/bin)
set(exp_runtimedir ${CMAKE_INSTALL_PREFIX}/var/trafficserver)
set(exp_logdir ${CMAKE_INSTALL_PREFIX}/var/log/trafficserver)
set(pkgsysuser ${TS_PKGSYSUSER})
set(pkgsysgroup ${TS_PKGSYSGROUP})

configure_file(trafficserver.in trafficserver @ONLY)
configure_file(trafficserver.conf.in trafficserver.conf @ONLY)
configure_file(trafficserver.service.in trafficserver.service @ONLY)

install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/trafficserver
  TYPE BIN
  PERMISSIONS
    OWNER_READ
    OWNER_WRITE
    OWNER_EXECUTE
    GROUP_READ
    GROUP_EXECUTE
    WORLD_READ
    WORLD_EXECUTE
)
