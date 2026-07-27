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
  esicore STATIC
  EsiGunzip.cc
  EsiGzip.cc
  EsiParser.cc
  EsiProcessor.cc
  Expression.cc
  HandlerManager.cc
  IncludeUrlValidator.cc
  Stats.cc
  Variables.cc
)
# esicore is a static library dlopen()ed as part of plugins (esi.so, combo_handler.so) alongside
# traffic_server. We do NOT link ts::tsutil here even though IncludeUrlValidator.cc references
# ts::Regex: if libtsutil.a were on the link line for esicore or its consumers, each plugin would
# statically embed tsutil globals (e.g., DbgCtl::_config_mode) and violate ODR against the same
# symbols in traffic_server at dlopen time (ASan detects_odr_violation). Instead, unresolved
# tsutil symbols are left in the plugin and resolved by the runtime linker against traffic_server
# at load time. The tsutil headers are visible via the global include_directories() in the
# top-level CMakeLists so compilation still succeeds.
target_link_libraries(esicore PUBLIC esi-common fetcher libswoc::libswoc)
target_include_directories(esicore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(esicore PROPERTIES POSITION_INDEPENDENT_CODE ON)
