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
#include "mgmt/config/FileManager.h"

#include <vector>
#include <algorithm>

#include "api/InkAPIInternal.h" // TODO: this brings a lot of dependencies, double check this.

#include "tscore/ink_platform.h"
#include "tscore/ink_file.h"
#include "../../records/P_RecCore.h"
#include "tscore/Diags.h"
#include "tscore/Filenames.h"
#include "tscore/Layout.h"

#if HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC
#define TS_ARCHIVE_STAT_MTIME(t) ((t).st_mtime * 1000000000 + (t).st_mtimespec.tv_nsec)
#elif HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC
#define TS_ARCHIVE_STAT_MTIME(t) ((t).st_mtime * 1000000000 + (t).st_mtim.tv_nsec)
#else
#define TS_ARCHIVE_STAT_MTIME(t) ((t).st_mtime * 1000000000)
#endif

namespace
{
DbgCtl dbg_ctl{"filemanager"};

swoc::Errata
handle_file_reload(std::string const &fileName, std::string const &configName)
{
  Dbg(dbg_ctl, "handling reload %s - %s", fileName.c_str(), configName.c_str());
  swoc::Errata ret;
  // TODO: make sure records holds the name after change, if not we should change it.
  if (fileName == ts::filename::RECORDS) {
    if (auto zret = RecReadYamlConfigFile(); zret) {
      RecConfigWarnIfUnregistered();
    } else {
      ret.note("Error reading {}", fileName).note(zret);
    }
  } else {
    RecT  rec_type;
    char *data = const_cast<char *>(configName.c_str());
    if (RecGetRecordType(data, &rec_type) == REC_ERR_OKAY && rec_type == RECT_CONFIG) {
      RecSetSyncRequired(data);
    } else {
      ret.note("Unknown file change {}.", configName);
    }
  }

  return ret;
}

// JSONRPC endpoint defs.
const std::string CONFIG_REGISTRY_KEY_STR{"config_registry"};
const std::string FILE_PATH_KEY_STR{"file_path"};
const std::string RECORD_NAME_KEY_STR{"config_record_name"};
const std::string PARENT_CONFIG_KEY_STR{"parent_config"};
const std::string ROOT_ACCESS_NEEDED_KEY_STR{"root_access_needed"};
const std::string IS_REQUIRED_KEY_STR{"is_required"};
const std::string NA_STR{"N/A"};

} // namespace

