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
prefix, exec_prefix, includedir, localstatedir, bindir, logdir, sbindir, sysconfdir,
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

// the function for the checking of the yaml file in the passed in path
// if found return the path, if not return empty string
std::string
check_path(const std::string &path)
{
  std::string whole_path = path;
  std::string yaml_path  = Layout::relative_to(whole_path, "runroot_path.yml");
  std::ifstream check_file;
  check_file.open(yaml_path);
  if (check_file.good()) {
    return whole_path;
  }
  return {};
}

// the function for the checking of the yaml file in passed in directory or parent directory
// if found return the parent path containing the yaml file
std::string
check_parent_path(const std::string &path)
{
  std::string whole_path = path;
  if (whole_path.back() == '/') {
    whole_path.pop_back();
  }

  // go up to 4 level of parent directories
  for (int i = 0; i < 4; i++) {
    if (whole_path.empty()) {
      return {};
    }
    if (!check_path(whole_path).empty()) {
      return whole_path;
    }
    whole_path = whole_path.substr(0, whole_path.find_last_of("/"));
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
// this function set up using_runroot
void
runroot_handler(const char **argv, bool json)
{
  std::string prefix = "--run-root";
  std::string path;

  // check if we have --run-root...
  std::string arg = {};

  int i = 0;
  while (argv[i]) {
    std::string_view command = argv[i];
    if (command.substr(0, prefix.size()) == prefix) {
      arg = command.data();
      break;
    }
    i++;
  }

  // if --run-root is provided arg is not just --run-root
  if (!arg.empty() && arg != prefix) {
    // 1. pass in path
    prefix += "=";
    path = check_path(arg.substr(prefix.size(), arg.size() - 1));
    if (!path.empty()) {
      if (!json) {
        ink_notice("using command line path as RUNROOT");
      }
      using_runroot = path;
      return;
    } else {
      if (!json) {
        ink_warning("bad RUNROOT passed in");
      }
    }
  }

  // 2. check Environment variable
  char *env_val = getenv("TS_RUNROOT");
  if ((env_val != nullptr) && is_directory(env_val)) {
    path = check_path(env_val);
    if (!path.empty()) {
      using_runroot = env_val;
      if (!json) {
        ink_notice("using the environment variable TS_RUNROOT");
      }
      return;
    } else {
      if (!json) {
        ink_warning("bad Environment var: $TS_RUNROOT");
      }
    }
  }

  // 3. find cwd or parent path of cwd to check
  char cwd[PATH_MAX] = {0};
  if (getcwd(cwd, sizeof(cwd)) != nullptr) {
    path = check_parent_path(cwd);
    if (!path.empty()) {
      using_runroot = path;
      if (!json) {
        ink_notice("using cwd as TS_RUNROOT");
      }
      return;
    }
  }

  // 4. installed executable
  char RealBinPath[PATH_MAX] = {0};
  if ((argv[0] != nullptr) && realpath(argv[0], RealBinPath) != nullptr) {
    std::string bindir = RealBinPath;
    bindir             = bindir.substr(0, bindir.find_last_of("/")); // getting the bin dir not executable path
    path               = check_parent_path(bindir);
    if (!path.empty()) {
      using_runroot = path;
      if (!json) {
        ink_notice("using the installed dir as TS_RUNROOT");
      }
      return;
    }
  }
  // 5. if no runroot use, using default build
  return;
}

// return a map of all path with default layout
std::unordered_map<std::string, std::string>
runroot_map_default()
{
  std::unordered_map<std::string, std::string> map;

  map[LAYOUT_PREFIX]        = Layout::get()->prefix;
  map[LAYOUT_EXEC_PREFIX]   = Layout::get()->exec_prefix;
  map[LAYOUT_BINDIR]        = Layout::get()->bindir;
  map[LAYOUT_SBINDIR]       = Layout::get()->sbindir;
  map[LAYOUT_SYSCONFDIR]    = Layout::get()->sysconfdir;
  map[LAYOUT_DATADIR]       = Layout::get()->datadir;
  map[LAYOUT_INCLUDEDIR]    = Layout::get()->includedir;
  map[LAYOUT_LIBDIR]        = Layout::get()->libdir;
  map[LAYOUT_LIBEXECDIR]    = Layout::get()->libexecdir;
  map[LAYOUT_LOCALSTATEDIR] = Layout::get()->localstatedir;
  map[LAYOUT_RUNTIMEDIR]    = Layout::get()->runtimedir;
  map[LAYOUT_LOGDIR]        = Layout::get()->logdir;
  // mandir is not needed for runroot
  map[LAYOUT_CACHEDIR] = Layout::get()->cachedir;

  return map;
}

// return a map of all path in runroot_path.yml
RunrootMapType
runroot_map(const std::string &prefix)
{
  std::string yaml_path = Layout::relative_to(prefix, "runroot_path.yml");
  std::ifstream file;
  file.open(yaml_path);
  if (!file.good()) {
    ink_warning("Bad path '%s', continue with default value", prefix.c_str());
    return RunrootMapType{};
  }

  std::ifstream yamlfile(yaml_path);
  RunrootMapType map;
  std::string str;
  while (std::getline(yamlfile, str)) {
    int pos                 = str.find(':');
    map[str.substr(0, pos)] = str.substr(pos + 2);
  }

  // change it to absolute path in the map
  for (auto it : map) {
    if (it.second[0] != '/') {
      map[it.first] = Layout::relative_to(prefix, it.second);
    }
  }
  return map;
}

// check for the using of runroot
// a map of all path will be returned
// if we do not use runroot, a empty map will be returned.
RunrootMapType
check_runroot()
{
  if (using_runroot.empty()) {
    return RunrootMapType{};
  }

  int len = using_runroot.size();
  if ((len + 1) > PATH_NAME_MAX) {
    ink_fatal("runroot path is too big: %d, max %d\n", len, PATH_NAME_MAX - 1);
  }
  return runroot_map(using_runroot);
}
