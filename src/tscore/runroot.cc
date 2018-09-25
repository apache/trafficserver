/** @file

  runroot.cc

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
  Please refer to traffic_layout document for the usage
*/

#include "tscore/ink_error.h"
#include "tscore/I_Layout.h"
#include "tscore/runroot.h"
#include <yaml-cpp/yaml.h>

static std::string runroot_file = {};

// this is a temporary approach and will be replaced when std::filesystem is in
bool
exists(const std::string &dir)
{
  struct stat buffer;
  int result = stat(dir.c_str(), &buffer);
  return (!result) ? true : false;
}

bool
is_directory(const std::string &directory)
{
  struct stat buffer;
  int result = stat(directory.c_str(), &buffer);
  return (!result && (S_IFDIR & buffer.st_mode)) ? true : false;
}

// the function for the checking of the yaml file in the passed in path
// if found return the path to the yaml file, if not return empty string.
static std::string
get_yaml_path(const std::string &path)
{
  // yaml_file 2 and 3 are for temporary use in case the change may break for people using runroot already
  // this can be removed in the future.
  std::string yaml_file;
  std::string yaml_file2;
  std::string yaml_file3;
  if (is_directory(path.c_str())) {
    std::string yaml_file(Layout::relative_to(path, "runroot.yaml"));
    if (exists(yaml_file)) {
      return yaml_file;
    }
    std::string yaml_file2(Layout::relative_to(path, "runroot.yml"));
    if (exists(yaml_file2)) {
      return yaml_file2;
    }
    std::string yaml_file3(Layout::relative_to(path, "runroot_path.yml"));
    if (exists(yaml_file3)) {
      return yaml_file3;
    }
  } else if (exists(path)) {
    return path;
  }
  return {};
}

// the function for the checking of the yaml file in passed in directory or parent directory
// if found return the parent path to the yaml file
static std::string
get_parent_yaml_path(const std::string &path)
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
    std::string yaml_file = get_yaml_path(whole_path);
    if (!yaml_file.empty()) {
      return yaml_file;
    }
    whole_path = whole_path.substr(0, whole_path.find_last_of("/"));
  }
  return {};
}

// handler for ts runroot
// this function set up runroot_file
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

  // if --run-root is provided
  if (!arg.empty() && arg != prefix) {
    // 1. pass in path
    prefix += "=";
    std::string value = arg.substr(prefix.size(), arg.size() - 1);
    path              = get_yaml_path(value);
    if (!path.empty()) {
      if (!json) {
        ink_notice("using command line path as RUNROOT");
      }
      runroot_file = path;
      return;
    } else if (!json) {
      ink_warning("Unable to access runroot: '%s'", value.c_str());
    }
  }

  // 2. check Environment variable
  char *env_val = getenv("TS_RUNROOT");
  if (env_val) {
    path = get_yaml_path(env_val);
    if (!path.empty()) {
      runroot_file = path;
      if (!json) {
        ink_notice("using the environment variable TS_RUNROOT");
      }
      return;
    } else if (!json) {
      ink_warning("Unable to access runroot: '%s' from $TS_RUNROOT", env_val);
    }
  }

  // 3. find cwd or parent path of cwd to check
  char cwd[PATH_MAX] = {0};
  if (getcwd(cwd, sizeof(cwd)) != nullptr) {
    path = get_parent_yaml_path(cwd);
    if (!path.empty()) {
      runroot_file = path;
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
    path               = get_parent_yaml_path(bindir);
    if (!path.empty()) {
      runroot_file = path;
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

// return a map of all path in runroot.yaml
RunrootMapType
runroot_map(const std::string &file)
{
  RunrootMapType map;
  try {
    YAML::Node yamlfile = YAML::LoadFile(file);
    std::string prefix  = file.substr(0, file.find_last_of("/"));

    for (auto it : yamlfile) {
      // key value pairs of dirs
      std::string value = it.second.as<std::string>();
      if (value[0] != '/') {
        value = Layout::relative_to(prefix, value);
      }
      map[it.first.as<std::string>()] = value;
    }
  } catch (YAML::Exception &e) {
    ink_warning("Unable to read '%s': %s", file.c_str(), e.what());
    ink_notice("Continuing with default value");
    return RunrootMapType{};
  }
  return map;
}

// check for the using of runroot
// a map of all path will be returned
// if we do not use runroot, a empty map will be returned.
RunrootMapType
check_runroot()
{
  if (runroot_file.empty()) {
    return RunrootMapType{};
  }

  int len = runroot_file.size();
  if ((len + 1) > PATH_NAME_MAX) {
    ink_fatal("runroot path is too big: %d, max %d\n", len, PATH_NAME_MAX - 1);
  }
  return runroot_map(runroot_file);
}

std::string_view
get_runroot()
{
  return runroot_file;
}
