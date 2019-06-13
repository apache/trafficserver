/** @file

  This file contains code for class to allow management of configuration files

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
#include "ConfigManager.h"
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

ConfigManager::ConfigManager(const char *fileName_, const char *configName_, bool root_access_needed_, ConfigManager *parentConfig_)
  : root_access_needed(root_access_needed_), parentConfig(parentConfig_)
{
  ExpandingArray existVer(25, true); // Existing versions
  struct stat fileInfo;
  ink_assert(fileName_ != nullptr);

  // parent must not also have a parent
  if (parentConfig) {
    ink_assert(parentConfig->parentConfig == nullptr);
  }

  // Copy the file name.
  fileName   = ats_strdup(fileName_);
  configName = ats_strdup(configName_);

  ink_mutex_init(&fileAccessLock);

  // Check to make sure that our configuration file exists
  //
  if (statFile(&fileInfo) < 0) {
    // If we can't find an active version because there is none we have a hard failure.
    mgmt_fatal(0, "[ConfigManager::ConfigManager] Unable to find configuration file %s.\n\tStat failed : %s\n", fileName,
               strerror(errno));

  } else {
    fileLastModified = TS_ARCHIVE_STAT_MTIME(fileInfo);
  }
}

ConfigManager::~ConfigManager()
{
  ats_free(fileName);
}

//
//
// int ConfigManager::statFile()
//
//  A wrapper for stat()
//
int
ConfigManager::statFile(struct stat *buf)
{
  int statResult;
  std::string sysconfdir(RecConfigReadConfigDir());
  std::string filePath = Layout::get()->relative_to(sysconfdir, fileName);

  statResult = root_access_needed ? elevating_stat(filePath.c_str(), buf) : stat(filePath.c_str(), buf);

  return statResult;
}

// bool ConfigManager::checkForUserUpdate()
//
//  Called to check if the file has been changed  by the user.
//  Timestamps are compared to see if a change occurred
bool
ConfigManager::checkForUserUpdate()
{
  struct stat fileInfo;
  bool result;

  ink_mutex_acquire(&fileAccessLock);

  if (this->statFile(&fileInfo) < 0) {
    ink_mutex_release(&fileAccessLock);
    return false;
  }

  if (fileLastModified < TS_ARCHIVE_STAT_MTIME(fileInfo)) {
    fileLastModified = TS_ARCHIVE_STAT_MTIME(fileInfo);
    configFiles->fileChanged(fileName, configName);
    mgmt_log("User has changed config file %s\n", fileName);

    result = true;
  } else {
    result = false;
  }

  ink_mutex_release(&fileAccessLock);
  return result;
}