FileManager::FileManager()
{
  ink_mutex_init(&accessLock);
  this->registerCallback(&handle_file_reload);

  // Register the files registry jsonrpc endpoint
  rpc::add_method_handler("filemanager.get_files_registry",
                          [this](std::string_view const &id, const YAML::Node &req) -> swoc::Rv<YAML::Node> {
                            return get_files_registry_rpc_endpoint(id, req);
                          },
                          &rpc::core_ats_rpc_service_provider_handle, {{rpc::NON_RESTRICTED_API}});
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

swoc::Errata
FileManager::fileChanged(std::string const &fileName, std::string const &configName)
{
  Dbg(dbg_ctl, "file changed %s", fileName.c_str());
  swoc::Errata ret;

  std::lock_guard<std::mutex> guard(_callbacksMutex);
  for (auto const &call : _configCallbacks) {
    if (auto const &r = call(fileName, configName); !r) {
      Dbg(dbg_ctl, "something back from callback %s", fileName.c_str());
      if (ret.empty()) {
        ret.note("Errors while reloading configurations.");
      }
      ret.note(r);
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
  Dbg(dbg_ctl, "invoke plugin callbacks");
  if (_pluginCallbackList) {
    _pluginCallbackList->invoke();
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
swoc::Errata
FileManager::rereadConfig()
{
  swoc::Errata ret;

  ConfigManager               *rb;
  std::vector<ConfigManager *> changedFiles;
  std::vector<ConfigManager *> parentFileNeedChange;
  size_t                       n;
  ink_mutex_acquire(&accessLock);
  for (auto &&it : bindings) {
    rb = it.second;
    // ToDo: rb->isVersions() was always true before, because numberBackups was always >= 1. So ROLLBACK_CHECK_ONLY could not
    // happen at all...
    if (rb->checkForUserUpdate(FileManager::ROLLBACK_CHECK_AND_UPDATE)) {
      Dbg(dbg_ctl, "File %s changed.", it.first.c_str());
      auto const &r = fileChanged(rb->getFileName(), rb->getConfigName());

      if (!r) {
        ret.note("Errors while reloading configurations.");
        ret.note(r);
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
        if (ret.empty()) {
          ret.note("Error while handling parent file name changed.");
        }
        ret.note(r);
      }
    }
  }
  // INKqa11910
  // need to first check that enable_customizations is enabled
  bool found;
  int  enabled = static_cast<int>(REC_readInteger("proxy.config.body_factory.enable_customizations", &found));

  if (found && enabled) {
    if (auto const &r = fileChanged("proxy.config.body_factory.template_sets_dir", "proxy.config.body_factory.template_sets_dir");
        !r) {
      if (ret.empty()) {
        ret.note("Error while loading body factory templates");
      }
      ret.note(r);
    }
  }

  if (auto const &r = fileChanged("proxy.config.ssl.server.ticket_key.filename", "proxy.config.ssl.server.ticket_key.filename");
      !r) {
    if (ret.empty()) {
      ret.note("Error while loading ticket keys");
    }
    ret.note(r);
  }

  return ret;
}

bool
FileManager::isConfigStale()
{
  ConfigManager *rb;
  bool           stale = false;

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
    Dbg(dbg_ctl, "Adding child file %s to %s parent", child, parent);
    parentConfig = it->second;
    addFileHelper(child, "", parentConfig->rootAccessNeeded(), parentConfig->getIsRequired(), parentConfig);
  }
  ink_mutex_release(&accessLock);
}

auto
FileManager::get_files_registry_rpc_endpoint(std::string_view const & /* id ATS_UNUSED */,
                                             YAML::Node const & /* params ATS_UNUSED */) -> swoc::Rv<YAML::Node>
{
  // If any error, the rpc manager will catch it and respond with it.
  YAML::Node configs{YAML::NodeType::Sequence};
  {
    ink_scoped_mutex_lock lock(accessLock);
    for (auto &&it : bindings) {
      if (ConfigManager *cm = it.second; cm) {
        YAML::Node  element{YAML::NodeType::Map};
        std::string sysconfdir(RecConfigReadConfigDir());
        element[FILE_PATH_KEY_STR]          = Layout::get()->relative_to(sysconfdir, cm->getFileName());
        element[RECORD_NAME_KEY_STR]        = cm->getConfigName();
        element[PARENT_CONFIG_KEY_STR]      = (cm->isChildManaged() ? cm->getParentConfig()->getFileName() : NA_STR);
        element[ROOT_ACCESS_NEEDED_KEY_STR] = cm->rootAccessNeeded();
        element[IS_REQUIRED_KEY_STR]        = cm->getIsRequired();
        configs.push_back(element);
      }
    }
  }

  YAML::Node registry;
  registry[CONFIG_REGISTRY_KEY_STR] = configs;
  return registry;
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
  fileName   = std::string(fileName_);
  configName = std::string(configName_);

  ink_mutex_init(&fileAccessLock);
  // Check to make sure that our configuration file exists
  //
  if (statFile(&fileInfo) < 0) {
    Dbg(dbg_ctl, "%s  Unable to load: %s", fileName.c_str(), strerror(errno));

    if (isRequired) {
      Dbg(dbg_ctl, " Unable to open required configuration file %s\n\t failed :%s", fileName.c_str(), strerror(errno));
    }
  } else {
    fileLastModified = TS_ARCHIVE_STAT_MTIME(fileInfo);
  }
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
  int         statResult;
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
  bool        result;

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
    Dbg(dbg_ctl, "User has changed config file %s\n", fileName.c_str());
    result = true;
  } else {
    result = false;
  }

  ink_mutex_release(&fileAccessLock);
  return result;
}
