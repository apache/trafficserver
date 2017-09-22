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

/*
This file contains the function of the runroot handler for TS_RUNROOT
handle the --run-root for every command or program

Goal: set up an ENV variable for Layout.cc to use as TS_RUNROOT sandbox
easy & clean

Example: ./traffic_server --run-root=/path/to/sandbox

Need a yaml file in the sandbox with key value pairs of all directory locations for other programs to use

Directories needed in the yaml file:
prefix, exec_prefix, includedir, localstatedir, bindir, logdir, mandir, sbindir, sysconfdir,
datadir, libexecdir, libdir, runtimedir, infodir, cachedir.
*/

#include "ts/ink_error.h"
#include "runroot.h"

#include <vector>
#include <fstream>
#include <set>
#include <unistd.h>

// the function for the checking of the yaml file in parent path
// if found return the parent path containing the yaml file
std::string
check_parent_path(const std::string &path, bool json)
{
  std::string whole_path = path;
  if (whole_path.back() == '/')
    whole_path.pop_back();

  while (whole_path != "") {
    whole_path                   = whole_path.substr(0, whole_path.find_last_of("/"));
    std::string parent_yaml_path = whole_path + "/runroot_path.yaml";
    std::ifstream parent_check_file;
    parent_check_file.open(parent_yaml_path);
    if (parent_check_file.good()) {
      if (!json)
        ink_notice("using parent of bin/current working dir");
      return whole_path;
    }
  }
  return {};
}

// until I get a <filesystem> impl in
bool
is_directory(const char *directory)
{
  struct stat buffer;
  int result = stat(directory, &buffer);
  return (!result && (S_IFDIR & buffer.st_mode)) ? true : false;
}

// handler for ts runroot
void
runroot_handler(const char **argv, bool json)
{
  std::string command = {};
  std::string arg     = {};
  std::string prefix  = "--run-root";

  int i = 0;
  while (argv[i]) {
    command = argv[i];
    if (command.substr(0, prefix.size()) == prefix) {
      arg = command;
      break;
    }
    i++;
  }
  if (arg.empty())
    return;

  // 1. check pass in path
  prefix += "=";
  if (arg.substr(0, prefix.size()) == prefix) {
    std::ifstream yaml_checkfile;
    std::string path = arg.substr(prefix.size(), arg.size() - 1);

    if (path.back() != '/')
      path.append("/");

    std::string yaml_path = path + "runroot_path.yaml";
    yaml_checkfile.open(yaml_path);
    if (yaml_checkfile.good()) {
      if (!json)
        ink_notice("using command line path as RUNROOT");
      setenv("USING_RUNROOT", path.c_str(), true);
      return;
    } else {
      if (!json)
        ink_warning("bad RUNROOT");
    }
  }
  // 2. argv provided invalid/no yaml file, then check env variable
  char *env_val = getenv("TS_RUNROOT");
  if ((env_val != nullptr) && is_directory(env_val)) {
    setenv("USING_RUNROOT", env_val, true);
    if (!json)
      ink_notice("using the environment variable TS_RUNROOT");
    return;
  }
  // 3. find parent path of bin/pwd to check
  char cwd[MAX_CWD_LEN]      = {0};
  char RealBinPath[PATH_MAX] = {0};
  if ((argv[0] != nullptr) && (getcwd(cwd, sizeof(cwd)) != nullptr) && (realpath(argv[0], RealBinPath) != nullptr)) {
    std::vector<std::string> TwoPath = {RealBinPath, cwd};
    for (auto it : TwoPath) {
      std::string path = check_parent_path(it);
      if (!path.empty()) {
        setenv("USING_RUNROOT", path.c_str(), true);
        return;
      }
    }
  }
}
