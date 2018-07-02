/** @file

  Implementation of the Layout class.

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

#include "ts/ink_platform.h"
#include "ts/ink_assert.h"
#include "ts/ink_file.h"
#include "ts/ink_memory.h"
#include "ts/ink_string.h"
#include "ts/I_Layout.h"
#include "ts/runroot.h"

#include <unordered_map>

static Layout *layout = nullptr;

Layout *
Layout::get()
{
  if (layout == nullptr) {
    ink_assert("need to call create_default_layout before accessing"
               "default_layout()");
  }
  return layout;
}

void
Layout::create(std::string_view const prefix)
{
  if (layout == nullptr) {
    layout = new Layout(prefix);
  }
}

static void
_relative(char *path, size_t buffsz, std::string_view root, std::string_view file)
{
  if (ink_filepath_merge(path, buffsz, root.data(), file.data(), INK_FILEPATH_TRUENAME)) {
    int err = errno;
    // Log error
    if (err == EACCES) {
      ink_fatal("Cannot merge path '%s' above the root '%s'\n", file.data(), root.data());
    } else if (err == E2BIG) {
      ink_fatal("Exceeding file name length limit of %d characters\n", PATH_NAME_MAX);
    } else {
      // TODO: Make some pretty errors.
      ink_fatal("Cannot merge '%s' with '%s' error=%d\n", file.data(), root.data(), err);
    }
  }
}

static std::string
layout_relative(std::string_view root, std::string_view file)
{
  char path[PATH_NAME_MAX];
  std::string ret;
  _relative(path, PATH_NAME_MAX, root, file);
  ret = path;
  return ret;
}

std::string
Layout::relative(std::string_view file)
{
  return layout_relative(prefix, file);
}

// for updating the structure sysconfdir
void
Layout::update_sysconfdir(std::string_view dir)
{
  sysconfdir.assign(dir.data(), dir.size());
}

std::string
Layout::relative_to(std::string_view dir, std::string_view file)
{
  return layout_relative(dir, file);
}

void
Layout::relative_to(char *buf, size_t bufsz, std::string_view dir, std::string_view file)
{
  char path[PATH_NAME_MAX];

  _relative(path, PATH_NAME_MAX, dir, file);
  size_t path_len = strlen(path) + 1;
  if (path_len > bufsz) {
    ink_fatal("Provided buffer is too small: %zu, required %zu\n", bufsz, path_len);
  } else {
    ink_strlcpy(buf, path, bufsz);
  }
}

Layout::Layout(std::string_view const _prefix)
{
  if (!_prefix.empty()) {
    prefix.assign(_prefix.data(), _prefix.size());
  } else {
    std::string path;
    int len;
    RunrootMapType dir_map = check_runroot();
    if (dir_map.size() != 0) {
      prefix        = dir_map["prefix"];
      exec_prefix   = dir_map["exec_prefix"];
      bindir        = dir_map["bindir"];
      sbindir       = dir_map["sbindir"];
      sysconfdir    = dir_map["sysconfdir"];
      datadir       = dir_map["datadir"];
      includedir    = dir_map["includedir"];
      libdir        = dir_map["libdir"];
      libexecdir    = dir_map["libexecdir"];
      localstatedir = dir_map["localstatedir"];
      runtimedir    = dir_map["runtimedir"];
      logdir        = dir_map["logdir"];
      mandir        = dir_map["mandir"];
      infodir       = dir_map["infodir"];
      cachedir      = dir_map["cachedir"];
      return;
    }
    if (getenv("TS_ROOT") != nullptr) {
      std::string env_path(getenv("TS_ROOT"));
      len = env_path.size();
      if ((len + 1) > PATH_NAME_MAX) {
        ink_fatal("TS_ROOT environment variable is too big: %d, max %d\n", len, PATH_NAME_MAX - 1);
      }
      path = env_path;
      while (path.back() == '/') {
        path.pop_back();
      }
    } else {
      // Use compile time --prefix
      path = TS_BUILD_PREFIX;
    }
    prefix = path;
  }
  exec_prefix   = layout_relative(prefix, TS_BUILD_EXEC_PREFIX);
  bindir        = layout_relative(prefix, TS_BUILD_BINDIR);
  sbindir       = layout_relative(prefix, TS_BUILD_SBINDIR);
  sysconfdir    = layout_relative(prefix, TS_BUILD_SYSCONFDIR);
  datadir       = layout_relative(prefix, TS_BUILD_DATADIR);
  includedir    = layout_relative(prefix, TS_BUILD_INCLUDEDIR);
  libdir        = layout_relative(prefix, TS_BUILD_LIBDIR);
  libexecdir    = layout_relative(prefix, TS_BUILD_LIBEXECDIR);
  localstatedir = layout_relative(prefix, TS_BUILD_LOCALSTATEDIR);
  runtimedir    = layout_relative(prefix, TS_BUILD_RUNTIMEDIR);
  logdir        = layout_relative(prefix, TS_BUILD_LOGDIR);
  mandir        = layout_relative(prefix, TS_BUILD_MANDIR);
  infodir       = layout_relative(prefix, TS_BUILD_INFODIR);
  cachedir      = layout_relative(prefix, TS_BUILD_CACHEDIR);
}

Layout::~Layout() {}
