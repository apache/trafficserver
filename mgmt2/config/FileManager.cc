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

#include <vector>
#include <algorithm>

#include "InkAPIInternal.h" // TODO: this brings a lot of dependencies, double check this.

#include "tscore/ink_platform.h"
#include "tscore/ink_file.h"
#include "ConfigManager.h"
#include "records/P_RecCore.h"
#include "tscore/Diags.h"
#include "tscore/Filenames.h"
#include "tscore/I_Layout.h"
#include <tscore/BufferWriter.h>

#if HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC
#define TS_ARCHIVE_STAT_MTIME(t) ((t).st_mtime * 1000000000 + (t).st_mtimespec.tv_nsec)
#elif HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC
#define TS_ARCHIVE_STAT_MTIME(t) ((t).st_mtime * 1000000000 + (t).st_mtim.tv_nsec)
#else
#define TS_ARCHIVE_STAT_MTIME(t) ((t).st_mtime * 1000000000)
#endif

static constexpr auto logTag{"filemanager"};
namespace
{
ts::Errata
handle_file_reload(std::string const &fileName, std::string const &configName)
{
  Debug(logTag, "handling reload %s - %s", fileName.c_str(), configName.c_str());
  ts::Errata ret;
  // TODO: make sure records holds the name after change, if not we should change it.
  if (fileName == ts::filename::RECORDS) {
    if (RecReadConfigFile() == REC_ERR_OKAY) {
      RecConfigWarnIfUnregistered();
    } else {
      std::string str;
      ret.push(1, ts::bwprint(str, "Error reading {}.", fileName));
    }
  } else {
    RecT rec_type;
    char *data = const_cast<char *>(configName.c_str());
    if (RecGetRecordType(data, &rec_type) == REC_ERR_OKAY && rec_type == RECT_CONFIG) {
      RecSetSyncRequired(data);
    } else {
      std::string str;
      ret.push(1, ts::bwprint(str, "Unknown file change {}.", configName));
    }
  }

  return ret;
}
} // namespace

FileManager::FileManager()
{
  ink_mutex_init(&accessLock);
  this->registerCallback(&handle_file_reload);
}

