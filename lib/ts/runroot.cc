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
datadir, libexecdir, libdir, runtimedir, cachedir.
*/

#include "ts/ink_error.h"
#include "ts/I_Layout.h"
#include "runroot.h"

#include <vector>
#include <fstream>
#include <set>
#include <unistd.h>

static std::string using_runroot = {};

// the function for the checking of the yaml file in parent path
// if found return the parent path containing the yaml file
std::string
check_parent_path(const std::string &path, bool json)
{
  std::string whole_path = path;
  if (whole_path.back() == '/')
    whole_path.pop_back();

  whole_path                   = whole_path.substr(0, whole_path.find_last_of("/"));
  std::string parent_yaml_path = Layout::relative_to(whole_path, "runroot_path.yml");
  std::ifstream parent_check_file;
  parent_check_file.open(parent_yaml_path);
  if (parent_check_file.good()) {
    if (!json)
      ink_notice("using parent of bin/current working dir");
    return whole_path;
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
    std::string path      = arg.substr(prefix.size(), arg.size() - 1);
    std::string yaml_path = Layout::relative_to(path, "runroot_path.yml");
    yaml_checkfile.open(yaml_path);
    if (yaml_checkfile.good()) {
      if (!json)
        ink_notice("using command line path as RUNROOT");
      using_runroot = path;
      return;
    } else {
      if (!json)
        ink_warning("bad RUNROOT");
    }
  }
  // 2. argv provided invalid/no yaml file, then check env variable
  char *env_val = getenv("TS_RUNROOT");
  if ((env_val != nullptr) && is_directory(env_val)) {
    using_runroot = env_val;
    if (!json)
      ink_notice("using the environment variable TS_RUNROOT");
    return;
  }
  // 3. find parent path of bin/pwd to check
  char cwd[PATH_MAX]         = {0};
  char RealBinPath[PATH_MAX] = {0};
  if ((argv[0] != nullptr) && (getcwd(cwd, sizeof(cwd)) != nullptr) && (realpath(argv[0], RealBinPath) != nullptr)) {
    std::vector<std::string> TwoPath = {RealBinPath, cwd};
    for (auto it : TwoPath) {
      std::string path = check_parent_path(it);
      if (!path.empty()) {
        using_runroot = path;
        return;
      }
    }
  }
}

// return a map of all path in runroot_path.yml
std::unordered_map<std::string, std::string>
runroot_map(std::string &yaml_path, std::string &prefix)
{
  std::ifstream file;
  file.open(yaml_path);
  if (!file.good()) {
    ink_warning("Bad env path, continue with default value");
    return std::unordered_map<std::string, std::string>{};
  }

  std::ifstream yamlfile(yaml_path);
  std::unordered_map<std::string, std::string> runroot_map;
  std::string str;
  while (std::getline(yamlfile, str)) {
    int pos = str.find(':');
    runroot_map[str.substr(0, pos)] = str.substr(pos + 2);
  }

  // change it to absolute path in the map
  for (auto it : runroot_map) {
    if (it.second[0] != '/') {
      runroot_map[it.first] = Layout::relative_to(prefix, it.second);
    }
  }
  return runroot_map;
}

// check for the using of runroot
// a map of all path will be returned
// if we do not use runroot, a empty map will be returned.
std::unordered_map<std::string, std::string>
check_runroot()
{
  if (using_runroot.empty()) {
    return std::unordered_map<std::string, std::string>{};
  }

  std::string env_path = using_runroot;
  int len              = env_path.size();
  if ((len + 1) > PATH_NAME_MAX) {
    ink_fatal("TS_RUNROOT environment variable is too big: %d, max %d\n", len, PATH_NAME_MAX - 1);
  }
  std::string yaml_path = Layout::relative_to(env_path, "runroot_path.yml");

  return runroot_map(yaml_path, env_path);
}
