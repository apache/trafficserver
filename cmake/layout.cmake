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


# trafficserver default layout:
set(CMAKE_INSTALL_BINDIR
    "bin"
    CACHE STRING "bindir"
)
set(CMAKE_INSTALL_SBINDIR
    "bin"
    CACHE STRING "sbindir"
)

# trafficserver overrides lib64 to lib
set(CMAKE_INSTALL_LIBDIR
    "lib"
    CACHE STRING "libdir"
)
set(CMAKE_INSTALL_LIBEXECDIR
    "libexec/trafficserver"
    CACHE STRING "libexecdir"
)
set(CMAKE_INSTALL_SYSCONFDIR
    "etc/trafficserver"
    CACHE STRING "sysconfdir"
)
set(CMAKE_INSTALL_LOCALSTATEDIR
  "var"
  CACHE STRING "localstatedir"
)
set(CMAKE_INSTALL_RUNSTATEDIR
  "${CMAKE_INSTALL_LOCALSTATEDIR}/trafficserver"
  CACHE STRING "runstatedir"
)
set(CMAKE_INSTALL_DATAROOTDIR
    "share"
    CACHE STRING "datarootdir"
)
set(CMAKE_INSTALL_DATADIR
    "${CMAKE_INSTALL_DATAROOTDIR}/trafficserver"
    CACHE STRING "datadir"
)
set(CMAKE_INSTALL_DOCDIR
    "${CMAKE_INSTALL_DATAROOTDIR}/doc/trafficserver"
    CACHE STRING "docdir"
)
set(CMAKE_INSTALL_LOGDIR
  "${CMAKE_INSTALL_LOCALSTATEDIR}/log/trafficserver"
    CACHE STRING "logdir"
)
set(CMAKE_INSTALL_CACHEDIR
  "${CMAKE_INSTALL_LOCALSTATEDIR}/trafficserver"
    CACHE STRING "cachedir"
)
include(GNUInstallDirs)
