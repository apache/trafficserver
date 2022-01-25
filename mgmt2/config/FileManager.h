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

#include "tscore/Errata.h"

#include <rpc/jsonrpc/JsonRPC.h>

#include <unordered_map>
#include <string_view>
#include <forward_list>
#include <mutex>
#include <functional>

class ConfigUpdateCbTable;

class FileManager
{
public:
  enum RollBackCheckType {
    ROLLBACK_CHECK_AND_UPDATE,
    ROLLBACK_CHECK_ONLY,
  };
  class ConfigManager
  {
  public:
    // fileName_ should be rooted or a base file name.
    ConfigManager(const char *fileName_, const char *configName_, bool root_access_needed, bool isRequired_,
                  ConfigManager *parentConfig_);
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
    bool checkForUserUpdate(FileManager::RollBackCheckType);

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

    // FileManager *configFiles = nullptr; // Manager to notify on an update.

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

  using CallbackType = std::function<ts::Errata(std::string const &, std::string const &)>;

  FileManager();
  ~FileManager();
  void addFile(const char *fileName, const char *configName, bool root_access_needed, bool isRequired,
               ConfigManager *parentConfig = nullptr);

  bool getConfigObj(const char *fileName, ConfigManager **rbPtr);

  void
  registerCallback(CallbackType f)
  {
    std::lock_guard<std::mutex> guard(_callbacksMutex);
    _configCallbacks.push_front(std::move(f));
  }

  ts::Errata fileChanged(std::string const &fileName, std::string const &configName);
  ts::Errata rereadConfig();
  bool isConfigStale();
  void configFileChild(const char *parent, const char *child);

  void registerConfigPluginCallbacks(ConfigUpdateCbTable *cblist);
  void invokeConfigPluginCallbacks();

  static FileManager &
  instance()
  {
    static FileManager configFiles;
    return configFiles;
  }

private:
  ink_mutex accessLock; // Protects bindings hashtable
  ConfigUpdateCbTable *_pluginCallbackList;

  std::mutex _callbacksMutex;
  std::mutex _accessMutex;

  std::forward_list<CallbackType> _configCallbacks;

  std::unordered_map<std::string, ConfigManager *> bindings;
  void addFileHelper(const char *fileName, const char *configName, bool root_access_needed, bool isRequired,
                     ConfigManager *parentConfig);
  /// JSONRPC endpoint
  ts::Rv<YAML::Node> get_files_registry_rpc_endpoint(std::string_view const &id, YAML::Node const &params);
};

void initializeRegistry(); // implemented in AddConfigFilesHere.cc
