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
#include "Rollback.h"
#include "WebMgmtUtils.h"

#include <vector>
#include <algorithm>

FileManager::FileManager()
{
  bindings = ink_hash_table_create(InkHashTableKeyType_String);
  ink_assert(bindings != nullptr);

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
  Rollback *rb;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  // Let other operations finish and do not start any new ones
  ink_mutex_acquire(&accessLock);

  for (cb = cblist.pop(); cb != nullptr; cb = cblist.pop()) {
    delete cb;
  }

  for (entry = ink_hash_table_iterator_first(bindings, &iterator_state); entry != nullptr;
       entry = ink_hash_table_iterator_next(bindings, &iterator_state)) {
    rb = (Rollback *)ink_hash_table_entry_value(bindings, entry);

    delete rb;
  }

  ink_hash_table_destroy(bindings);

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
//  Rollback* parentRollback)
//
//  for the baseFile, creates a Rollback object for it
//
//  if file_info is not null, a WebFileEdit object is also created for
//    the file
//
//  Pointers to the new objects are stored in the bindings hashtable
//
void
FileManager::addFile(const char *fileName, const char *configName, bool root_access_needed, Rollback *parentRollback,
                     unsigned flags)
{
  ink_mutex_acquire(&accessLock);
  addFileHelper(fileName, configName, root_access_needed, parentRollback, flags);
  ink_mutex_release(&accessLock);
}

// caller must hold the lock
void
FileManager::addFileHelper(const char *fileName, const char *configName, bool root_access_needed, Rollback *parentRollback,
                           unsigned flags)
{
  ink_assert(fileName != nullptr);

  Rollback *rb    = new Rollback(fileName, configName, root_access_needed, parentRollback, flags);
  rb->configFiles = this;

  ink_hash_table_insert(bindings, fileName, rb);
}

// bool FileManager::getRollbackObj(char* fileName, Rollback** rbPtr)
//
//  Sets rbPtr to the rollback object associated
//    with the passed in fileName.
//
//  If there is no binding, falseis returned
//
bool
FileManager::getRollbackObj(const char *fileName, Rollback **rbPtr)
{
  InkHashTableValue lookup = nullptr;
  int found;

  ink_mutex_acquire(&accessLock);
  found = ink_hash_table_lookup(bindings, fileName, &lookup);
  ink_mutex_release(&accessLock);

  *rbPtr = (Rollback *)lookup;
  return (found == 0) ? false : true;
}

// bool FileManager::fileChanged(const char* fileName)
//
//  Called by the Rollback class whenever a a config has changed
//     Initiates callbacks
//
//
void
FileManager::fileChanged(const char *fileName, const char *configName, bool incVersion)
{
  callbackListable *cb;
  char *filenameCopy, *confignameCopy;
  Debug("lm", "filename changed %s", fileName);
  ink_mutex_acquire(&cbListLock);

  for (cb = cblist.head; cb != nullptr; cb = cb->link.next) {
    // Dup the string for each callback to be
    //  defensive incase it modified when it is not supposed to be
    confignameCopy = ats_strdup(configName);
    filenameCopy   = ats_strdup(fileName);
    (*cb->func)(filenameCopy, confignameCopy, incVersion);
    ats_free(filenameCopy);
    ats_free(confignameCopy);
  }
  ink_mutex_release(&cbListLock);
}

// void FileManger::rereadConfig()
//
//   Interates through the list of managed files and
//     calls Rollback::checkForUserUpdate on them
//
//   although it is tempting, DO NOT CALL FROM SIGNAL HANDLERS
//      This function is not Async-Signal Safe.  It
//      is thread safe
void
FileManager::rereadConfig()
{
  Rollback *rb;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  std::vector<Rollback *> changedFiles;
  std::vector<Rollback *> parentFileNeedChange;
  size_t n;
  ink_mutex_acquire(&accessLock);
  for (entry = ink_hash_table_iterator_first(bindings, &iterator_state); entry != nullptr;
       entry = ink_hash_table_iterator_next(bindings, &iterator_state)) {
    rb = (Rollback *)ink_hash_table_entry_value(bindings, entry);
    if (rb->checkForUserUpdate(rb->isVersioned() ? ROLLBACK_CHECK_AND_UPDATE : ROLLBACK_CHECK_ONLY)) {
      changedFiles.push_back(rb);
      if (rb->isChildRollback()) {
        if (std::find(parentFileNeedChange.begin(), parentFileNeedChange.end(), rb->getParentRollback()) ==
            parentFileNeedChange.end()) {
          parentFileNeedChange.push_back(rb->getParentRollback());
        }
      }
    }
  }

  std::vector<Rollback *> childFileNeedDelete;
  n = changedFiles.size();
  for (size_t i = 0; i < n; i++) {
    if (changedFiles[i]->isChildRollback()) {
      continue;
    }
    // for each parent file, if it is changed, then delete all its children
    for (entry = ink_hash_table_iterator_first(bindings, &iterator_state); entry != nullptr;
         entry = ink_hash_table_iterator_next(bindings, &iterator_state)) {
      rb = (Rollback *)ink_hash_table_entry_value(bindings, entry);
      if (rb->getParentRollback() == changedFiles[i]) {
        if (std::find(childFileNeedDelete.begin(), childFileNeedDelete.end(), rb) == childFileNeedDelete.end()) {
          childFileNeedDelete.push_back(rb);
        }
      }
    }
  }
  n = childFileNeedDelete.size();
  for (size_t i = 0; i < n; i++) {
    ink_hash_table_delete(bindings, childFileNeedDelete[i]->getFileName());
    delete childFileNeedDelete[i];
  }
  ink_mutex_release(&accessLock);

  n = parentFileNeedChange.size();
  for (size_t i = 0; i < n; i++) {
    if (std::find(changedFiles.begin(), changedFiles.end(), parentFileNeedChange[i]) == changedFiles.end()) {
      fileChanged(parentFileNeedChange[i]->getFileName(), parentFileNeedChange[i]->getConfigName(), true);
    }
  }
  // INKqa11910
  // need to first check that enable_customizations is enabled
  bool found;
  int enabled = (int)REC_readInteger("proxy.config.body_factory.enable_customizations", &found);
  if (found && enabled) {
    fileChanged("proxy.config.body_factory.template_sets_dir", "proxy.config.body_factory.template_sets_dir", true);
  }
  fileChanged("proxy.config.ssl.server.ticket_key.filename", "proxy.config.ssl.server.ticket_key.filename", true);
}

bool
FileManager::isConfigStale()
{
  Rollback *rb;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;
  bool stale = false;

  ink_mutex_acquire(&accessLock);
  for (entry = ink_hash_table_iterator_first(bindings, &iterator_state); entry != nullptr;
       entry = ink_hash_table_iterator_next(bindings, &iterator_state)) {
    rb = (Rollback *)ink_hash_table_entry_value(bindings, entry);
    if (rb->checkForUserUpdate(ROLLBACK_CHECK_ONLY)) {
      stale = true;
      break;
    }
  }

  ink_mutex_release(&accessLock);
  return stale;
}

// void configFileChild(const char *parent, const char *child)
//
// Add child to the bindings with parentRollback
void
FileManager::configFileChild(const char *parent, const char *child, unsigned flags)
{
  InkHashTableValue lookup;
  Rollback *parentRollback = nullptr;
  ink_mutex_acquire(&accessLock);
  int htfound = ink_hash_table_lookup(bindings, parent, &lookup);
  if (htfound) {
    parentRollback = (Rollback *)lookup;
    addFileHelper(child, "", parentRollback->rootAccessNeeded(), parentRollback, flags);
  }
  ink_mutex_release(&accessLock);
}
