/** @file

  A brief file description

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

#pragma once

#include "tscore/ArgParser.h"
#include <unordered_map>

typedef std::unordered_map<std::string, std::string> RunrootMapType;

// structure for informaiton of the runroot passing around
struct LayoutEngine {
  // default output of all layouts
  void info();
  // the function of creating runroot
  void create_runroot();
  // the function for removing the runroot
  void remove_runroot();
  // the function of verifying runroot (including fix)
  void verify_runroot();
  // vector containing all directory names
  std::vector<std::string> const dir_vector = {LAYOUT_PREFIX,     LAYOUT_EXEC_PREFIX,   LAYOUT_BINDIR,     LAYOUT_SBINDIR,
                                               LAYOUT_SYSCONFDIR, LAYOUT_DATADIR,       LAYOUT_INCLUDEDIR, LAYOUT_LIBDIR,
                                               LAYOUT_LIBEXECDIR, LAYOUT_LOCALSTATEDIR, LAYOUT_RUNTIMEDIR, LAYOUT_LOGDIR,
                                               LAYOUT_CACHEDIR};
  // parser
  ts::ArgParser parser;
  // parsed arguments
  ts::Arguments arguments;
  // mordern argv
  std::vector<std::string> _argv;
};
