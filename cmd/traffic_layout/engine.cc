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
#include "records/I_RecCore.h"
#include "ts/ink_config.h"

#include "engine.h"
#include "file_system.h"
#include "ts/runroot.h"

#include <fstream>
#include <iostream>
#include <ftw.h>
#include <pwd.h>

std::string directory_check = "";

// check if user want to force create the ts_runroot
// return true if user replies Y
static bool
check_force()
{
  // check for Y/N 3 times
  for (int i = 0; i < 3; i++) {
    std::cout << "Are you sure to overwrite and force creating/removing runroot? (irreversible) Y/N: ";
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
check_run_path(const std::string &arg, const bool forceflag)
{
  if (arg.empty()) {
    return false;
  }
  // the condition of force create
  if (exists(arg) && is_directory(arg) && forceflag) {
    if (!check_force()) {
      std::cout << "Force create terminated" << std::endl;
      exit(0);
    }
    std::cout << "Forcing creating runroot ..." << std::endl;
    // directory_remove = arg;
    // nftw(arg.c_str(), remove_inside_directory, OPEN_MAX_FILE, FTW_DEPTH);
    return true;
  }

  // if directory already exist
  if (exists(arg) && is_directory(arg)) {
    return true;
  } else {
    // try to create & remove
    if (!create_directory(arg)) {
      return false;
    }
    remove_directory(arg);
    return true;
  }
}

// handle the path of the engine during parsing
static std::string
path_handler(const std::string &path, bool run_flag, const std::string &command)
{
  char cwd[PATH_MAX];
  if (getcwd(cwd, sizeof(cwd)) == nullptr) {
    ink_fatal("unexcepted failure from getcwd() code=%d", errno);
  }

  if (run_flag) {
    // no path passed in, use cwd
    if (path.empty()) {
      return cwd;
    }
    if (path[0] == '/') {
      return path;
    } else {
      return Layout::relative_to(cwd, path);
    }
  }

  // for other commands
  // 1. passed in path
  if (!path.empty()) {
    std::string cmdpath;
    if (path[0] == '/') {
      cmdpath = check_path(path);
    } else {
      cmdpath = check_path(Layout::relative_to(cwd, path));
    }
    if (!cmdpath.empty()) {
      return cmdpath;
    }
  }

  // 2. check Environment variable
  char *env_val = getenv("TS_RUNROOT");
  if (env_val != nullptr) {
    std::string envpath = check_path(env_val);
    if (!envpath.empty()) {
      return envpath;
    }
  }

  // 3. find cwd or parent path of cwd to check
  std::string cwdpath = check_parent_path(cwd);
  if (!cwdpath.empty()) {
    return cwdpath;
  }

  // 4. installed executable
  char RealBinPath[PATH_MAX] = {0};
  if (!command.empty() && realpath(command.c_str(), RealBinPath) != nullptr) {
    std::string bindir = RealBinPath;
    bindir             = bindir.substr(0, bindir.find_last_of("/")); // getting the bin dir not executable path
    bindir             = check_parent_path(bindir);
    if (!bindir.empty()) {
      return bindir;
    }
  }

  return path;
}

// the help message for traffic_layout runroot
void
RunrootEngine::runroot_help_message(const bool runflag, const bool cleanflag, const bool verifyflag, const bool fixflag)
{
  if (runflag) {
    std::cout << "\ninit Usage: traffic_layout init ([switch]) (--path /path/to/sandbox)\n" << std::endl;
    std::cout << "Sub-switches:\n"
                 "--path        Specify the path of the runroot to create (the path should be the next argument)\n"
                 "--force       Force to create ts_runroot even directory already exists\n"
                 "--absolute    Produce absolute path in the yaml file\n"
                 "--run-root(=/path)  Using specified TS_RUNROOT as sandbox\n"
              << std::endl;
  }
  if (cleanflag) {
    std::cout << "\nremove Usage: traffic_layout remove ([switch]) (--path /path/to/sandbox)\n" << std::endl;
    std::cout << "Sub-switches:\n"
                 "--path       specify the path of the runroot to remove (the path should be the next argument)\n"
                 "--force      force to remove ts_runroot even with other unknown files\n"
              << std::endl;
  }
  if (verifyflag) {
    std::cout << "\nverify Usage: traffic_layout verify (--path /path/to/sandbox)\n" << std::endl;
    std::cout << "Sub-switches:\n"
                 "--path       specify the path of the runroot to verify (the path should be the next argument)\n"
              << std::endl;
  }
  return;
}

// the parsing function for traffic runroot program
// set the flag & path appropriately
bool
RunrootEngine::runroot_parse()
{
  for (unsigned int i = 1; i < _argv.size(); ++i) {
    std::string argument = _argv[i];
    // set the help, version, force and absolute flags
    if (argument == "-h" || argument == "--help") {
      help_flag = true;
      continue;
    }
    if (argument == "-V" || argument == "--version") {
      version_flag = true;
      continue;
    }
    if (argument == "--force") {
      force_flag = true;
      continue;
    }
    if (argument == "--absolute") {
      abs_flag = true;
      continue;
    }
    if (argument.substr(0, RUNROOT_WORD_LENGTH) == "--run-root") {
      continue;
    }
    // set init flag
    if (argument == "init") {
      run_flag = true;
      command_num++;
      continue;
    }
    // set remove flag
    if (argument == "remove") {
      clean_flag = true;
      command_num++;
      continue;
    }
    // set verify flag
    if (argument == "verify") {
      verify_flag = true;
      command_num++;
      continue;
    }
    // set fix flag
    if (argument == "--fix") {
      fix_flag = true;
      command_num++;
      continue;
    }
    if (argument == "--path") {
      if (i + 1 >= _argv.size() || _argv[i + 1][0] == '-') {
        // invalid path
        return false;
      }
      path = _argv[i + 1];
      ++i;
      continue;
    }
    return false;
  }
  return true;
}

// check the logic and see if everthing is fine
void
RunrootEngine::sanity_check()
{
  // check output help or not
  if (help_flag) {
    runroot_help_message(run_flag, clean_flag, verify_flag, fix_flag);
    exit(0);
  }
  if (version_flag) {
    // get version info
    AppVersionInfo appVersionInfo;
    appVersionInfo.setup(PACKAGE_NAME, "traffic_layout", PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");
    ink_fputln(stdout, appVersionInfo.FullVersionInfoStr);
    exit(0);
  }
  if (command_num > 1) {
    ink_fatal("Cannot run multiple command at the same time");
  }
  if (command_num < 1) {
    ink_fatal("No command specified");
  }
  if ((force_flag && !run_flag) && (force_flag && !clean_flag)) {
    ink_fatal("Nothing to force");
  }

  path = path_handler(path, run_flag, _argv[0]);

  if (path.empty()) {
    ink_fatal("Path not valild (runroot_path.yml not found)");
  }

  if (run_flag) {
    if (!check_run_path(path, force_flag)) {
      ink_fatal("Failed to create runroot with path '%s'", path.c_str());
    }
  }
}

// the function for removing the runroot
void
RunrootEngine::clean_runroot()
{
  std::string clean_root = path;

  char cwd[PATH_MAX];
  if (getcwd(cwd, sizeof(cwd)) == nullptr) {
    ink_fatal("unexcepted failure from getcwd() code=%d", errno);
  }
  std::string cur_working_dir = cwd;

  if (force_flag) {
    // the force clean
    if (!check_force()) {
      std::cout << "Force remove terminated" << std::endl;
      exit(0);
    }
    std::cout << "Forcing removing runroot ..." << std::endl;
    if (cur_working_dir == clean_root) {
      // if cwd is the runroot, keep the directory and remove everything insides
      remove_inside_directory(clean_root);
    } else {
      if (!remove_directory(clean_root)) {
        ink_warning("Failed force removing runroot '%s' - %s", clean_root.c_str(), strerror(errno));
      }
    }
  } else {
    // handle the map and deleting of each directories specified in the yml file
    std::unordered_map<std::string, std::string> map = runroot_map(clean_root);
    map.erase("prefix");
    map.erase("exec_prefix");
    append_slash(clean_root);
    for (auto it : map) {
      std::string dir = it.second;
      append_slash(dir);
      std::string later_dir = dir.substr(clean_root.size());
      dir                   = Layout::relative_to(clean_root, later_dir.substr(0, later_dir.find_first_of("/")));
      remove_directory(dir);
    }

    std::string yaml_file = Layout::relative_to(clean_root, "runroot_path.yml");
    // remove yml file
    if (yaml_file.size()) {
      remove(yaml_file.c_str());
    }
    if (cur_working_dir != clean_root) {
      // if the runroot is empty, remove it
      remove(clean_root.c_str());
    }
  }
}

// if directory is not empty, throw error
static int
check_directory(const char *path, const struct stat *s, int flag, struct FTW *f)
{
  std::string checkpath = path;
  if (checkpath != directory_check) {
    ink_fatal("directory not empty, use --force to overwrite");
  }
  return 0;
}

// the function for creating the runroot
void
RunrootEngine::create_runroot()
{
  // start the runroot creating stuff
  std::string original_root;
  char *env_val = getenv("TS_RUNROOT");
  if ((env_val != nullptr) && is_directory(env_val)) {
    // from env variable
    original_root = env_val;
  } else {
    // from default layout structure
    original_root = Layout::get()->prefix;
  }
  std::string ts_runroot = path;

  // handle the ts_runroot
  // ts runroot must be an accessible path
  std::ifstream check_file(Layout::relative_to(ts_runroot, "runroot_path.yml"));
  if (check_file.good()) {
    // if the path already ts_runroot, use it rather than create new one
    std::cout << "Using existing TS_RUNROOT..." << std::endl;
    std::cout << "Please remove the old TS_RUNROOT if new runroot is needed \n(usage: traffic_layout remove --path /path/...)"
              << std::endl;
    return;
  } else if (exists(ts_runroot) && is_directory(ts_runroot) && !force_flag) {
    // check if directory is empty
    directory_check = ts_runroot;
    nftw(ts_runroot.c_str(), check_directory, OPEN_MAX_FILE, FTW_DEPTH);
  }

  // create new root & copy from original to new runroot. then fill in the map
  copy_runroot(original_root, ts_runroot);

  // create and emit to yaml file the key value pairs of path
  std::ofstream yamlfile;
  std::string yaml_path = Layout::relative_to(ts_runroot, "runroot_path.yml");
  yamlfile.open(yaml_path);

  for (auto it : path_map) {
    // out put key value pairs of path
    yamlfile << it.first << ": " << it.second << std::endl;
  }
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
  original_map["cachedir"]      = TS_BUILD_CACHEDIR;

  // copy each directory to the runroot path
  // symlink the executables
  // set up path_map for yaml to emit key-value pairs
  for (auto it : original_map) {
    // path handle
    std::string join_path;
    if (it.second[0] && it.second[0] == '/') {
      join_path = it.second.substr(1);
    } else {
      join_path = it.second;
    }

    std::string old_path = Layout::relative_to(original_root, join_path);
    std::string new_path = Layout::relative_to(ts_runroot, join_path);
    if (abs_flag) {
      path_map[it.first] = Layout::relative_to(ts_runroot, join_path);
    } else {
      path_map[it.first] = Layout::relative_to(".", join_path);
    }

    if (!copy_directory(old_path, new_path)) {
      ink_warning("Copy failed for '%s' - %s", it.first.c_str(), strerror(errno));
    }
  }

  std::cout << "Copying from " + original_root + " ..." << std::endl;

  if (abs_flag) {
    path_map["prefix"] = ts_runroot;
  } else {
    path_map["prefix"] = ".";
  }
}

// return an array containing gid and uid of provided user
static std::pair<int, int>
get_giduid(const char *user)
{
  passwd *pwd;
  if (*user == '#') {
    // Numeric user notation.
    uid_t uid = (uid_t)atoi(&user[1]);
    pwd       = getpwuid(uid);
  } else {
    pwd = getpwnam(user);
  }
  if (!pwd) {
    // null ptr
    ink_fatal("missing password database entry for '%s'", user);
  }
  std::pair<int, int> giduid = {int(pwd->pw_gid), int(pwd->pw_uid)};
  return giduid;
}

static void
output_read_permission(const std::string &permission)
{
  if (permission[0] == '1') {
    std::cout << "\tRead Permission: \033[1;32mPASSED\033[0m" << std::endl;
  } else {
    std::cout << "\tRead Permission: \033[1;31mFAILED\033[0m" << std::endl;
  }
}

static void
output_write_permission(const std::string &permission)
{
  if (permission[1] == '1') {
    std::cout << "\tWrite Permission: \033[1;32mPASSED\033[0m" << std::endl;
  } else {
    std::cout << "\tWrite Permission: \033[1;31mFAILED\033[0m" << std::endl;
  }
}

static void
output_execute_permission(const std::string &permission)
{
  if (permission[2] == '1') {
    std::cout << "\tExecute Permission: \033[1;32mPASSED\033[0m" << std::endl;
  } else {
    std::cout << "\tExecute Permission: \033[1;31mFAILED\033[0m" << std::endl;
  }
}

void
RunrootEngine::verify_runroot()
{
  std::pair<int, int> giduid = get_giduid(TS_PKGSYSUSER);

  int gid = giduid.first;
  int uid = giduid.second;

  std::cout << "trafficserver user: " << TS_PKGSYSUSER << std::endl << std::endl;

  if (int(getuid()) != uid) {
    if (getuid() != 0) {
      ink_error("In order to test as user '%s', root privileges are required.\nPlease run with sudo.", TS_PKGSYSUSER);
      exit(70);
    }
    if (setregid(gid, gid) != 0) {
      ink_fatal("failed to set group ID '%d' - %s", gid, strerror(errno));
    }
    if (setreuid(uid, uid) != 0) {
      ink_fatal("failed to set user ID '%d' - %s", uid, strerror(errno));
    }
  }

  std::unordered_map<std::string, std::string> path_map = runroot_map(path);
  std::unordered_map<std::string, std::string> permission_map;

  // set up permission map for all permissions
  for (auto it : path_map) {
    std::string name  = it.first;
    std::string value = it.second;

    if (name == "prefix" || name == "exec_prefix")
      continue;

    permission_map[name] = "000"; // default rwx all 0

    if (!access(value.c_str(), R_OK)) {
      permission_map[name][0] = '1';
    }
    if (!access(value.c_str(), W_OK)) {
      permission_map[name][1] = '1';
    }
    if (!access(value.c_str(), X_OK)) {
      permission_map[name][2] = '1';
    }
  }

  // display pass or fail for permission required
  for (auto it : permission_map) {
    std::string name       = it.first;
    std::string permission = it.second;
    std::cout << name << ": \x1b[1m" + path_map[name] + "\x1b[0m" << std::endl;

    // check for read permission
    if (name == "includedir" || name == "mandir" || name == "sysconfdir" || name == "datadir") {
      output_read_permission(permission);
    }
    // check for write permission
    if (name == "localstatedir" || name == "logdir" || name == "runtimedir" || name == "cachedir") {
      output_read_permission(permission);
      output_write_permission(permission);
    }
    // check for execute permission
    if (name == "bindir" || name == "sbindir" || name == "libdir" || name == "libexecdir") {
      output_read_permission(permission);
      output_execute_permission(permission);
    }
  }
}
