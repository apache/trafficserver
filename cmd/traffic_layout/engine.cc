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

// for engine of traffic runroot
// function introduction in engine.h

#include "ts/runroot.h"
#include "ts/I_Layout.h"
#include "ts/ink_error.h"
#include "ts/ink_args.h"
#include "ts/I_Version.h"
#include "ts/ink_file.h"
#include "ts/ink_assert.h"

#include "engine.h"
#include "file_system.h"

#include <fstream>
#include <iostream>

// check if user want to force create the ts_runroot
// return true if user replies Y
static bool
check_force()
{
  // check for Y/N 3 times
  for (int i = 0; i < 3; i++) {
    std::cout << "Are you sure to overwrite and force creating runroot? (irreversible) Y/N: ";
    std::string input;
    std::cin >> input;
    if (input == "Y" || input == "y")
      return true;
    if (input == "N" || input == "n")
      return false;
  }
  ink_error("Invalid input Y/N");
  exit(70);
}

// check if we can create the runroot using path
// return true if the path is good to use
static bool
check_run_path(const std::string &arg, const int forceflag)
{
  if (arg.empty() || arg[0] == '-')
    return false;
  if (arg[0] != '-' && arg[0] != '/')
    ink_fatal("Please provide absolute path");

  std::string path = arg;
  // check force create
  if (forceflag == 1) {
    if (!check_force()) {
      ink_notice("Force create failed");
      exit(0);
    }
    ink_notice("Forcing creating runroot ...");
    if (!remove_directory(path)) {
      ink_warning("Failed removing(overwriting) existing directory - %s", strerror(errno));
    }
  }
  // if directory already exist
  if (exists(path) && is_directory(path)) {
    return true;
  } else {
    // try to create & remove
    if (!create_directory(path)) {
      return false;
    }
    remove_directory(path);
    return true;
  }
}

// return true if the path is good to delete
static bool
check_delete_path(const std::string &arg)
{
  if (arg.empty() || arg[0] == '-')
    return false;
  if (arg[0] != '-' && arg[0] != '/')
    ink_fatal("Please provide absolute path");

  std::ifstream check_file(arg);
  if (check_file) {
    return true;
  }
  return false;
}

// the help message for traffic_runroot
static void
help_message(const int versionflag, const int runflag, const int cleanflag, const int forceflag)
{
  std::cout << "if no path provided, please set Environment variable $TS_RUNROOT" << std::endl;
  std::cout << "traffic_layout runroot usage: traffic_layout [switch] [<path>]" << std::endl;
  std::cout << "                       traffic_layout --force [switch] [<path>]\n" << std::endl;
  std::cout << "==option=====switch=====description=====================================" << std::endl;
  std::cout << "Run:      --init(-i)     (Initialize the ts_runroot sandbox)" << std::endl;
  std::cout << "Remove:   --remove(-r)   (remove the ts_runroot sandbox)\n" << std::endl;
  std::cout << "==flag=======key=========description======================================" << std::endl;
  std::cout << "force:    --force   (force to create ts_runroot, only works with init)\n" << std::endl;
  std::cout << "Program information: traffic_layout [switch] -h" << std::endl;

  if (runflag)
    std::cout << "\ninit example: traffic_layout --init(-i) /path/to/sandbox" << std::endl;
  if (cleanflag)
    std::cout << "\nremove example: traffic_layout --remove(-r) /path/to/sandbox" << std::endl;
  if (forceflag)
    std::cout << "\nforce example: traffic_layout --force init /path/to/sandbox" << std::endl;
}

