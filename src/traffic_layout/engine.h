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

#include "file_system.h"

#include <vector>
#include <string>
#include <unordered_map>

#define RUNROOT_WORD_LENGTH 10
#define COPYSTYLE_WORD_LENGTH 12

typedef std::unordered_map<std::string, std::string> RunrootMapType;

// structure for informaiton of the runroot passing around
struct RunrootEngine {
  // the parsing function for traffic runroot program
  bool runroot_parse();

  // check the logic and see if everthing is fine
  void sanity_check();

  // the function for removing the runroot
  void clean_runroot();

  // the function of creating runroot
  void create_runroot();

  // the function of verifying runroot (including fix)
  void verify_runroot();

  // copy the stuff from original_root to ts_runroot
  // fill in the global map for yaml file emitting later
  void copy_runroot(const std::string &original_root, const std::string &ts_runroot);

  // the help message for runroot
  void runroot_help_message(const bool runflag, const bool cleanflag, const bool verifyflag);

  // the pass in arguments
  std::vector<std::string> _argv;
  // flags for command line parsing
  bool help_flag    = false;
  bool version_flag = false;
  bool run_flag     = false;
  bool clean_flag   = false;
  bool force_flag   = false;
  bool abs_flag     = false;
  bool verify_flag  = false;
  bool fix_flag     = false;
  // verify the default layout or not
  bool verify_default = false;
  // for parsing
  int command_num = 0;

  CopyStyle copy_style = HARD;

  // the path for create & remove
  std::string path;

  // vector containing all directory names
  std::vector<std::string> const dir_vector = {LAYOUT_PREFIX,     LAYOUT_EXEC_PREFIX,   LAYOUT_BINDIR,     LAYOUT_SBINDIR,
                                               LAYOUT_SYSCONFDIR, LAYOUT_DATADIR,       LAYOUT_INCLUDEDIR, LAYOUT_LIBDIR,
                                               LAYOUT_LIBEXECDIR, LAYOUT_LOCALSTATEDIR, LAYOUT_RUNTIMEDIR, LAYOUT_LOGDIR,
                                               LAYOUT_CACHEDIR};

  // map for yaml file emit
  RunrootMapType path_map;
};
