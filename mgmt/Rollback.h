/** @file

  Interface for class to allow rollback of configuration files

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

//
//  class Rollback
//
//  public functions
//
//  checkForUserUpdate() - compares the last known modification time
//    of the active version of the file with that files current modification
//    time.  Returns true if the file has been changed manually or false
//    if it hasn't
////
// private functions
//
//  openFile(int oflags) - a wrapper for open
//
//  statFile(struct stat*) - a wrapper for stat
//
//  createPathStr() - creates a string to the file. Callee deletes storage.
//

class Rollback
{
public:
  // fileName_ should be rooted or a base file name.
  Rollback(const char *fileName_, const char *configName_, bool root_access_needed, Rollback *parentRollback = nullptr,
           unsigned flags = 0);
  ~Rollback();

  // Manual take out of lock required
  void
  acquireLock()
  {
    ink_mutex_acquire(&fileAccessLock);
  }

  void
  releaseLock()
  {
    ink_mutex_release(&fileAccessLock);
  }

  // Automatically take out lock
  bool checkForUserUpdate();
  bool setLastModifiedTime();

  // Not file based so no lock necessary
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
  isChildRollback() const
  {
    return parentRollback != nullptr;
  }

  Rollback *
  getParentRollback() const
  {
    return parentRollback;
  }

  bool
  rootAccessNeeded() const
  {
    return root_access_needed;
  }

  FileManager *configFiles; // Manager to notify on an update.

  // noncopyable
  Rollback(const Rollback &) = delete;
  Rollback &operator=(const Rollback &) = delete;

private:
  int openFile(int oflags, int *errnoPtr = nullptr);
  int closeFile(int fd, bool callSync);
  int statFile(struct stat *buf);
  char *createPathStr();

  ink_mutex fileAccessLock;
  char *fileName;
  char *fileBaseName;
  char *configName;
  size_t fileNameLen;
  bool root_access_needed;
  Rollback *parentRollback;
  time_t fileLastModified;
};
