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

// funciton for file system management
// including: make directory (with parents), copy directory (recursively), remove directory (recursively), remove all directories
// inside

#include "ts/ink_error.h"
#include "ts/I_Layout.h"
#include "file_system.h"

#include <iostream>
#include <fstream>
#include <ftw.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

// global variables for copy function
static std::string dst_root;
static std::string src_root;
static std::string remove_path;

void
append_slash(std::string &path)
{
  if (path.back() != '/') {
    path.append("/");
  }
}

void
remove_slash(std::string &path)
{
  if (path.back() == '/') {
    path.pop_back();
  }
}

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

bool
create_directory(const std::string &dir)
{
  std::string s = dir;
  append_slash(s);

  if (exists(dir) && is_directory(dir)) {
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
  if (ret) {
    return false;
  } else {
    return true;
  }
}

static int
remove_function(const char *path, const struct stat *s, int flag, struct FTW *f)
{
  int (*rm_func)(const char *);

  switch (flag) {
  default:
    rm_func = unlink;
    break;
  case FTW_DP:
    rm_func = rmdir;
  }
  if (rm_func(path) == -1) {
    ink_notice("Failed removing directory: %s\n", strerror(errno));
    return -1;
  }
  return 0;
}

static int
remove_inside_function(const char *path, const struct stat *s, int flag, struct FTW *f)
{
  std::string path_to_remove = path;
  if (path_to_remove != remove_path) {
    switch (flag) {
    default:
      if (remove(path) != 0) {
        ink_error("unable to remove: %s", path);
        return -1;
      }
      break;
    case FTW_DP:
      if (!remove_directory(path_to_remove)) {
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

static int
copy_function(const char *src_path, const struct stat *sb, int flag)
{
  // src path no slash
  std::string full_src_path = src_path;
  if (full_src_path == src_root) {
    if (!create_directory(dst_root)) {
      ink_fatal("create directory failed during copy");
    }
    return 0;
  }
  std::string src_back = full_src_path.substr(src_root.size() + 1);
  std::string dst_path = dst_root + src_back;

  switch (flag) {
  case FTW_D:
    // create directory for FTW_D type
    if (!create_directory(dst_path)) {
      ink_fatal("create directory failed during copy");
    }
    break;
  case FTW_F:
    // if the file already exist, overwrite it
    if (exists(dst_path)) {
      if (remove(dst_path.c_str())) {
        ink_error("overwrite file falied during copy");
      }
    }
    // hardlink bin executable
    if (sb->st_mode & S_IEXEC) {
      if (link(src_path, dst_path.c_str()) != 0) {
        if (errno != EEXIST) {
          ink_warning("failed to create hard link - %s", strerror(errno));
        }
      }
    } else {
      // for normal other files
      std::ifstream src(src_path, std::ios::binary);
      std::ofstream dst(dst_path, std::ios::binary);
      dst << src.rdbuf();
      if (chmod(dst_path.c_str(), sb->st_mode) == -1) {
        ink_warning("failed chomd the destination path: %s", strerror(errno));
      }
    }
  }
  return 0;
}

// copy directory recursively using ftw to iterate
bool
copy_directory(const std::string &src, const std::string &dst)
{
  src_root = src;
  dst_root = dst;
  remove_slash(src_root);
  append_slash(dst_root);

  if (ftw(src_root.c_str(), copy_function, OPEN_MAX_FILE)) {
    return false;
  } else {
    return true;
  }
}
