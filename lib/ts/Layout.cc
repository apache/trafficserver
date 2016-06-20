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

static Layout *layout = NULL;

Layout *
Layout::get()
{
  if (layout == NULL) {
    ink_assert("need to call create_default_layout before accessing"
               "default_layout()");
  }
  return layout;
}

void
Layout::create(const char *prefix)
{
  if (layout == NULL) {
    layout = new Layout(prefix);
  }
}

static char *
layout_relative(const char *root, const char *file)
{
  char path[PATH_NAME_MAX];

  if (ink_filepath_merge(path, PATH_NAME_MAX, root, file, INK_FILEPATH_TRUENAME)) {
    int err = errno;
    // Log error
    if (err == EACCES) {
      ink_error("Cannot merge path '%s' above the root '%s'\n", file, root);
    } else if (err == E2BIG) {
      ink_error("Exceeding file name length limit of %d characters\n", PATH_NAME_MAX);
    } else {
      // TODO: Make some pretty errors.
      ink_error("Cannot merge '%s' with '%s' error=%d\n", file, root, err);
    }
    return NULL;
  }
  return ats_strdup(path);
}

char *
Layout::relative(const char *file)
{
  return layout_relative(prefix, file);
}

void
Layout::relative(char *buf, size_t bufsz, const char *file)
{
  char path[PATH_NAME_MAX];

  if (ink_filepath_merge(path, PATH_NAME_MAX, prefix, file, INK_FILEPATH_TRUENAME)) {
    int err = errno;
    // Log error
    if (err == EACCES) {
      ink_error("Cannot merge path '%s' above the root '%s'\n", file, prefix);
    } else if (err == E2BIG) {
      ink_error("Exceeding file name length limit of %d characters\n", PATH_NAME_MAX);
    } else {
      // TODO: Make some pretty errors.
      ink_error("Cannot merge '%s' with '%s' error=%d\n", file, prefix, err);
    }
    return;
  }
  size_t path_len = strlen(path) + 1;
  if (path_len > bufsz) {
    ink_error("Provided buffer is too small: %zu, required %zu\n", bufsz, path_len);
  } else {
    ink_strlcpy(buf, path, bufsz);
  }
}

void
Layout::update_sysconfdir(const char *dir)
{
  if (sysconfdir) {
    ats_free(sysconfdir);
  }

  sysconfdir = ats_strdup(dir);
}

char *
Layout::relative_to(const char *dir, const char *file)
{
  return layout_relative(dir, file);
}

void
Layout::relative_to(char *buf, size_t bufsz, const char *dir, const char *file)
{
  char path[PATH_NAME_MAX];

  if (ink_filepath_merge(path, PATH_NAME_MAX, dir, file, INK_FILEPATH_TRUENAME)) {
    int err = errno;
    // Log error
    if (err == EACCES) {
      ink_error("Cannot merge path '%s' above the root '%s'\n", file, dir);
    } else if (err == E2BIG) {
      ink_error("Exceeding file name length limit of %d characters\n", PATH_NAME_MAX);
    } else {
      // TODO: Make some pretty errors.
      ink_error("Cannot merge '%s' with '%s' error=%d\n", file, dir, err);
    }
    return;
  }
  size_t path_len = strlen(path) + 1;
  if (path_len > bufsz) {
    ink_error("Provided buffer is too small: %zu, required %zu\n", bufsz, path_len);
  } else {
    ink_strlcpy(buf, path, bufsz);
  }
}

Layout::Layout(const char *_prefix)
{
  if (_prefix) {
    prefix = ats_strdup(_prefix);
  } else {
    char *env_path;
    char path[PATH_NAME_MAX];
    int len;

    if ((env_path = getenv("TS_ROOT"))) {
      len = strlen(env_path);
      if ((len + 1) > PATH_NAME_MAX) {
        ink_error("TS_ROOT environment variable is too big: %d, max %d\n", len, PATH_NAME_MAX - 1);
        return;
      }
      ink_strlcpy(path, env_path, sizeof(path));
      while (len > 1 && path[len - 1] == '/') {
        path[len - 1] = '\0';
        --len;
      }
    } else {
      // Use compile time --prefix
      ink_strlcpy(path, TS_BUILD_PREFIX, sizeof(path));
    }

    prefix = ats_strdup(path);
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
  ats_free(prefix);
  ats_free(exec_prefix);
  ats_free(bindir);
  ats_free(sbindir);
  ats_free(sysconfdir);
  ats_free(datadir);
  ats_free(includedir);
  ats_free(libdir);
  ats_free(libexecdir);
  ats_free(localstatedir);
  ats_free(runtimedir);
  ats_free(logdir);
  ats_free(mandir);
  ats_free(infodir);
  ats_free(cachedir);
}