// FileManager::~FileManager
//
//  There is only FileManager object in the process and it
//     should never need to be destructed except at
//     program exit
//
FileManager::~FileManager()
{
  // Let other operations finish and do not start any new ones
  ink_mutex_acquire(&accessLock);

  for (auto &&it : bindings) {
    delete it.second;
  }

  ink_mutex_release(&accessLock);
  ink_mutex_destroy(&accessLock);
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
FileManager::addFile(const char *fileName, const char *configName, bool root_access_needed, bool isRequired,
                     ConfigManager *parentConfig)
{
  ink_mutex_acquire(&accessLock);
  addFileHelper(fileName, configName, root_access_needed, isRequired, parentConfig);
  ink_mutex_release(&accessLock);
}

// caller must hold the lock
void
FileManager::addFileHelper(const char *fileName, const char *configName, bool root_access_needed, bool isRequired,
                           ConfigManager *parentConfig)
{
  ink_assert(fileName != nullptr);
  ConfigManager *configManager = new ConfigManager(fileName, configName, root_access_needed, isRequired, parentConfig);
  bindings.emplace(configManager->getFileName(), configManager);
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

ts::Errata
FileManager::fileChanged(std::string const &fileName, std::string const &configName)
{
  Debug("filemanager", "file changed %s", fileName.c_str());
  ts::Errata ret;

  std::lock_guard<std::mutex> guard(_callbacksMutex);
  for (auto const &call : _configCallbacks) {
    if (auto const &r = call(fileName, configName); !r) {
      Debug("filemanager", "something back from callback %s", fileName.c_str());
      std::for_each(r.begin(), r.end(), [&ret](auto &&e) { ret.push(e); });
    }
  }

  return ret;
}

// TODO: To do the following here, we have to pull up a lot of dependencies we don't really
// need, #include "InkAPIInternal.h" brings plenty of them. Double check this approach. RPC will
// also be able to pass messages to plugins, once that's designed it can also cover this.
void
FileManager::registerConfigPluginCallbacks(ConfigUpdateCbTable *cblist)
{
  _pluginCallbackList = cblist;
}

void
FileManager::invokeConfigPluginCallbacks()
{
  Debug("filemanager", "invoke plugin callbacks");
  static const std::string_view s{"*"};
  if (_pluginCallbackList) {
    _pluginCallbackList->invoke(s.data());
  }
}

// void FileManger::rereadConfig()
//
//   Iterates through the list of managed files and
//     calls ConfigManager::checkForUserUpdate on them
//
//   although it is tempting, DO NOT CALL FROM SIGNAL HANDLERS
//      This function is not Async-Signal Safe.  It
//      is thread safe
ts::Errata
FileManager::rereadConfig()
{
  ts::Errata ret;

  ConfigManager *rb;
  std::vector<ConfigManager *> changedFiles;
  std::vector<ConfigManager *> parentFileNeedChange;
  size_t n;
  ink_mutex_acquire(&accessLock);
  for (auto &&it : bindings) {
    rb = it.second;
    // ToDo: rb->isVersions() was always true before, because numberBackups was always >= 1. So ROLLBACK_CHECK_ONLY could not
    // happen at all...
    if (rb->checkForUserUpdate(FileManager::ROLLBACK_CHECK_AND_UPDATE)) {
      Debug(logTag, "File %s changed.", it.first.c_str());
      auto const &r = fileChanged(rb->getFileName(), rb->getConfigName());

      if (!r) {
        std::for_each(r.begin(), r.end(), [&ret](auto &&e) { ret.push(e); });
      }

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
      if (auto const &r = fileChanged(parentFileNeedChange[i]->getFileName(), parentFileNeedChange[i]->getConfigName()); !r) {
        std::for_each(r.begin(), r.end(), [&ret](auto &&e) { ret.push(e); });
      }
    }
  }
  // INKqa11910
  // need to first check that enable_customizations is enabled
  bool found;
  int enabled = static_cast<int>(REC_readInteger("proxy.config.body_factory.enable_customizations", &found));

  if (found && enabled) {
    if (auto const &r = fileChanged("proxy.config.body_factory.template_sets_dir", "proxy.config.body_factory.template_sets_dir");
        !r) {
      std::for_each(r.begin(), r.end(), [&ret](auto &&e) { ret.push(e); });
    }
  }

  if (auto const &r = fileChanged("proxy.config.ssl.server.ticket_key.filename", "proxy.config.ssl.server.ticket_key.filename");
      !r) {
    std::for_each(r.begin(), r.end(), [&ret](auto &&e) { ret.push(e); });
  }

  return ret;
}

bool
FileManager::isConfigStale()
{
  ConfigManager *rb;
  bool stale = false;

  ink_mutex_acquire(&accessLock);
  for (auto &&it : bindings) {
    rb = it.second;
    if (rb->checkForUserUpdate(FileManager::ROLLBACK_CHECK_ONLY)) {
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
    Debug(logTag, "Adding child file %s to %s parent", child, parent);
    parentConfig = it->second;
    addFileHelper(child, "", parentConfig->rootAccessNeeded(), parentConfig->getIsRequired(), parentConfig);
  }
  ink_mutex_release(&accessLock);
}

/// ConfigFile

FileManager::ConfigManager::ConfigManager(const char *fileName_, const char *configName_, bool root_access_needed_,
                                          bool isRequired_, ConfigManager *parentConfig_)
  : root_access_needed(root_access_needed_), isRequired(isRequired_), parentConfig(parentConfig_)
{
  // ExpandingArray existVer(25, true); // Existing versions
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
    Debug(logTag, "%s  Unable to load: %s", fileName, strerror(errno));

    if (isRequired) {
      Debug(logTag, " Unable to open required configuration file %s\n\t failed :%s", fileName, strerror(errno));
    }
  } else {
    fileLastModified = TS_ARCHIVE_STAT_MTIME(fileInfo);
  }
}

FileManager::ConfigManager::~ConfigManager()
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
FileManager::ConfigManager::statFile(struct stat *buf)
{
  int statResult;
  std::string sysconfdir(RecConfigReadConfigDir());
  std::string filePath = Layout::get()->relative_to(sysconfdir, fileName);

  statResult = root_access_needed ? elevating_stat(filePath.c_str(), buf) : stat(filePath.c_str(), buf);

  return statResult;
}

// bool ConfigManager::checkForUserUpdate(RollBackCheckType how)
//
//  Called to check if the file has been changed  by the user.
//  Timestamps are compared to see if a change occurred
bool
FileManager::ConfigManager::checkForUserUpdate(FileManager::RollBackCheckType how)
{
  struct stat fileInfo;
  bool result;

  ink_mutex_acquire(&fileAccessLock);

  if (this->statFile(&fileInfo) < 0) {
    ink_mutex_release(&fileAccessLock);
    return false;
  }

  if (fileLastModified < TS_ARCHIVE_STAT_MTIME(fileInfo)) {
    if (how == FileManager::ROLLBACK_CHECK_AND_UPDATE) {
      fileLastModified = TS_ARCHIVE_STAT_MTIME(fileInfo);
      // TODO: syslog????
    }
    Debug(logTag, "User has changed config file %s\n", fileName);
    result = true;
  } else {
    result = false;
  }

  ink_mutex_release(&fileAccessLock);
  return result;
}
