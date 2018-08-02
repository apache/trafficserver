/** @file

  A brief file prefix

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

// runroot handler for TS_RUNROOT
// detailed information in runroot.cc

#pragma once

#include <string>
#include <unordered_map>

// name of the directory declared globally
const std::string LAYOUT_PREFIX        = "prefix";
const std::string LAYOUT_EXEC_PREFIX   = "exec_prefix";
const std::string LAYOUT_BINDIR        = "bindir";
const std::string LAYOUT_SBINDIR       = "sbindir";
const std::string LAYOUT_SYSCONFDIR    = "sysconfdir";
const std::string LAYOUT_DATADIR       = "datadir";
const std::string LAYOUT_INCLUDEDIR    = "includedir";
const std::string LAYOUT_LIBDIR        = "libdir";
const std::string LAYOUT_LIBEXECDIR    = "libexecdir";
const std::string LAYOUT_LOCALSTATEDIR = "localstatedir";
const std::string LAYOUT_RUNTIMEDIR    = "runtimedir";
const std::string LAYOUT_LOGDIR        = "logdir";
const std::string LAYOUT_MANDIR        = "mandir";
const std::string LAYOUT_INFODIR       = "infodir";
const std::string LAYOUT_CACHEDIR      = "cachedir";

typedef std::unordered_map<std::string, std::string> RunrootMapType;

void runroot_handler(const char **argv, bool json = false);

// get a map from default layout
std::unordered_map<std::string, std::string> runroot_map_default();
// get runroot map from yaml path and prefix
RunrootMapType runroot_map(const std::string &prefix);

// help check runroot for layout
RunrootMapType check_runroot();

// To get the runroot value
std::string_view get_runroot();
