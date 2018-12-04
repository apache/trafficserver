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

// file for file system management for runroot including:
// create directory (with parents), copy directory (recursively),
// remove directory (recursively), remove everything inside certain directory (recursively)

#include "tscore/ink_error.h"
#include "tscore/runroot.h"
#include "tscore/ts_file.h"
#include "file_system.h"

#include <fstream>
#include <ftw.h>
#include <unordered_set>
#include <iostream>

// global variables for copy callback function from ftw
static std::string dst_root;
static std::string src_root;
static std::string copy_dir; // the current dir we are copying. e.x. sysconfdir, bindir...
static std::string remove_path;
static CopyStyle copy_style;
static int symlink_failure_counter  = 0;
static int hardlink_failure_counter = 0;
static bool msg_flag                = false;
// list of all executables of traffic server
std::unordered_set<std::string> const executables = {"traffic_crashlog", "traffic_ctl",     "traffic_layout", "traffic_logcat",
                                                     "traffic_logstats", "traffic_manager", "traffic_server", "traffic_top",
                                                     "traffic_via",      "trafficserver",   "tspush",         "tsxs"};

void
append_slash(std::string &path)
{
  if (path.back() != '/') {
    path.append("/");
  }
}

static void
remove_slash(std::string &path)
{
  while (path.back() == '/') {
    path.pop_back();
  }
}

