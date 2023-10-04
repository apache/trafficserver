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


add_subdirectory(eventsystem)
if (TS_USE_LINUX_IO_URING)
add_subdirectory(io_uring)
endif (TS_USE_LINUX_IO_URING)
add_subdirectory(net)
add_subdirectory(aio)
add_subdirectory(dns)
add_subdirectory(hostdb)
add_subdirectory(utils)
add_subdirectory(cache)

set(IOCORE_INCLUDE_DIRS
        ${CMAKE_SOURCE_DIR}/include/iocore/eventsystem
        ${CMAKE_SOURCE_DIR}/include/iocore/dns
        ${CMAKE_SOURCE_DIR}/include/iocore/io_uring
        ${CMAKE_SOURCE_DIR}/include/iocore/aio
        ${CMAKE_SOURCE_DIR}/include/iocore/net
        ${CMAKE_SOURCE_DIR}/include/iocore/cache
        ${CMAKE_SOURCE_DIR}/include/iocore/hostdb
        ${CMAKE_SOURCE_DIR}/include/iocore/utils PARENT_SCOPE)
