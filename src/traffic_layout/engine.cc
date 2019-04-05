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
#include "tscore/ink_defs.h"
#include "records/I_RecProcess.h"
#include "RecordsConfig.h"
#include "engine.h"
#include "file_system.h"
#include "info.h"

#include <fstream>
#include <iostream>
#include <ftw.h>
#include <yaml-cpp/yaml.h>
#include <grp.h>
#include <sysexits.h>

static const long MAX_LOGIN        = ink_login_name_max();
static constexpr int MAX_GROUP_NUM = 32;

// Personally I don't really like the way that nftw needs global variables. Right now there is no other options.
// This iteration through directory can be avoided after std::filesystem is in.

// for nftw check_directory
static std::string empty_check_directory;
// for 'verify --fix' nftw iteration
static PermissionEntry permission_entry;
// if fix_flag is set, permission handler will perform fixing operation
static bool fix_flag = false;

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

// handle the path of the engine during parsing
static std::string
path_handler(const std::string &path, bool run_flag, const std::string &command)
{
  std::string cur_working_dir = "";
  char cwd[PATH_MAX];
  if (!getcwd(cwd, sizeof(cwd))) {
    ink_warning("unexcepted failure from getcwd() - %s", strerror(errno));
  } else {
    cur_working_dir = cwd;
  }

  if (run_flag) {
    // no path passed in, use cwd
    if (path.empty()) {
      return cur_working_dir;
    }
    if (path[0] == '/') {
      return path;
    } else {
      return Layout::relative_to(cur_working_dir, path);
    }
  }

  // for other commands
  // 1. passed in path
  if (!path.empty()) {
    std::string cmdpath;
    if (path[0] == '/') {
      cmdpath = check_path(path);
    } else {
      cmdpath = check_path(Layout::relative_to(cur_working_dir, path));
    }
    if (!cmdpath.empty()) {
      return cmdpath;
    }
  }

  // 2. find cwd or parent path of cwd to check
  std::string cwdpath = check_parent_path(cur_working_dir);
  if (!cwdpath.empty()) {
    return cwdpath;
  }

  // 3. installed executable
  char RealBinPath[PATH_MAX] = {0};
  if (!command.empty() && realpath(command.c_str(), RealBinPath) != nullptr) {
    std::string bindir = RealBinPath;

    bindir = bindir.substr(0, bindir.find_last_of("/")); // getting the bin dir not executable path
    bindir = check_parent_path(bindir);
    if (!bindir.empty()) {
      return bindir;
    }
  }

  return path;
}

// check if there is any directory inside empty_check_directory to see if it is empty or not
static int
check_directory_empty(const char *path, const struct stat *s, int flag)
{
  return std::string(path) != empty_check_directory ? -1 : 0;
}

void
LayoutEngine::info()
{
  bool json = arguments.get("json");

  if (arguments.get("features")) {
    produce_features(json);
  } else if (arguments.get("versions")) {
    produce_versions(json);
  } else {
    produce_layout(json);
  }
}

