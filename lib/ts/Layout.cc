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

#include <fstream>
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
Layout::create(ts::string_view const prefix)
{
  if (layout == nullptr) {
    layout = new Layout(prefix);
  }
}

static void
_relative(char *path, size_t buffsz, ts::string_view root, ts::string_view file)
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
layout_relative(ts::string_view root, ts::string_view file)
{
  char path[PATH_NAME_MAX];
  std::string ret;
  _relative(path, PATH_NAME_MAX, root, file);
  ret = path;
  return ret;
}

std::string
Layout::relative(ts::string_view file)
{
  return layout_relative(prefix, file);
}

// for updating the structure sysconfdir
void
Layout::update_sysconfdir(ts::string_view dir)
{
  sysconfdir.assign(dir.data(), dir.size());
}

std::string
Layout::relative_to(ts::string_view dir, ts::string_view file)
{
  return layout_relative(dir, file);
}

void
Layout::relative_to(char *buf, size_t bufsz, ts::string_view dir, ts::string_view file)
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

bool
Layout::check_runroot()
{
  if (getenv("USING_RUNROOT") == nullptr) {
    return false;
  }

  std::string env_path = getenv("USING_RUNROOT");
  int len              = env_path.size();
  if ((len + 1) > PATH_NAME_MAX) {
    ink_fatal("TS_RUNROOT environment variable is too big: %d, max %d\n", len, PATH_NAME_MAX - 1);
  }
  std::ifstream file;
  std::string yaml_path = layout_relative(env_path, "runroot_path.yml");

  file.open(yaml_path);
  if (!file.good()) {
    ink_warning("Bad env path, continue with default value");
    return false;
  }

  std::ifstream yamlfile(yaml_path);
  std::unordered_map<std::string, std::string> runroot_map;
  std::string str;
  while (std::getline(yamlfile, str)) {
    int pos = str.find(':');
    runroot_map[str.substr(0, pos)] = str.substr(pos + 2);
  }

  prefix        = runroot_map["prefix"];
  exec_prefix   = runroot_map["exec_prefix"];
  bindir        = runroot_map["bindir"];
  sbindir       = runroot_map["sbindir"];
  sysconfdir    = runroot_map["sysconfdir"];
  datadir       = runroot_map["datadir"];
  includedir    = runroot_map["includedir"];
  libdir        = runroot_map["libdir"];
  libexecdir    = runroot_map["libexecdir"];
  localstatedir = runroot_map["localstatedir"];
  runtimedir    = runroot_map["runtimedir"];
  logdir        = runroot_map["logdir"];
  mandir        = runroot_map["mandir"];
  infodir       = runroot_map["infodir"];
  cachedir      = runroot_map["cachedir"];

  // // for yaml lib operations
  // YAML::Node yamlfile = YAML::LoadFile(yaml_path);
  // prefix              = yamlfile["prefix"].as<string>();
  // exec_prefix         = yamlfile["exec_prefix"].as<string>();
  // bindir              = yamlfile["bindir"].as<string>();
  // sbindir             = yamlfile["sbindir"].as<string>();
  // sysconfdir          = yamlfile["sysconfdir"].as<string>();
  // datadir             = yamlfile["datadir"].as<string>();
  // includedir          = yamlfile["includedir"].as<string>();
  // libdir              = yamlfile["libdir"].as<string>();
  // libexecdir          = yamlfile["libexecdir"].as<string>();
  // localstatedir       = yamlfile["localstatedir"].as<string>();
  // runtimedir          = yamlfile["runtimedir"].as<string>();
  // logdir              = yamlfile["logdir"].as<string>();
  // mandir              = yamlfile["mandir"].as<string>();
  // infodir             = yamlfile["infodir"].as<string>();
  // cachedir            = yamlfile["cachedir"].as<string>();
  return true;
}

Layout::Layout(ts::string_view const _prefix)
{
  if (!_prefix.empty()) {
    prefix.assign(_prefix.data(), _prefix.size());
  } else {
    std::string path;
    int len;
    if (check_runroot()) {
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

Layout::~Layout()
{
}
