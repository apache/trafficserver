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

# CheckSoMark.cmake
#
# This will define the following variables
#
#     TS_HAS_SO_MARK
#

set(CHECK_PROGRAM
    "
    #include <netinet/in.h>
    #include <netinet/ip.h>
    #include <netinet/tcp.h>
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <sys/un.h>

    int
    main (void)
    {
        setsockopt(0, SOL_SOCKET, SO_MARK, (void*)0, 0);
        return 0;
    }
    "
)

include(CheckCSourceCompiles)
check_c_source_compiles("${CHECK_PROGRAM}" TS_HAS_SO_MARK)
