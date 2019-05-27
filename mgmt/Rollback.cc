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
  ExpandingArray existVer(25, true); // Existing versions
  struct stat fileInfo;
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

  // ToDo: This was really broken before, it  used to check if numberBackups <=0, but that could never happen.
  if (flags & CONFIG_FLAG_UNVERSIONED) {
    setLastModifiedTime();
    return;
  }

  // Check to make sure that our configuration file exists
  //
  if (statFile(&fileInfo) < 0) {
    // If we can't find an active version because there is none we have a hard failure.
    mgmt_fatal(0, "[RollBack::Rollback] Unable to find configuration file %s.\n\tStat failed : %s\n", fileName, strerror(errno));

  } else {
    fileLastModified = TS_ARCHIVE_STAT_MTIME(fileInfo);
  }
}

Rollback::~Rollback()
{
  ats_free(fileName);
}

//
//
// int Rollback::statFile()
//
//  A wrapper for stat()
//
int
Rollback::statFile(struct stat *buf)
{
  int statResult;
  std::string sysconfdir(RecConfigReadConfigDir());
  std::string filePath = Layout::get()->relative_to(sysconfdir, fileName);

  statResult = root_access_needed ? elevating_stat(filePath.c_str(), buf) : stat(filePath.c_str(), buf);

  return statResult;
}

bool
Rollback::setLastModifiedTime()
{
  struct stat fileInfo;

  // Now we need to get the modification time off of the new active file
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
//  Called to check if the file has been changed  by the user.
//  Timestamps are compared to see if a change occurred
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

  if (fileLastModified < TS_ARCHIVE_STAT_MTIME(fileInfo)) {
    setLastModifiedTime();
    configFiles->fileChanged(fileName, configName, true);
    mgmt_log("User has changed config file %s\n", fileName);

    result = true;
  } else {
    result = false;
  }

  ink_mutex_release(&fileAccessLock);
  return result;
}
