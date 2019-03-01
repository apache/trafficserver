/** @file

  Header file for a class that deals with plugin Dynamic Shared Objects (DSO)

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

  @section details Details

  Implements code necessary for Reverse Proxy which mostly consists of
  general purpose hostname substitution in URLs.

 */

#pragma once

#include <dlfcn.h>
#include <vector>
#include <ctime>

#include "tscore/ts_file.h"
namespace fs = ts::file;

#include "tscore/Ptr.h"
#include "tscpp/util/IntrusiveDList.h"

class PluginThreadContext : public RefCountObj
{
public:
  virtual void acquire()                  = 0;
  virtual void release()                  = 0;
  static constexpr const char *const _tag = "plugin_context"; /** @brief log tag used by this class */
};

class PluginDso : public PluginThreadContext
{
  friend class PluginFactory;

public:
  PluginDso(const fs::path &configPath, const fs::path &effectivePath, const fs::path &runtimePath);
  virtual ~PluginDso();

  /* DSO Load, unload, get symbols from DSO */
  virtual bool load(std::string &error);
  virtual bool unload(std::string &error);
  bool isLoaded();
  bool getSymbol(const char *symbol, void *&address, std::string &error) const;

  /* Accessors for effective and runtime paths */
  const fs::path &effectivePath() const;
  const fs::path &runtimePath() const;
  time_t modTime() const;

  /* List used by the plugin factory */
  using self_type  = PluginDso; ///< Self reference type.
  self_type *_next = nullptr;
  self_type *_prev = nullptr;
  using Linkage    = ts::IntrusiveLinkage<self_type>;
  using PluginList = ts::IntrusiveDList<PluginDso::Linkage>;

  /* Methods to be called when processing a list of plugins, to overloaded by the remap or the global plugins correspondingly */
  virtual void indicateReload()         = 0;
  virtual bool init(std::string &error) = 0;
  virtual void done()                   = 0;

  void acquire();
  void release();

  void incInstanceCount();
  void decInstanceCount();
  int instanceCount();

protected:
  void clean(std::string &error);

  fs::path _configPath;    /** @brief the name specified in the config file */
  fs::path _effectivePath; /** @brief the plugin installation path which was used to load DSO */
  fs::path _runtimePath;   /** @brief the plugin runtime path where the plugin was copied to be loaded */

  void *_dlh = nullptr; /** @brief dlopen handler used internally in this class, used as flag for loaded vs unloaded (nullptr) */
  std::error_code _errorCode; /** @brief used in filesystem calls */

  static constexpr const char *const _tag = "plugin_dso"; /** @brief log tag used by this class */
  time_t _mtime                           = 0;            /* @brief modification time of the DSO's file, used for checking */
  bool _preventiveCleaning                = true;

  static PluginList _list; /** @brief a global list of plugins, usually maintained by a plugin factory or plugin instance itself */
  RefCountObj _instanceCount; /** @brief used for properly calling "done" and "indicate config reload" methods by the factory */
};
