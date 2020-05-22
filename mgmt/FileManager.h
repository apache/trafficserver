/** @file

  Interface for class to manage configuration updates

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

#include <unordered_map>

class ExpandingArray;
class ConfigManager;

typedef void (*FileCallbackFunc)(char *, char *);

struct callbackListable {
public:
  FileCallbackFunc func;
  LINK(callbackListable, link);
};

enum lockAction_t {
  ACQUIRE_LOCK,
  RELEASE_LOCK,
};

//  class FileManager
//
//  public functions:
//
//  addFile(char*, char *, bool, configFileInfo*) - adds a new config file to be
//       managed.  A ConfigManager object is created for the file.
//       if the file_info ptr is not NULL, a WebFileEdit object
//       is also created
//
//  getRollbckObj(char* , ConfigManagerPtr**) - sets *rbPtr to ConfigManager
//       object bound to fileName.  Returns true if there is
//       a binding and false otherwise
//
//  getWFEObj(char*, WebFileEdit**)  - sets *wfePtr to WebFileEdit
//       object bound to fileName.  Returns true if there is
//       a binding and false otherwise
//
//  registerCallback(FileCallbackFunc) - registers a callback function
//       which will get called every time a managed file changes.  The
//       callback function should NOT use the calling thread to
//       access any ConfigManager objects or block for a long time
//
//  fileChanged(const char* fileName, const char *configName) - called by ConfigManager objects
//       when their contents change.  Triggers callbacks to FileCallbackFuncs
//
//  isConfigStale() - returns whether the in-memory files might be stale
//       compared to what is on disk.
//
//  rereadConfig() - Checks all managed files to see if they have been
//       updated
//  addConfigFileGroup(char* data_str, int data_size) - update config file group infos
class FileManager
{
public:
  FileManager();
  ~FileManager();
  void addFile(const char *fileName, const char *configName, bool root_access_needed, bool isRequired,
               ConfigManager *parentConfig = nullptr);
  bool getConfigObj(const char *fileName, ConfigManager **rbPtr);
  void registerCallback(FileCallbackFunc func);
  void fileChanged(const char *fileName, const char *configName);
  void rereadConfig();
  bool isConfigStale();
  void configFileChild(const char *parent, const char *child);

private:
  ink_mutex accessLock; // Protects bindings hashtable
  ink_mutex cbListLock; // Protects the CallBack List
  DLL<callbackListable> cblist;
  std::unordered_map<std::string_view, ConfigManager *> bindings;
  void addFileHelper(const char *fileName, const char *configName, bool root_access_needed, bool isRequired,
                     ConfigManager *parentConfig);
};

void initializeRegistry(); // implemented in AddConfigFilesHere.cc