// the parsing function for traffic runroot program
// set the flag & path appropriately
void
RunrootEngine::runroot_parse()
{
  int i = 0;
  while (i < _argc) {
    std::string argument = _argv[i];
    // set help, verion, force flag
    if (argument == "-h" || argument == "--help") {
      help_flag = 1;
    }
    if (argument == "-V" || argument == "--version") {
      version_flag = 1;
    }
    if (argument == "--force") {
      force_flag = 1;
    }
    // set init flag & sandbox path
    if (argument == "--init" || argument == "-i") {
      run_flag = 1;
      if (i == _argc - 1)
        break;
      if (!check_run_path(_argv[i + 1], force_flag)) {
        ++i;
        continue;
      }
      run_path = _argv[i + 1];
      ++i;
    }
    // set remove flag & sandbox path
    if (argument == "--remove" || argument == "-r") {
      clean_flag = 1;
      if (i == _argc - 1)
        break;
      if (!check_delete_path(_argv[i + 1])) {
        ++i;
        continue;
      }
      clean_path = _argv[i + 1];
      ++i;
    }
    ++i;
  }
  // check output help or not
  if (help_flag == 1) {
    help_message(version_flag, run_flag, clean_flag, force_flag);
    exit(0);
  }
  if (version_flag == 1) {
    // get version info
    AppVersionInfo appVersionInfo;
    appVersionInfo.setup(PACKAGE_NAME, "traffic_runroot", PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");
    ink_fputln(stdout, appVersionInfo.FullVersionInfoStr);
    exit(0);
  }
  if (run_flag == 1 && clean_flag == 1) {
    ink_fatal("Cannot run and clean in the same time");
  }
  if (force_flag == 1 && run_flag == 0) {
    ink_fatal("Nothing to force");
  }
}

// for cleaning the parent of bin / cwd
// return the path if we can clean the bin / cwd
const static std::string
clean_parent(const std::string &bin_path)
{
  char cwd[MAX_CWD_LEN];
  ink_release_assert(getcwd(cwd, sizeof(cwd)) == nullptr);

  char resolved_binpath[MAX_CWD_LEN];
  if (realpath(bin_path.c_str(), resolved_binpath) == nullptr) { // bin path
    return "";
  }
  std::string RealBinPath = resolved_binpath;

  std::vector<std::string> TwoPath = {RealBinPath, cwd};
  for (auto it : TwoPath) {
    std::string path = check_parent_path(it);
    if (path.size() != 0) {
      return path;
    }
  }
  return "";
}

// the function for removing the runroot
bool
RunrootEngine::clean_runroot()
{
  if (clean_flag == 1) {
    std::string clean_root;
    if (!clean_path.empty()) {
      clean_root = clean_path;
    } else {
      // no clean path provided get the environment
      if (getenv("TS_RUNROOT") != nullptr) {
        clean_root = getenv("TS_RUNROOT");
      } else {
        // no path & environment, get parents of bin/cwd
        clean_root = clean_parent(_argv[0]);
        if (clean_root.empty())
          ink_fatal("Nothing to clean");
      }
    }

    // if we can find the yaml, then clean it
    std::ifstream check_file(Layout::relative_to(clean_root, "runroot_path.yml"));
    if (check_file.good()) {
      if (!remove_directory(clean_root)) {
        ink_fatal("Error cleaning directory - %s", strerror(errno));
      }
    } else {
      ink_fatal("invalid path to clean (no runroot_path.yml file found)");
    }
    return true;
  }

  // no clean
  return false;
}

// copy the stuff from original_root to ts_runroot
// fill in the global map for yaml file emitting later
void
RunrootEngine::copy_runroot(const std::string &original_root, const std::string &ts_runroot)
{
  // map the original build time directory
  std::unordered_map<std::string, std::string> original_map;

  original_map["exec_prefix"]   = TS_BUILD_EXEC_PREFIX;
  original_map["bindir"]        = TS_BUILD_BINDIR;
  original_map["sbindir"]       = TS_BUILD_SBINDIR;
  original_map["sysconfdir"]    = TS_BUILD_SYSCONFDIR;
  original_map["datadir"]       = TS_BUILD_DATADIR;
  original_map["includedir"]    = TS_BUILD_INCLUDEDIR;
  original_map["libdir"]        = TS_BUILD_LIBDIR;
  original_map["libexecdir"]    = TS_BUILD_LIBEXECDIR;
  original_map["localstatedir"] = TS_BUILD_LOCALSTATEDIR;
  original_map["runtimedir"]    = TS_BUILD_RUNTIMEDIR;
  original_map["logdir"]        = TS_BUILD_LOGDIR;
  original_map["mandir"]        = TS_BUILD_MANDIR;
  original_map["infodir"]       = TS_BUILD_INFODIR;
  original_map["cachedir"]      = TS_BUILD_CACHEDIR;

  // copy each directory to the runroot path
  // symlink the executables
  // set up path_map for yaml to emit key-value pairs
  ink_notice("Copying from the original root...");

  for (auto it : original_map) {
    std::string old_path = Layout::relative_to(original_root, it.second);
    std::string new_path = Layout::relative_to(ts_runroot, it.second);
    if (!copy_directory(old_path, new_path)) {
      ink_warning("Copy failed for %s - %s", it.first.c_str(), strerror(errno));
    }
    path_map[it.first] = new_path;
  }
  path_map["prefix"] = ts_runroot;
}