bool
create_directory(const std::string &dir)
{
  std::string s = dir;
  append_slash(s);

  std::error_code ec;
  auto fs = ts::file::status(ts::file::path(s), ec);
  if (ts::file::is_dir(fs)) {
    return true;
  }

  int ret = 0, pos = 0, pos1 = 0;
  if ((s[0] == '.') || (s[0] == '/')) {
    pos1 = s.find("/") + 1;
  }
  pos = s.find("/", pos1);

  ret  = mkdir(s.substr(0, pos).c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  pos1 = pos + 1;
  // create directory one layer by one layer
  while (true) {
    pos = s.find("/", pos1);
    if ((size_t)pos == s.npos) {
      break;
    }
    ret  = mkdir(s.substr(0, pos).c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    pos1 = pos + 1;
  }

  return ret == 0 ? true : false;
}

static int
remove_function(const char *path, const struct stat *s, int flag, struct FTW *f)
{
  int (*rm_func)(const char *);

  if (flag == FTW_DP || flag == FTW_D || flag == FTW_DNR) {
    rm_func = rmdir;
  } else {
    rm_func = unlink;
  }
  if (rm_func(path) == -1) {
    ink_notice("Failed removing directory %s - %s\n", path, strerror(errno));
    return -1;
  }
  return 0;
}

static int
remove_inside_function(const char *path, const struct stat *s, int flag, struct FTW *f)
{
  std::string path_to_remove = path;
  if (path_to_remove != remove_path) {
    if (flag == FTW_DP || flag == FTW_D || flag == FTW_DNR) {
      if (!remove_directory(path_to_remove)) {
        ink_error("unable to remove: %s", path);
        return -1;
      }
    } else {
      if (remove(path) != 0) {
        ink_error("unable to remove: %s", path);
        return -1;
      }
    }
  }
  return 0;
}

// remove directory recursively using nftw to iterate
bool
remove_directory(const std::string &dir)
{
  std::string path = dir;
  remove_slash(path);
  if (nftw(path.c_str(), remove_function, OPEN_MAX_FILE, FTW_DEPTH)) {
    return false;
  } else {
    return true;
  }
}

// remove everything inside this directory
bool
remove_inside_directory(const std::string &dir)
{
  std::string path = dir;
  remove_slash(path);
  remove_path = path;
  if (nftw(path.c_str(), remove_inside_function, OPEN_MAX_FILE, FTW_DEPTH)) {
    return false;
  } else {
    return true;
  }
}

bool
filter_ts_directories(const std::string &dir, const std::string &dst_path)
{
  // ----- filter traffic server related directories -----
  if (dir == LAYOUT_BINDIR || dir == LAYOUT_SBINDIR) {
    // no directory from bindir and sbindir should be copied.
    return false;
  }
  if (dir == LAYOUT_LIBDIR) {
    // valid directory of libdir are perl5 and pkgconfig. If not one of those, end the copying.
    if (dst_path.find("/perl5") == std::string::npos && dst_path.find("/pkgconfig") == std::string::npos) {
      return false;
    }
  }
  if (dir == LAYOUT_INCLUDEDIR) {
    // valid directory of includedir are atscppapi and ts. If not one of those, end the copying.
    if (dst_path.find("/atscppapi") == std::string::npos && dst_path.find("/ts") == std::string::npos) {
      return false;
    }
  }
  return true;
}

bool
filter_ts_files(const std::string &dir, const std::string &dst_path)
{
  // ----- filter traffic server related files -----
  if (dir == LAYOUT_BINDIR || dir == LAYOUT_SBINDIR) {
    // check if executable is in the list of traffic server executables. If not, end the copying.
    if (executables.find(dst_path.substr(dst_path.find_last_of("/") + 1)) == executables.end()) {
      return false;
    }
  }
  if (dir == LAYOUT_LIBDIR) {
    // check if library file starts with libats, libts or contained in perl5/ and pkgconfig/.
    // If not, end the copying.
    if (dst_path.find("/perl5/") == std::string::npos && dst_path.find("/pkgconfig/") == std::string::npos &&
        dst_path.find("libats") == std::string::npos && dst_path.find("libts") == std::string::npos) {
      return false;
    }
  }
  if (dir == LAYOUT_INCLUDEDIR) {
    // check if include file is contained in atscppapi/ and ts/. If not, end the copying.
    if (dst_path.find("/atscppapi/") == std::string::npos && dst_path.find("/ts/") == std::string::npos &&
        dst_path.find("/tscpp/") == std::string::npos) {
      return false;
    }
  }
  return true;
}

static int
ts_copy_function(const char *src_path, const struct stat *sb, int flag)
{
  // src path no slash
  std::string full_src_path = src_path;
  if (full_src_path == src_root) {
    if (!create_directory(dst_root)) {
      ink_fatal("create directory '%s' failed during copy", dst_root.c_str());
    }
    return 0;
  }
  std::string src_back = full_src_path.substr(src_root.size() + 1);
  std::string dst_path = dst_root + src_back;

  switch (flag) {
  // copying a directory
  case FTW_D:
    if (!filter_ts_directories(copy_dir, dst_path)) {
      break;
    }
    // create directory for FTW_D type
    if (!create_directory(dst_path)) {
      ink_fatal("create directory failed during copy");
    }
    break;
  // copying a file
  case FTW_F:
    if (!filter_ts_files(copy_dir, dst_path)) {
      break;
    }
    // if the file already exist, overwrite it
    std::error_code ec;
    ts::file::status(ts::file::path(dst_path), ec);
    if (ec.value() == 0 || errno != ENOENT) {
      if (remove(dst_path.c_str())) {
        ink_warning("overwrite file failed during copy, unable to remove %s - %s", dst_path.c_str(), strerror(errno));
      }
    }
    // hardlink bin executable
    if (sb->st_mode & S_IEXEC) {
      if (copy_style == SOFT) {
        if (symlink(src_path, dst_path.c_str()) != 0 && errno != EEXIST) {
          // prevent too many messages generated
          if (symlink_failure_counter < 3) {
            ink_warning("failed to create symlink from %s - %s\nFall back to a full copy", src_path, strerror(errno));
            symlink_failure_counter += 1;
          } else if (!msg_flag) {
            std::cout << "All failure symlinks fall back to full copies" << std::endl;
            msg_flag = true;
          }
        } else {
          return 0;
        }
      } else if (copy_style == HARD) {
        if (link(src_path, dst_path.c_str()) != 0 && errno != EEXIST) {
          // prevent too many messages generated
          if (hardlink_failure_counter < 3) {
            ink_warning("failed to create hard link from %s - %s\nFall back to a full copy", src_path, strerror(errno));
            hardlink_failure_counter += 1;
          } else if (!msg_flag) {
            std::cout << "All failure symlinks fall back to full copies" << std::endl;
            msg_flag = true;
          }
        } else {
          return 0;
        }
      }
    }
    // for normal other files
    std::ifstream src(src_path, std::ios::binary);
    std::ofstream dst(dst_path, std::ios::binary);
    dst << src.rdbuf();
    if (chmod(dst_path.c_str(), sb->st_mode) == -1) {
      ink_warning("failed chomd the destination path: %s", strerror(errno));
    }
  }
  return 0;
}

// copy directory recursively using ftw to iterate
bool
copy_directory(const std::string &src, const std::string &dst, const std::string &dir, CopyStyle style)
{
  src_root   = src;
  dst_root   = dst;
  copy_dir   = dir;
  copy_style = style;
  remove_slash(src_root);
  append_slash(dst_root);

  if (ftw(src_root.c_str(), ts_copy_function, OPEN_MAX_FILE)) {
    return false;
  } else {
    return true;
  }
}
