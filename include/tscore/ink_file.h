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

/****************************************************************************

  ink_file.h

  File manipulation routines for libts

 ****************************************************************************/

#pragma once

#include "tscore/ink_config.h"

#include <cstdio>
#include <sys/types.h>
#include <dirent.h>

#if HAVE_SYS_STATFS_H
#include <sys/statfs.h>
#endif

#if HAVE_SYS_STATVFS_H
#include <sys/statvfs.h>
#endif

// Darwin keeps statfs(2) in <sys/mount.h> ...
#if HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif

#ifdef HAVE_STDINT_H
#include <stdint.h> // NOLINT(modernize-deprecated-headers)
#endif

/*===========================================================================*

                            Function Prototypes

 *===========================================================================*/

// Cause ink_filepath_merge to fail if addpath is above rootpath
//
#define INK_FILEPATH_NOTABOVEROOT 0x01
// internal: Only meaningful with INK_FILEPATH_NOTABOVEROOT
#define INK_FILEPATH_SECUREROOTTEST 0x02
// Cause ink_filepath_merge to fail if addpath is above rootpath,
// even given a rootpath /foo/bar and an addpath ../bar/bash
//
#define INK_FILEPATH_SECUREROOT 0x03
// Fail ink_filepath_merge if the merged path is relative
#define INK_FILEPATH_NOTRELATIVE 0x04
// Fail ink_filepath_merge if the merged path is absolute
#define INK_FILEPATH_NOTABSOLUTE 0x08
// Return the file system's native path format (e.g. path delimiters
// of ':' on MacOS9, '\' on Win32, etc.)
#define INK_FILEPATH_NATIVE 0x10
// Resolve the true case of existing directories and file elements
// of addpath, (resolving any aliases on Win32) and append a proper
// trailing slash if a directory
//
#define INK_FILEPATH_TRUENAME 0x20

int ink_fputln(FILE *stream, const char *s);
int ink_file_fd_readline(int fd, int bufsize, char *buf);
int ink_file_fd_writestring(int fd, const char *buf);
int ink_filepath_merge(char *buf, int bufsz, const char *rootpath, const char *addpath, int flags = INK_FILEPATH_TRUENAME);
/**
 Add addpath to the rootpath prepending slash if rootpath
 is not nullptr and doesn't end with the slash already and put the
 result into path buffer. If the buffer is too small to hold the
 resulting string, required size is returned. On success zero is returned
 */
int ink_filepath_make(char *path, int pathsz, const char *rootpath, const char *addpath);

/**
 Resize and zero-fill the given file.
 Returns 0 on success, errno on failure.
 */
int ink_file_fd_zerofill(int fd, off_t size);

/**
 Return true if the path is a directory.
 */
bool ink_file_is_directory(const char *path);

/**
 Return true if this file type can be mmap(2)'ed.
 */
bool ink_file_is_mmappable(mode_t st_mode);

struct ink_device_geometry {
  uint64_t totalsz; // Total device size in bytes.
  unsigned blocksz; // Preferred I/O block size.
  unsigned alignsz; // Block device alignment in bytes. Only relevant with stacked block devices.
};

bool ink_file_get_geometry(int fd, ink_device_geometry &geometry);

// Return the value of pathconf(path, _PC_NAME_MAX), or the closest approximation.
size_t ink_file_namemax(const char *path);

/**
 Parses the given string for file permissions
 Ownership of string parameter is NOT passed
 Returns an integer bit field on success
 Returns -1 on failure
 */
int ink_fileperm_parse(const char *perms);

// Is the given path "."?
static inline bool
isdot(const char *path)
{
  return path[0] == '.' && path[1] == '\0';
}

// Is the given path ".."?
static inline bool
isdotdot(const char *path)
{
  return path[0] == '.' && path[1] == '.' && path[2] == '\0';
}