void
LayoutEngine::create_runroot()
{
  // set up options
  std::string path = path_handler(arguments.get("path").value(), true, _argv[0]);
  if (path.empty()) {
    ink_error("Path not valid for creating");
    status_code = EX_SOFTWARE;
    return;
  }
  bool force_flag         = arguments.get("force");
  bool abs_flag           = arguments.get("absolute");
  std::string layout_file = arguments.get("layout").value();
  if (layout_file.find("runroot.yaml") != std::string::npos) {
    ink_error(
      "'runroot.yaml' is a potentially dangerous name for '--layout' option.\nPlease set other name to the file for '--layout'");
    status_code = EX_SOFTWARE;
    return;
  }
  // deal with the copy style
  CopyStyle copy_style = HARD;
  std::string style    = arguments.get("copy-style").value();
  if (!style.empty()) {
    transform(style.begin(), style.end(), style.begin(), ::tolower);
    if (style == "full") {
      copy_style = FULL;
    } else if (style == "soft") {
      copy_style = SOFT;
    } else if (style == "hard") {
      copy_style = HARD;
    } else {
      ink_error("Unknown copy style: '%s'", style.c_str());
      status_code = EX_USAGE;
      return;
    }
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
    ink_error("Cannot create runroot inside another runroot");
    status_code = EX_SOFTWARE;
    return;
  }

  std::cout << "creating runroot - " + ts_runroot << std::endl;

  // check if directory is empty when --force is not used
  if (is_directory(ts_runroot) && !force_flag) {
    empty_check_directory = ts_runroot;
    if (ftw(ts_runroot.c_str(), check_directory_empty, OPEN_MAX_FILE) != 0) {
      // if the directory is not empty, check for valid Y/N
      for (int i = 0; i < 3; i++) {
        std::cout << "Are you sure to create runroot inside a non-empty directory Y/N: ";
        std::string input;
        std::cin >> input;
        if (input == "Y" || input == "y") {
          break;
        }
        if (input == "N" || input == "n") {
          return;
        }
        if (i == 2) {
          ink_error("Invalid input Y/N");
          status_code = EX_SOFTWARE;
          return;
        }
      }
    }
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
  if (layout_file.size() != 0) {
    try {
      YAML::Node yamlfile = YAML::LoadFile(layout_file);
      for (const auto &it : yamlfile) {
        std::string key   = it.first.as<std::string>();
        std::string value = it.second.as<std::string>();
        auto iter         = new_map.find(key);
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
  for (const auto &it : original_map) {
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
  for (const auto &it : dir_vector) {
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

// the function for removing the runroot
void
LayoutEngine::remove_runroot()
{
  std::string path = path_handler(arguments.get("path").value(), false, _argv[0]);
  // check validity of the path
  if (path.empty()) {
    ink_error("Path not valid (runroot.yaml not found)");
    status_code = EX_IOERR;
    return;
  }

  std::string clean_root = path;
  append_slash(clean_root);

  if (arguments.get("force")) {
    // the force remove
    std::cout << "Forcing removing runroot ..." << std::endl;
    if (!remove_directory(clean_root)) {
      ink_warning("Failed force removing runroot '%s' - %s", clean_root.c_str(), strerror(errno));
    }
  } else {
    // handle the map and deleting of each directories specified in the yml file
    RunrootMapType map = runroot_map(Layout::relative_to(clean_root, "runroot.yaml"));
    map.erase(LAYOUT_PREFIX);
    map.erase(LAYOUT_EXEC_PREFIX);
    // get current working directory
    std::string cur_working_dir = "";
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
      ink_warning("unexcepted failure from getcwd() - %s", strerror(errno));
    } else {
      cur_working_dir = cwd;
    }
    for (const auto &it : map) {
      std::string dir = it.second;
      append_slash(dir);
      // get the directory to remove: prefix/etc/trafficserver -> prefix/etc
      dir = dir.substr(0, dir.substr(clean_root.size()).find_first_of("/") + clean_root.size());
      // don't remove cwd
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

// check permission for verify
static int
permission_handler(const char *path, const struct stat *s, int flag)
{
  std::string cur_directory = permission_entry.name;
  // filter traffic server related files
  if (!filter_ts_files(cur_directory, path)) {
    return 0;
  }
  if (flag == FTW_NS || !s) {
    ink_warning("unable to stat() destination path %s - %s", path, strerror(errno));
    return -1;
  }

  int ret = 0;
  // --------- for directories ---------
  if (flag == FTW_D || flag == FTW_DNR) {
    // always need read permission for directories
    if (!(s->st_mode & permission_entry.r_mode)) {
      if (fix_flag) {
        if (chmod(path, s->st_mode | permission_entry.r_mode) < 0) {
          ink_warning("Unable to change file mode on %s - %s", path, strerror(errno));
        } else {
          std::cout << "Fixed read permission: " << path << std::endl;
        }
      } else {
        std::cout << "Read permission failed for directory: " << path << std::endl;
        ret = -1;
      }
    }
    // need write permission for logdir, runtimedir and cachedir
    if (cur_directory == LAYOUT_LOGDIR || cur_directory == LAYOUT_RUNTIMEDIR || cur_directory == LAYOUT_CACHEDIR) {
      if (!(s->st_mode & permission_entry.w_mode)) {
        if (fix_flag) {
          if (chmod(path, s->st_mode | permission_entry.w_mode) < 0) {
            ink_warning("Unable to change file mode on %s - %s", path, strerror(errno));
          } else {
            std::cout << "Fixed write permission: " << path << std::endl;
          }
        } else {
          std::cout << "Write permission failed for directory: " << path << std::endl;
          ret = -1;
        }
      }
    }
    // always need execute permission for directories
    if (!(s->st_mode & permission_entry.e_mode)) {
      if (fix_flag) {
        if (chmod(path, s->st_mode | permission_entry.e_mode) < 0) {
          ink_warning("Unable to change file mode on %s - %s", path, strerror(errno));
        } else {
          std::cout << "Fixed execute permission: " << path << std::endl;
        }
      } else {
        std::cout << "Execute permission failed for directory: " << path << std::endl;
        ret = -1;
      }
    }
  } else {
    // --------- for files ---------
    // always need read permission for all files
    if (!(s->st_mode & permission_entry.r_mode)) {
      if (fix_flag) {
        if (chmod(path, s->st_mode | permission_entry.r_mode) < 0) {
          ink_warning("Unable to change file mode on %s - %s", path, strerror(errno));
        } else {
          std::cout << "Fixed read permission: " << path << std::endl;
        }
      } else {
        std::cout << "Read permission failed for file " << path << std::endl;
        ret = -1;
      }
    }
    // need write permission for files in logdir, runtimedir and cachedir
    if (cur_directory == LAYOUT_LOGDIR || cur_directory == LAYOUT_RUNTIMEDIR || cur_directory == LAYOUT_CACHEDIR) {
      if (!(s->st_mode & permission_entry.w_mode)) {
        if (fix_flag) {
          if (chmod(path, s->st_mode | permission_entry.w_mode) < 0) {
            ink_warning("Unable to change file mode on %s - %s", path, strerror(errno));
          } else {
            std::cout << "Fixed write permission: " << path << std::endl;
          }
        } else {
          std::cout << "Write permission failed for file " << path << std::endl;
          ret = -1;
        }
      }
    }
    // execute permission on files in bindir, sbindir, libdir and libexecdir
    if (cur_directory == LAYOUT_BINDIR || cur_directory == LAYOUT_SBINDIR || cur_directory == LAYOUT_LIBDIR ||
        cur_directory == LAYOUT_LIBEXECDIR) {
      std::string tmp_path = path;
      // skip the files in perl5 and pkgconfig from libdir
      if (tmp_path.find("/perl5/") != std::string::npos || tmp_path.find("/pkgconfig/") != std::string::npos) {
        return 0;
      }
      if (!(s->st_mode & permission_entry.e_mode)) {
        if (fix_flag) {
          if (chmod(path, s->st_mode | permission_entry.e_mode) < 0) {
            ink_warning("Unable to change file mode on %s - %s", path, strerror(errno));
          } else {
            std::cout << "Fixed execute permission: " << path << std::endl;
          }
        } else {
          std::cout << "Execute permission failed for file: " << path << std::endl;
          ret = -1;
        }
      }
    }
  }
  return ret;
}

// used for prefix, exec_prefix and localstatedir
// only check the read/execute permission on those directories
static bool
check_directory_permission(const char *path)
{
  struct stat stat_buffer;
  if (stat(path, &stat_buffer) < 0) {
    ink_warning("unable to stat() destination path %s - %s", path, strerror(errno));
    return false;
  }
  if (!(stat_buffer.st_mode & permission_entry.r_mode)) {
    std::cout << "Read permission failed for: " << path << std::endl;
    return false;
  }
  if (!(stat_buffer.st_mode & permission_entry.e_mode)) {
    std::cout << "Execute permission failed for: " << path << std::endl;
    return false;
  }
  return true;
}

#if defined(darwin)
// on Darwin, getgrouplist() takes int.
using gid_type = int;
#else
using gid_type = gid_t;
#endif

// helper function to check if user is from the same group of path_gid
static bool
from_group(const char *user, gid_type group_id, gid_type path_gid)
{
  int ngroups = MAX_GROUP_NUM;
  gid_type groups[ngroups];
  if (getgrouplist(user, group_id, groups, &ngroups) == -1) {
    ink_warning("Unable to get group list as user '%s'\n", user);
    return false;
  }
  for (int i = 0; i < ngroups; i++) {
    if (path_gid == groups[i]) {
      return true;
    }
  }
  return false;
}

// set permission to the map in verify runroot
static void
set_permission(PermissionMapType &permission_map, const struct passwd *pwd)
{
  gid_t ts_gid  = pwd->pw_gid;
  uid_t ts_uid  = pwd->pw_uid;
  bool new_line = false;

  // set up permission map for all permissions
  for (auto &it : permission_map) {
    std::string name  = it.first;
    std::string value = it.second.path;

    struct stat stat_buffer;
    if (stat(value.c_str(), &stat_buffer) < 0) {
      ink_warning("unable to stat() destination path %s - %s", value.c_str(), strerror(errno));
      it.second.result = false;
      new_line         = true;
      continue;
    }
    gid_t path_gid = stat_buffer.st_gid;
    uid_t path_uid = stat_buffer.st_uid;

    if (ts_uid == path_uid) {
      it.second.r_mode = S_IRUSR;
      it.second.w_mode = S_IWUSR;
      it.second.e_mode = S_IXUSR;
    } else if (from_group(pwd->pw_name, ts_gid, path_gid)) {
      it.second.r_mode = S_IRGRP;
      it.second.w_mode = S_IWGRP;
      it.second.e_mode = S_IXGRP;
    } else {
      it.second.r_mode = S_IROTH;
      it.second.w_mode = S_IWOTH;
      it.second.e_mode = S_IXOTH;
    }
    // set the result in permission entry
    permission_entry = it.second;
    it.second.result = true;
    // Special case with prefix, exec_prefix and localstatedir. We only need to check the directory itself
    // but not the files within because they are just container dir for others.
    if (name == LAYOUT_PREFIX || name == LAYOUT_EXEC_PREFIX || name == LAYOUT_LOCALSTATEDIR) {
      if (!check_directory_permission(value.c_str())) {
        it.second.result = false;
        new_line         = true;
      }
    } else {
      // go through all files to check permission
      if (ftw(value.c_str(), permission_handler, OPEN_MAX_FILE) != 0) {
        it.second.result = false;
        new_line         = true;
      }
    }
  }
  if (new_line) {
    std::cout << std::endl; // print a new line after the failed permission messages
  }
}

// the fixing permission of runroot used by verify command
static void
fix_runroot(PermissionMapType &permission_map, const struct passwd *pwd)
{
  fix_flag = true;
  for (const auto &it : permission_map) {
    std::string name  = it.first;
    std::string value = it.second.path;
    permission_entry  = permission_map[name];
    ftw(value.c_str(), permission_handler, OPEN_MAX_FILE);
  }
  fix_flag = false;
  set_permission(permission_map, pwd);
}

void
LayoutEngine::verify_runroot()
{
  // require sudo permission for --fix
  if (arguments.get("fix") && getuid() != 0) {
    ink_error("To fix permission issues, root privilege is required.\nPlease run with sudo.");
    status_code = EX_SOFTWARE;
    return;
  }

  // retrieve information
  std::string path = path_handler(arguments.get("path").value(), false, _argv[0]);
  std::string user;
  char user_buf[MAX_LOGIN + 1];
  if (arguments.get("with-user")) {
    user = arguments.get("with-user").value();
  } else {
    RecProcessInit(RECM_STAND_ALONE, nullptr /* diags */);
    LibRecordsConfigInit();
    if (RecGetRecordString("proxy.config.admin.user_id", user_buf, sizeof(user_buf)) != 0 || strlen(user_buf) == 0) {
      user = user_buf;
    } else {
      user = TS_PKGSYSUSER;
    }
  }
  // Numeric user notation for user
  if (user[0] == '#') {
    struct passwd *pwd = getpwuid(atoi(&user[1]));
    if (!pwd) {
      ink_error("No user found under id '%s'", user.c_str());
      status_code = EX_OSERR;
      return;
    }
    user = pwd->pw_name;
  }
  std::cout << "Verifying permission as user: \x1b[1m" << user << "\x1b[0m" << std::endl << std::endl;
  // try to find the user from password file
  struct passwd *pwd = getpwnam(user.c_str());
  if (!pwd) {
    ink_error("No user found under name '%s'", user.c_str());
    status_code = EX_OSERR;
    return;
  }
  // put paths from yaml file or default paths to path_map
  RunrootMapType path_map;
  if (path.empty()) {
    path_map = runroot_map_default();
  } else {
    path_map = runroot_map(Layout::relative_to(path, "runroot.yaml"));
  }
  // setup the permission map
  PermissionMapType permission_map;
  for (const auto &it : dir_vector) {
    permission_map[it].name = it;
    permission_map[it].path = path_map[it];
  }
  // root always has full access and no need to check for root
  if (user != "root") {
    set_permission(permission_map, pwd);
    // if --fix is used
    if (arguments.get("fix")) {
      fix_runroot(permission_map, pwd);
    }
  }

  // display pass or fail for permission required
  for (const auto &it : dir_vector) {
    if (permission_map[it].result) {
      std::cout << it << ": \x1b[1m" << path_map[it] << "\x1b[0m \033[1;32mPASSED\033[0m" << std::endl;
    } else {
      std::cout << it << ": \x1b[1m" << path_map[it] << "\x1b[0m \033[1;31mFAILED\033[0m" << std::endl;
      status_code = EX_SOFTWARE;
    }
  }
}
