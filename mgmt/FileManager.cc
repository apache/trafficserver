/** @file

  Code for class to manage configuration updates

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

#include "FileManager.h"
#include "tscore/ink_platform.h"
#include "tscore/ink_file.h"
#include "ConfigManager.h"
#include "WebMgmtUtils.h"

#include <vector>
#include <algorithm>

FileManager::FileManager()
{
  ink_mutex_init(&accessLock);
  ink_mutex_init(&cbListLock);
}

// FileManager::~FileManager
//
//  There is only FileManager object in the process and it
//     should never need to be destructed except at
//     program exit
//
FileManager::~FileManager()
{
  callbackListable *cb;

  // Let other operations finish and do not start any new ones
  ink_mutex_acquire(&accessLock);

  for (cb = cblist.pop(); cb != nullptr; cb = cblist.pop()) {
    delete cb;
  }
  for (auto &&it : bindings) {
    delete it.second;
  }

  ink_mutex_release(&accessLock);
  ink_mutex_destroy(&accessLock);
  ink_mutex_destroy(&cbListLock);
}

// void FileManager::registerCallback(FileCallbackFunc func)
//
//  Adds a new callback function
//    callbacks are made whenever a configuration file has
//    changed
//
//  The callback function is responsible for free'ing
//    the string the string it is passed
//
void
FileManager::registerCallback(FileCallbackFunc func)
{
  callbackListable *newcb = new callbackListable();
  ink_assert(newcb != nullptr);
  newcb->func = func;
  ink_mutex_acquire(&cbListLock);
  cblist.push(newcb);
  ink_mutex_release(&cbListLock);
}

// void FileManager::addFile(char* fileName, const configFileInfo* file_info,
//  ConfigManager* parentConfig)
//
//  for the baseFile, creates a ConfigManager object for it
//
//  if file_info is not null, a WebFileEdit object is also created for
//    the file
//
//  Pointers to the new objects are stored in the bindings hashtable
//
void
FileManager::addFile(const char *fileName, const char *configName, bool root_access_needed, ConfigManager *parentConfig)
{
  ink_mutex_acquire(&accessLock);
  addFileHelper(fileName, configName, root_access_needed, parentConfig);
  ink_mutex_release(&accessLock);
}

// caller must hold the lock
void
FileManager::addFileHelper(const char *fileName, const char *configName, bool root_access_needed, ConfigManager *parentConfig)
{
  ink_assert(fileName != nullptr);

  ConfigManager *rb = new ConfigManager(fileName, configName, root_access_needed, parentConfig);
  rb->configFiles   = this;

  bindings.emplace(rb->getFileName(), rb);
}

// bool FileManager::getConfigManagerObj(char* fileName, ConfigManager** rbPtr)
//
//  Sets rbPtr to the ConfigManager object associated
//    with the passed in fileName.
//
//  If there is no binding, false is returned
//
bool
FileManager::getConfigObj(const char *fileName, ConfigManager **rbPtr)
{
  ink_mutex_acquire(&accessLock);
  auto it    = bindings.find(fileName);
  bool found = it != bindings.end();
  ink_mutex_release(&accessLock);

  *rbPtr = found ? it->second : nullptr;
  return found;
}

// bool FileManager::fileChanged(const char* fileName)
//
//  Called by the ConfigManager class whenever a config has changed
//     Initiates callbacks
//
//
void
FileManager::fileChanged(const char *fileName, const char *configName)
{
  callbackListable *cb;
  char *filenameCopy, *confignameCopy;
  Debug("lm", "filename changed %s", fileName);
  ink_mutex_acquire(&cbListLock);

  for (cb = cblist.head; cb != nullptr; cb = cb->link.next) {
    // Dup the string for each callback to be
    //  defensive in case it's modified when it's not supposed to be
    confignameCopy = ats_strdup(configName);
    filenameCopy   = ats_strdup(fileName);
    (*cb->func)(filenameCopy, confignameCopy);
    ats_free(filenameCopy);
    ats_free(confignameCopy);
  }
  ink_mutex_release(&cbListLock);
}

// void FileManger::rereadConfig()
//
//   Iterates through the list of managed files and
//     calls ConfigManager::checkForUserUpdate on them
//
//   although it is tempting, DO NOT CALL FROM SIGNAL HANDLERS
//      This function is not Async-Signal Safe.  It
//      is thread safe
void
FileManager::rereadConfig()
{
  ConfigManager *rb;

  std::vector<ConfigManager *> changedFiles;
  std::vector<ConfigManager *> parentFileNeedChange;
  size_t n;
  ink_mutex_acquire(&accessLock);
  for (auto &&it : bindings) {
    rb = it.second;
    // ToDo: rb->isVersions() was always true before, because numberBackups was always >= 1. So ROLLBACK_CHECK_ONLY could not
    // happen at all...
    if (rb->checkForUserUpdate()) {
      changedFiles.push_back(rb);
      if (rb->isChildManaged()) {
        if (std::find(parentFileNeedChange.begin(), parentFileNeedChange.end(), rb->getParentConfig()) ==
            parentFileNeedChange.end()) {
          parentFileNeedChange.push_back(rb->getParentConfig());
        }
      }
    }
  }

  std::vector<ConfigManager *> childFileNeedDelete;
  n = changedFiles.size();
  for (size_t i = 0; i < n; i++) {
    if (changedFiles[i]->isChildManaged()) {
      continue;
    }
    // for each parent file, if it is changed, then delete all its children
    for (auto &&it : bindings) {
      rb = it.second;
      if (rb->getParentConfig() == changedFiles[i]) {
        if (std::find(childFileNeedDelete.begin(), childFileNeedDelete.end(), rb) == childFileNeedDelete.end()) {
          childFileNeedDelete.push_back(rb);
        }
      }
    }
  }
  n = childFileNeedDelete.size();
  for (size_t i = 0; i < n; i++) {
    bindings.erase(childFileNeedDelete[i]->getFileName());
    delete childFileNeedDelete[i];
  }
  ink_mutex_release(&accessLock);

  n = parentFileNeedChange.size();
  for (size_t i = 0; i < n; i++) {
    if (std::find(changedFiles.begin(), changedFiles.end(), parentFileNeedChange[i]) == changedFiles.end()) {
      fileChanged(parentFileNeedChange[i]->getFileName(), parentFileNeedChange[i]->getConfigName());
    }
  }
  // INKqa11910
  // need to first check that enable_customizations is enabled
  bool found;
  int enabled = static_cast<int>(REC_readInteger("proxy.config.body_factory.enable_customizations", &found));

  if (found && enabled) {
    fileChanged("proxy.config.body_factory.template_sets_dir", "proxy.config.body_factory.template_sets_dir");
  }
  fileChanged("proxy.config.ssl.server.ticket_key.filename", "proxy.config.ssl.server.ticket_key.filename");
}

bool
FileManager::isConfigStale()
{
  ConfigManager *rb;
  bool stale = false;

  ink_mutex_acquire(&accessLock);
  for (auto &&it : bindings) {
    rb = it.second;
    if (rb->checkForUserUpdate()) {
      stale = true;
      break;
    }
  }

  ink_mutex_release(&accessLock);
  return stale;
}

// void configFileChild(const char *parent, const char *child)
//
// Add child to the bindings with parentConfig
void
FileManager::configFileChild(const char *parent, const char *child)
{
  ConfigManager *parentConfig = nullptr;
  ink_mutex_acquire(&accessLock);
  if (auto it = bindings.find(parent); it != bindings.end()) {
    parentConfig = it->second;
    addFileHelper(child, "", parentConfig->rootAccessNeeded(), parentConfig);
  }
  ink_mutex_release(&accessLock);
}
