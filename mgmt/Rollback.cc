/** @file

  This file contains code for class to allow rollback of configuration files

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

#include "tscore/ink_platform.h"
#include "tscore/ink_memory.h"
#include "tscore/ink_time.h"
#include "Alarms.h"
#include "LocalManager.h"
#include "Rollback.h"
#include "WebMgmtUtils.h"
#include "ExpandingArray.h"
#include "MgmtSocket.h"
#include "tscore/I_Layout.h"
#include "FileManager.h"
#include "ProxyConfig.h"

#define MAX_VERSION_DIGITS 11
#define DEFAULT_BACKUPS 2

#if HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC
#define TS_ARCHIVE_STAT_MTIME(t) ((t).st_mtime * 1000000000 + (t).st_mtimespec.tv_nsec)
#elif HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC
#define TS_ARCHIVE_STAT_MTIME(t) ((t).st_mtime * 1000000000 + (t).st_mtim.tv_nsec)
#else
#define TS_ARCHIVE_STAT_MTIME(t) ((t).st_mtime * 1000000000)
#endif

// Error Strings
const char *RollbackStrings[] = {"Rollback Ok", "File was not found", "Version was out of date", "System Call Error",
                                 "Invalid Version - Version Numbers Must Increase"};

Rollback::Rollback(const char *fileName_, const char *configName_, bool root_access_needed_, Rollback *parentRollback_,
                   unsigned flags)
  : configFiles(nullptr), root_access_needed(root_access_needed_), parentRollback(parentRollback_), fileLastModified(0)
{
  ink_assert(fileName_ != nullptr);

  // parent must not also have a parent
  if (parentRollback) {
    ink_assert(parentRollback->parentRollback == nullptr);
  }

  // Copy the file name.
  fileNameLen = strlen(fileName_);
  fileName    = ats_strdup(fileName_);
  configName  = ats_strdup(configName_);

  // Extract the file base name.
  fileBaseName = strrchr(fileName, '/');
  if (fileBaseName) {
    fileBaseName++;
  } else {
    fileBaseName = fileName;
  }

  ink_mutex_init(&fileAccessLock);

  // ToDo: This was the case when versioning was off, do we still need any of this ?
  setLastModifiedTime();
}

Rollback::~Rollback()
{
  ats_free(fileName);
}

// Rollback::createPathStr()
//
//   CALLEE DELETES STORAGE
//
char *
Rollback::createPathStr()
{
  int bufSize  = 0;
  char *buffer = nullptr;
  std::string sysconfdir(RecConfigReadConfigDir());

  bufSize = sysconfdir.size() + fileNameLen + 2;
  buffer  = (char *)ats_malloc(bufSize);
  Layout::get()->relative_to(buffer, bufSize, sysconfdir, fileName);

  return buffer;
}

//
//
// int Rollback::statFile()
//
//  A wrapper for stat(). ToDo: do we still need this ?
//
int
Rollback::statFile(struct stat *buf)
{
  int statResult;
  ats_scoped_str filePath(createPathStr());

  statResult = root_access_needed ? elevating_stat(filePath, buf) : stat(filePath, buf);

  return statResult;
}

//
// int Rollback::openFile()
//
//  a wrapper for open()
//
int
Rollback::openFile(int oflags, int *errnoPtr)
{
  int fd;

  ats_scoped_str filePath(createPathStr());
  // TODO: Use the original permissions
  //       Anyhow the _1 files should not be created inside Syconfdir.
  //
  fd = mgmt_open_mode_elevate(filePath, oflags, 0644, root_access_needed);

  if (fd < 0) {
    if (errnoPtr != nullptr) {
      *errnoPtr = errno;
    }
    mgmt_log("[Rollback::openFile] Open of %s failed: %s\n", fileName, strerror(errno));
  } else {
    fcntl(fd, F_SETFD, FD_CLOEXEC);
  }

  return fd;
}

int
Rollback::closeFile(int fd, bool callSync)
{
  int result = 0;
  if (callSync && fsync(fd) < 0) {
    result = -1;
    mgmt_log("[Rollback::closeFile] fsync failed for file '%s' (%d: %s)\n", fileName, errno, strerror(errno));
  }

  if (result == 0) {
    result = close(fd);
  } else {
    close(fd);
  }
  return result;
}

bool
Rollback::setLastModifiedTime()
{
  struct stat fileInfo;

  // Now we need to get the modification time off of the file
  if (statFile(&fileInfo) >= 0) {
    fileLastModified = TS_ARCHIVE_STAT_MTIME(fileInfo);
    return true;
  } else {
    // We really shouldn't fail to stat the file since we just
    //  created it.  If we do, just punt and just use the current
    //  time.
    fileLastModified = (time(nullptr) - ink_timezone()) * 1000000000;
    return false;
  }
}

// bool Rollback::checkForUserUpdate()
//
//  Called to check if the file has been changed
//    by the user.  Timestamps are compared to see if a
//    change occurred
//
//  If the file has been changed, a new version is rolled.
//    The new current version and its predecessor will
//    be the same in this case.  While this is pointless,
//    for Rolling backward, we need to the version number
//    to up'ed so that WebFileEdit knows that the file has
//    changed.  Rolling a new version also has the effect
//    of creating a new timestamp
//
bool
Rollback::checkForUserUpdate()
{
  struct stat fileInfo;
  bool result;

  ink_mutex_acquire(&fileAccessLock);

  if (this->statFile(&fileInfo) < 0) {
    ink_mutex_release(&fileAccessLock);
    return false;
  }

  result = (fileLastModified < TS_ARCHIVE_STAT_MTIME(fileInfo));
  ink_mutex_release(&fileAccessLock);

  return result;
}
