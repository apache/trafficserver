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

#ifndef _FILE_MANAGER_H_
#define _FILE_MANAGER_H_

/****************************************************************************
 *
 *  FileManager.h - Interface for class to manage configuration updates
 *
 *
 ****************************************************************************/

#include <stdio.h>

#include "ts/ink_hash_table.h"
#include "ts/List.h"
#include "Rollback.h"
#include "MultiFile.h"

class Rollback;

typedef void (*FileCallbackFunc)(char *, bool);

struct callbackListable {
public:
  FileCallbackFunc func;
  LINK(callbackListable, link);
};

// MUST match the ordering MFresult so that we can cast
//   MFresult to SnapResult
enum SnapResult {
  SNAP_OK,
  SNAP_NO_DIR,
  SNAP_NOT_FOUND,
  SNAP_DIR_CREATE_FAILED,
  SNAP_FILE_CREATE_FAILED,
  SNAP_FILE_ACCESS_FAILED,
  SNAP_WRITE_FAILED,
  SNAP_REMOVE_FAILED,
  SNAP_INVALID_SUBMISSION,
  SNAP_NO_NAME_GIVEN,
  SNAP_ILLEGAL_NAME
};

enum lockAction_t {
  ACQUIRE_LOCK,
  RELEASE_LOCK,
};
class ExpandingArray;

//  class FileManager
//
//  public functions:
//
//  addFile(char*, configFileInfo*) - adds a new config file to be
//       managed.  A rollback object is created for the file.
//       if the file_info ptr is not NULL, a WebFileEdit object
//       is also created
//
//  getRollbckObj(char* , RollbackPtr**) - sets *rbPtr to Rollback
//       object bound to fileName.  Returns true if there is
//       a binding and false otherwise
//
//  getWFEObj(char*, WebFileEdit**)  - sets *wfePtr to WebFileEdit
//       object bound to fileName.  Returns true if there is
//       a binding and false otherwise
//
//  registerCallback(FileCallbackFunc) - registers a callback function
//       which will get called everytime a managed file changes.  The
//       callback function should NOT use the calling thread to
//       access any Rollback objects or block for a long time
//
//  fileChanged(const char* fileName) - called by Rollback objects
//       when their contents change.  Triggers callbacks to FileCallbackFuncs
//
//  filesManaged() - returns a textBuffer that contains a new line separated
//       list of call files being managed by the FileManager.  CALLEE
//       is responsible for deleting the returned object
//
//  isConfigStale() - returns whether the in-memory files might be stale
//       compared to what is on disk.
//
//  takeSnap(const char* snapName) - creates a new snapshot with
//       passed in name
//
//  restoreSnap(const char* snapName) - restores the specified snap
//       shot
//
//  rereadConfig() - Checks all managed files to see if they have been
//       updated
//  addConfigFileGroup(char* data_str, int data_size) - update config file group infos
class FileManager : public MultiFile
{
public:
  FileManager();
  ~FileManager();
  void addFile(const char *fileName, bool root_access_needed, Rollback *parentRollback = NULL, unsigned flags = 0);
  bool getRollbackObj(const char *fileName, Rollback **rbPtr);
  void registerCallback(FileCallbackFunc func);
  void fileChanged(const char *fileName, bool incVersion);
  textBuffer *filesManaged();
  void rereadConfig();
  bool isConfigStale();
  // SnapResult takeSnap(const char* snapName);
  SnapResult takeSnap(const char *snapName, const char *snapDir);
  // SnapResult restoreSnap(const char* snapName);
  SnapResult restoreSnap(const char *snapName, const char *snapDir);
  // SnapResult removeSnap(const char* snapName);
  SnapResult removeSnap(const char *snapName, const char *snapDir);
  void displaySnapOption(textBuffer *output);
  SnapResult WalkSnaps(ExpandingArray *snapList);
  void configFileChild(const char *parent, const char *child, unsigned int options);

private:
  void doRollbackLocks(lockAction_t action);
  ink_mutex accessLock; // Protects bindings hashtable
  ink_mutex cbListLock; // Protects the CallBack List
  DLL<callbackListable> cblist;
  InkHashTable *bindings;
  // InkHashTable* g_snapshot_directory_ht;
  SnapResult copyFile(Rollback *rb, const char *snapPath);
  SnapResult readFile(const char *filePath, textBuffer *contents);
  void abortRestore(const char *abortTo);
  void createSelect(char *action, textBuffer *output, ExpandingArray *options);
  void snapErrorResponse(char *action, SnapResult error, textBuffer *output);
  void snapSuccessResponse(char *action, textBuffer *output);
  void generateRestoreConfirm(char *snapName, textBuffer *output);
  bool checkValidName(const char *name);
  const char *getParentFileName(const char *fileName);
  void addFileHelper(const char *fileName, bool root_access_needed, Rollback *parentRollback, unsigned flags = 0);
};

int snapEntryCmpFunc(const void *e1, const void *e2);

void initializeRegistry(); // implemented in AddConfigFilesHere.cc

#endif
