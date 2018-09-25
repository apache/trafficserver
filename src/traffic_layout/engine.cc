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

#include "tscore/runroot.h"
#include "tscore/I_Layout.h"
#include "tscore/ink_error.h"
#include "engine.h"
#include "file_system.h"
#include "info.h"

#include <fstream>
#include <iostream>
#include <ftw.h>
#include <yaml-cpp/yaml.h>

// for nftw check_directory
std::string directory_check;

// the function for the checking of the yaml file in the passed in path
// if found return the path, if not return empty string
std::string
check_path(const std::string &path)
{
  std::string yaml_file = Layout::relative_to(path, "runroot.yaml");
  if (!exists(yaml_file)) {
    ink_warning("Unable to access runroot: '%s' - %s", yaml_file.c_str(), strerror(errno));
    return {};
  }
  return path;
}

// the function for the checking of the yaml file in passed in directory or parent directory
// if found return the parent path containing the yaml file
std::string
check_parent_path(const std::string &path)
{
  std::string yaml_path = path;
  if (yaml_path.back() == '/') {
    yaml_path.pop_back();
  }
  // go up to 4 level of parent directories
  for (int i = 0; i < 4; i++) {
    if (yaml_path.empty()) {
      return {};
    }
    if (exists(Layout::relative_to(yaml_path, "runroot.yaml"))) {
      return yaml_path;
    }
    yaml_path = yaml_path.substr(0, yaml_path.find_last_of("/"));
  }
  return {};
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
  if (is_directory(arg) && forceflag) {
    std::cout << "Forcing creating runroot ..." << std::endl;
    return true;
  }

  // if directory already exist
  if (is_directory(arg)) {
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

  // 2. find cwd or parent path of cwd to check
  std::string cwdpath = check_parent_path(cwd);
  if (!cwdpath.empty()) {
    return cwdpath;
  }

  // 3. installed executable
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

// if directory is not empty, ask input from user Y/N
static int
check_directory(const char *path, const struct stat *s, int flag, struct FTW *f)
{
  std::string checkpath = path;
  if (checkpath != directory_check) {
    // check for valid Y/N
    for (int i = 0; i < 3; i++) {
      std::cout << "Are you sure to create runroot inside a non-empty directory Y/N: ";
      std::string input;
      std::cin >> input;
      if (input == "Y" || input == "y") {
        // terminate nftw
        return 1;
      }
      if (input == "N" || input == "n") {
        exit(0);
      }
    }
    ink_error("Invalid input Y/N");
    exit(70);
  }
  return 0;
}

void
LayoutEngine::info()
{
  bool json = arguments.get("json");

  if (arguments.get("features")) {
    produce_features(json);
  } else {
    produce_layout(json);
  }
}

void
LayoutEngine::create_runroot()
{
  // set up options
  std::string path = path_handler(arguments.get("path").value(), true, _argv[0]);
  bool force_flag  = arguments.get("force");
  bool abs_flag    = arguments.get("absolute");
  // deal with the copy style
  CopyStyle copy_style;
  std::string style = arguments.get("copy-style").value();
  if (!style.empty()) {
    transform(style.begin(), style.end(), style.begin(), ::tolower);
    if (style == "full") {
      copy_style = FULL;
    } else if (style == "soft") {
      copy_style = SOFT;
    } else {
      copy_style = HARD;
    }
  } else {
    copy_style = HARD;
  }
  // check validity of the path
  if (!check_run_path(path, force_flag)) {
    ink_fatal("Failed to create runroot with path '%s'", path.c_str());
  }
  std::string original_root = Layout::get()->prefix;
  std::string ts_runroot    = path;
  // check for existing runroot to use rather than create new one
  if (!force_flag && exists(Layout::relative_to(ts_runroot, "runroot.yaml"))) {
    std::cout << "Using existing runroot...\n"
                 "Please remove the old runroot if new runroot is needed"
              << std::endl;
    return;
  }
  if (!force_flag && !check_parent_path(ts_runroot).empty()) {
    ink_fatal("Cannot create runroot inside another runroot");
  }

  std::cout << "creating runroot - " + ts_runroot << std::endl;

  // check if directory is empty when --force is not used
  if (is_directory(ts_runroot) && !force_flag) {
    directory_check = ts_runroot;
    nftw(ts_runroot.c_str(), check_directory, OPEN_MAX_FILE, FTW_DEPTH);
  }

  // create new root & copy from original to new runroot. then fill in the map
  RunrootMapType original_map; // map the original build time directory

  original_map[LAYOUT_EXEC_PREFIX]   = TS_BUILD_EXEC_PREFIX;
  original_map[LAYOUT_BINDIR]        = TS_BUILD_BINDIR;
  original_map[LAYOUT_SBINDIR]       = TS_BUILD_SBINDIR;
  original_map[LAYOUT_SYSCONFDIR]    = TS_BUILD_SYSCONFDIR;
  original_map[LAYOUT_DATADIR]       = TS_BUILD_DATADIR;
  original_map[LAYOUT_INCLUDEDIR]    = TS_BUILD_INCLUDEDIR;
  original_map[LAYOUT_LIBDIR]        = TS_BUILD_LIBDIR;
  original_map[LAYOUT_LIBEXECDIR]    = TS_BUILD_LIBEXECDIR;
  original_map[LAYOUT_LOCALSTATEDIR] = TS_BUILD_LOCALSTATEDIR;
  original_map[LAYOUT_RUNTIMEDIR]    = TS_BUILD_RUNTIMEDIR;
  original_map[LAYOUT_LOGDIR]        = TS_BUILD_LOGDIR;
  original_map[LAYOUT_MANDIR]        = TS_BUILD_MANDIR;
  original_map[LAYOUT_INFODIR]       = TS_BUILD_INFODIR;
  original_map[LAYOUT_CACHEDIR]      = TS_BUILD_CACHEDIR;

  RunrootMapType new_map = original_map;
  // use the user provided layout: layout_file
  std::string layout_file = arguments.get("layout").value();
  if (layout_file.size() != 0) {
    try {
      YAML::Node yamlfile = YAML::LoadFile(layout_file);
      for (auto &&it : yamlfile) {
        std::string key   = it.first.as<std::string>();
        std::string value = it.second.as<std::string>();
        auto &&iter       = new_map.find(key);
        if (iter != new_map.end()) {
          iter->second = value;
        } else {
          if (key != "prefix") {
            ink_warning("Unknown item from %s: '%s'", layout_file.c_str(), key.c_str());
          }
        }
      }
    } catch (YAML::Exception &e) {
      ink_warning("Unable to read provided YAML file '%s': %s", layout_file.c_str(), e.what());
      ink_notice("Continuing with default value");
    }
  }

  // copy each directory to the runroot path
  // symlink the executables
  // set up path_map for yaml to emit key-value pairs
  RunrootMapType path_map;
  for (auto &&it : original_map) {
    // path handle
    std::string join_path;
    if (it.second[0] && it.second[0] == '/') {
      join_path = it.second.substr(1);
    } else {
      join_path = it.second;
    }
    std::string new_join_path = new_map[it.first];

    std::string old_path = Layout::relative_to(original_root, join_path);
    std::string new_path = Layout::relative_to(ts_runroot, new_join_path);
    if (abs_flag) {
      path_map[it.first] = Layout::relative_to(ts_runroot, new_join_path);
    } else {
      path_map[it.first] = Layout::relative_to(".", new_join_path);
    }
    // don't copy the prefix, mandir, localstatedir and infodir
    if (it.first != LAYOUT_EXEC_PREFIX && it.first != LAYOUT_LOCALSTATEDIR && it.first != LAYOUT_MANDIR &&
        it.first != LAYOUT_INFODIR) {
      if (!copy_directory(old_path, new_path, it.first, copy_style)) {
        ink_warning("Unable to copy '%s' - %s", it.first.c_str(), strerror(errno));
        ink_notice("Creating '%s': %s", it.first.c_str(), new_path.c_str());
        // if copy failed for certain directory, we create it in runroot
        if (!create_directory(new_path)) {
          ink_warning("Unable to create '%s' - %s", it.first.c_str(), strerror(errno));
        }
      }
    }
  }

  std::cout << "Copying from " + original_root + " ..." << std::endl;

  if (abs_flag) {
    path_map[LAYOUT_PREFIX] = ts_runroot;
  } else {
    path_map[LAYOUT_PREFIX] = ".";
  }

  YAML::Emitter yamlfile;
  // emit key value pairs to the yaml file
  yamlfile << YAML::BeginMap;
  for (auto &&it : dir_vector) {
    yamlfile << YAML::Key << it;
    yamlfile << YAML::Value << path_map[it];
  }
  yamlfile << YAML::EndMap;

  // create the file
  std::ofstream f(Layout::relative_to(ts_runroot, "runroot.yaml"));
  if (f.bad()) {
    ink_warning("Writing to YAML file failed\n");
  } else {
    f << yamlfile.c_str();
  }
}
static bool
check_remove_force_second()
{
  for (int i = 0; i < 3; i++) {
    std::cout << "This is irreversible. Are you really sure to forcibly remove the runroot Y/N: ";
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

// check if user want to force remove the ts_runroot
// return true if user replies Y
static bool
check_remove_force()
{
  // check for valid Y/N
  for (int i = 0; i < 3; i++) {
    std::cout << "Are you sure to force removing runroot? (irreversible) Y/N: ";
    std::string input;
    std::cin >> input;
    if (input == "Y" || input == "y")
      return check_remove_force_second();
    if (input == "N" || input == "n")
      return false;
  }
  ink_error("Invalid input Y/N");
  exit(70);
}

// the function for removing the runroot
void
LayoutEngine::remove_runroot()
{
  std::string path = path_handler(arguments.get("path").value(), false, _argv[0]);
  // check validity of the path
  if (path.empty()) {
    ink_fatal("Path not valid (runroot.yaml not found)");
  }

  std::string clean_root = path;
  append_slash(clean_root);

  char cwd[PATH_MAX];
  if (getcwd(cwd, sizeof(cwd)) == nullptr) {
    ink_fatal("unexcepted failure from getcwd() code=%d", errno);
  }
  std::string cur_working_dir = cwd;

  if (arguments.get("force")) {
    // the force clean
    if (!check_remove_force()) {
      exit(0);
    }
    std::cout << "Forcing removing runroot ..." << std::endl;
    if (!remove_directory(clean_root)) {
      ink_warning("Failed force removing runroot '%s' - %s", clean_root.c_str(), strerror(errno));
    }
  } else {
    // handle the map and deleting of each directories specified in the yml file
    RunrootMapType map = runroot_map(Layout::relative_to(clean_root, "runroot.yaml"));
    map.erase(LAYOUT_PREFIX);
    map.erase(LAYOUT_EXEC_PREFIX);
    for (auto &&it : map) {
      std::string dir = it.second;
      append_slash(dir);
      // get the directory to remove: prefix/etc/trafficserver -> prefix/etc
      dir = dir.substr(0, dir.substr(clean_root.size()).find_first_of("/") + clean_root.size());
      if (cur_working_dir != dir) {
        remove_directory(dir);
      } else {
        // if we are at this directory, remove files inside
        remove_inside_directory(dir);
      }
    }
    // remove yaml file
    std::string yaml_file = Layout::relative_to(clean_root, "runroot.yaml");
    if (remove(yaml_file.c_str()) != 0) {
      ink_notice("unable to delete runroot.yaml - %s", strerror(errno));
    }

    append_slash(cur_working_dir);
    if (cur_working_dir.find(clean_root) != 0) {
      // if cwd is not runroot and runroot is empty, remove it
      if (remove(clean_root.c_str()) != 0) {
        ink_notice("unable to delete %s - %s", clean_root.c_str(), strerror(errno));
      }
    }
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

// output read permission
static void
output_read_permission(const std::string &permission)
{
  if (permission[0] == '1') {
    std::cout << "\tRead Permission: \033[1;32mPASSED\033[0m" << std::endl;
  } else {
    std::cout << "\tRead Permission: \033[1;31mFAILED\033[0m" << std::endl;
  }
}

// output write permission
static void
output_write_permission(const std::string &permission)
{
  if (permission[1] == '1') {
    std::cout << "\tWrite Permission: \033[1;32mPASSED\033[0m" << std::endl;
  } else {
    std::cout << "\tWrite Permission: \033[1;31mFAILED\033[0m" << std::endl;
  }
}

// output execute permission
static void
output_execute_permission(const std::string &permission)
{
  if (permission[2] == '1') {
    std::cout << "\tExecute Permission: \033[1;32mPASSED\033[0m" << std::endl;
  } else {
    std::cout << "\tExecute Permission: \033[1;31mFAILED\033[0m" << std::endl;
  }
}

// the fixing permission of runroot used by verify command
static void
fix_runroot(RunrootMapType &path_map, RunrootMapType &permission_map, RunrootMapType &usergroup_map)
{
  if (getuid() != 0) {
    ink_error("To fix permission issues, root privileges are required.\nPlease run with sudo.");
    exit(70);
  }
  for (auto &&it : permission_map) {
    std::string name       = it.first;
    std::string permission = it.second;
    std::string usergroup  = usergroup_map[name];
    std::string path       = path_map[name];

    int read_permission;
    int write_permission;
    int execute_permission;

    struct stat stat_buffer;
    if (stat(path.c_str(), &stat_buffer) < 0) {
      ink_warning("unable to stat() destination path %s: %s", path.c_str(), strerror(errno));
      continue;
    }

    if (usergroup == "owner") {
      read_permission    = S_IRUSR;
      write_permission   = S_IWUSR;
      execute_permission = S_IXUSR;
    } else if (usergroup == "group") {
      read_permission    = S_IRGRP;
      write_permission   = S_IWGRP;
      execute_permission = S_IXGRP;
    } else {
      read_permission    = S_IROTH;
      write_permission   = S_IWOTH;
      execute_permission = S_IXOTH;
    }
    // read
    if (permission[0] != '1') {
      if (chmod(path.c_str(), stat_buffer.st_mode | read_permission) < 0) {
        ink_warning("Unable to change file mode on %s: %s", path.c_str(), strerror(errno));
      }
      std::cout << "Read permission fixed for '" + name + "': " + path << std::endl;
    }
    if (name == LAYOUT_LOGDIR || name == LAYOUT_RUNTIMEDIR || name == LAYOUT_CACHEDIR) {
      // write
      if (permission[1] != '1') {
        if (chmod(path.c_str(), stat_buffer.st_mode | read_permission | write_permission) < 0) {
          ink_warning("Unable to change file mode on %s: %s", path.c_str(), strerror(errno));
        }
        std::cout << "Write permission fixed for '" + name + "': " + path << std::endl;
      }
    }
    if (name == LAYOUT_BINDIR || name == LAYOUT_SBINDIR || name == LAYOUT_LIBDIR || name == LAYOUT_LIBEXECDIR) {
      // execute
      if (permission[2] != '1') {
        if (chmod(path.c_str(), stat_buffer.st_mode | read_permission | execute_permission) < 0) {
          ink_warning("Unable to change file mode on %s: %s", path.c_str(), strerror(errno));
        }
        std::cout << "Execute permission fixed for '" + name + "': " + path << std::endl;
      }
    }
  }
}

// set permission to the map in verify runroot
static void
set_permission(std::vector<std::string> const &dir_vector, RunrootMapType &path_map, RunrootMapType &permission_map,
               RunrootMapType &usergroup_map)
{
  // active group and user of the path
  std::pair<int, int> giduid = get_giduid(TS_PKGSYSUSER);

  int ts_gid = giduid.first;
  int ts_uid = giduid.second;

  struct stat stat_buffer;

  // set up permission map for all permissions
  for (auto &&it : dir_vector) {
    std::string name  = it;
    std::string value = path_map[name];

    if (name == LAYOUT_PREFIX || name == LAYOUT_EXEC_PREFIX) {
      continue;
    }

    if (stat(value.c_str(), &stat_buffer) < 0) {
      ink_warning("unable to stat() destination path %s: %s", value.c_str(), strerror(errno));
      continue;
    }
    int path_gid = int(stat_buffer.st_gid);
    int path_uid = int(stat_buffer.st_uid);

    permission_map[name] = "000"; // default rwx all 0
    if (ts_uid == path_uid) {
      // check for owner permission of st_mode
      usergroup_map[name] = "owner";
      if (stat_buffer.st_mode & S_IRUSR) {
        permission_map[name][0] = '1';
      }
      if (stat_buffer.st_mode & S_IWUSR) {
        permission_map[name][1] = '1';
      }
      if (stat_buffer.st_mode & S_IXUSR) {
        permission_map[name][2] = '1';
      }
    } else if (ts_gid == path_gid) {
      // check for group permission of st_mode
      usergroup_map[name] = "group";
      if (stat_buffer.st_mode & S_IRGRP) {
        permission_map[name][0] = '1';
      }
      if (stat_buffer.st_mode & S_IWGRP) {
        permission_map[name][1] = '1';
      }
      if (stat_buffer.st_mode & S_IXGRP) {
        permission_map[name][2] = '1';
      }
    } else {
      // check for others permission of st_mode
      usergroup_map[name] = "others";
      if (stat_buffer.st_mode & S_IROTH) {
        permission_map[name][0] = '1';
      }
      if (stat_buffer.st_mode & S_IWOTH) {
        permission_map[name][1] = '1';
      }
      if (stat_buffer.st_mode & S_IXOTH) {
        permission_map[name][2] = '1';
      }
    }
  }
}

void
LayoutEngine::verify_runroot()
{
  std::string path = path_handler(arguments.get("path").value(), false, _argv[0]);

  std::cout << "trafficserver user: " << TS_PKGSYSUSER << std::endl << std::endl;

  // put paths from yaml file or default paths to path_map
  RunrootMapType path_map;
  if (path.empty()) {
    path_map = runroot_map_default();
    std::cout << "Verifying default build ..." << std::endl;
  } else {
    path_map = runroot_map(Layout::relative_to(path, "runroot.yaml"));
  }

  RunrootMapType permission_map; // contains rwx bits
  RunrootMapType usergroup_map;  // map: owner, group, others

  set_permission(dir_vector, path_map, permission_map, usergroup_map);

  // if --fix is used
  if (arguments.get("fix")) {
    fix_runroot(path_map, permission_map, usergroup_map);
    set_permission(dir_vector, path_map, permission_map, usergroup_map);
  }

  // display pass or fail for permission required
  for (uint i = 2; i < dir_vector.size(); i++) {
    std::string name       = dir_vector[i];
    std::string permission = permission_map[dir_vector[i]];
    std::cout << name << ": \x1b[1m" + path_map[name] + "\x1b[0m" << std::endl;

    // output read permission
    if (name == LAYOUT_LOCALSTATEDIR || name == LAYOUT_INCLUDEDIR || name == LAYOUT_SYSCONFDIR || name == LAYOUT_DATADIR) {
      output_read_permission(permission);
    }
    // output write permission
    if (name == LAYOUT_LOGDIR || name == LAYOUT_RUNTIMEDIR || name == LAYOUT_CACHEDIR) {
      output_read_permission(permission);
      output_write_permission(permission);
    }
    // output execute permission
    if (name == LAYOUT_BINDIR || name == LAYOUT_SBINDIR || name == LAYOUT_LIBDIR || name == LAYOUT_LIBEXECDIR) {
      output_read_permission(permission);
      output_execute_permission(permission);
    }
  }
}
