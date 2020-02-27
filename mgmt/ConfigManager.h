/** @file

  Interface for class to allow management of configuration files

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

#pragma once

#include "tscore/ink_mutex.h"
#include "tscore/List.h"

class FileManager;
class TextBuffer;

class ExpandingArray;

enum RollBackCheckType {
  ROLLBACK_CHECK_AND_UPDATE,
  ROLLBACK_CHECK_ONLY,
};

//
//  class ConfigManager
//
//  public functions
//
//  checkForUserUpdate() - compares the last known modification time
//    of the active version of the file with that files current modification
//    time.  Returns true if the file has been changed manually or false
//    if it hasn't
//
// private functions
//
//  statFile(struct stat*) - a wrapper for stat(), using layout engine
//
class ConfigManager
{
public:
  // fileName_ should be rooted or a base file name.
  ConfigManager(const char *fileName_, const char *configName_, bool root_access_needed, bool isRequired_, ConfigManager *parentConfig_);
  ~ConfigManager();

  // Manual take out of lock required
  void
  acquireLock()
  {
    ink_mutex_acquire(&fileAccessLock);
  };

  void
  releaseLock()
  {
    ink_mutex_release(&fileAccessLock);
  };

  // Check if a file has changed, automatically holds the lock. Used by FileManager.
  bool checkForUserUpdate(RollBackCheckType);

  // These are getters, for FileManager to get info about a particular configuration.
  const char *
  getFileName() const
  {
    return fileName;
  }

  const char *
  getConfigName() const
  {
    return configName;
  }

  bool
  isChildManaged() const
  {
    return parentConfig != nullptr;
  }

  ConfigManager *
  getParentConfig() const
  {
    return parentConfig;
  }

  bool
  rootAccessNeeded() const
  {
    return root_access_needed;
  }

  bool
  getIsRequired() const
  {
    return isRequired;
  }

  FileManager *configFiles = nullptr; // Manager to notify on an update.

  // noncopyable
  ConfigManager(const ConfigManager &) = delete;
  ConfigManager &operator=(const ConfigManager &) = delete;

private:
  int statFile(struct stat *buf);

  ink_mutex fileAccessLock;
  char *fileName;
  char *configName;
  bool root_access_needed;
  bool isRequired;
  ConfigManager *parentConfig;
  time_t fileLastModified = 0;
};
