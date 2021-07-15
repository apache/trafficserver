/** @file

  Functionality allowing to load all plugins from a single config reload (header).

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

#include <vector>

#include "tscore/Ptr.h"
#include "PluginDso.h"
#include "RemapPluginInfo.h"

#include "tscore/Ptr.h"
#include "tscpp/util/IntrusiveDList.h"

#include "tscore/ink_uuid.h"
#include "ts/apidefs.h"

/**
 * @brief Bundles plugin info + plugin instance data to be used always together.
 */
class RemapPluginInst
{
public:
  RemapPluginInst()                  = delete;
  RemapPluginInst(RemapPluginInst &) = delete;
  RemapPluginInst(RemapPluginInfo &plugin);
  ~RemapPluginInst();

  /* Used by the PluginFactory */
  static RemapPluginInst *init(RemapPluginInfo *plugin, int argc, char **argv, std::string &error);
  void done();

  /* Used by the traffic server core while processing requests */
  TSRemapStatus doRemap(TSHttpTxn rh, TSRemapRequestInfo *rri);
  void osResponse(TSHttpTxn rh, int os_response_type);

  /* List used by the plugin factory */
  using self_type  = RemapPluginInst; ///< Self reference type.
  self_type *_next = nullptr;
  self_type *_prev = nullptr;
  using Linkage    = ts::IntrusiveLinkage<self_type>;

  /* Plugin instance = the plugin info + the data returned by the init callback */
  RemapPluginInfo &_plugin;
  void *_instance = nullptr;
};

/**
 * @brief loads plugins, instantiates and keep track of plugin instances created by this factory.
 *
 * - Handles looking through search directories to determine final plugin canonical file name to be used (called here effective
 * path).
 * - Makes sure we load each DSO only once per effective path.
 * - Keeps track of all loaded remap plugins and their instances.
 * - Maitains the notion of plugin runtime paths and makes sure every factory instance uses different runtime paths for its plugins.
 * - Makes sure plugin DSOs are loaded for the lifetime of the PluginFactory.
 *
 * Each plugin factory instance corresponds to a config reload, each new config file set is meant to use a new factory instance.
 * A notion of runtime directory is maintained to make sure the DSO library files are not erased or modified while the library are
 * loaded in memory and make sure if the library file is overridden with a new DSO file that the new overriding plugin's
 * functionality will be loaded with the next factory, it also handles some problems noticed on different OSes in handling
 * filesystem links and different dl library implementations.
 *
 * @note This is meant to unify the way global and remap plugins are (re)loaded (global plugin support is not implemented yet).
 * @note In the case of a mixed plugin, getRemapPlugin/dynamicReloadEnabled can be internally overwritten if the plugin opt out to
 * take part in the dynamic reload process.
 */
class PluginFactory
{
  using PluginInstList = ts::IntrusiveDList<RemapPluginInst::Linkage>;

public:
  PluginFactory();
  virtual ~PluginFactory();

  PluginFactory &setRuntimeDir(const fs::path &runtimeDir);
  PluginFactory &addSearchDir(const fs::path &searchDir);

  RemapPluginInst *getRemapPlugin(const fs::path &configPath, int argc, char **argv, std::string &error, bool dynamicReloadEnabled);

  virtual const char *getUuid();
  void clean(std::string &error);

  void deactivate();
  void indicatePreReload();
  void indicatePostReload(bool reloadSuccessful);

protected:
  PluginDso *findByEffectivePath(const fs::path &path, bool dynamicReloadEnabled);
  fs::path getEffectivePath(const fs::path &configPath);

  std::vector<fs::path> _searchDirs; /** @brief ordered list of search paths where we look for plugins */
  fs::path _runtimeDir;              /** @brief the path where we would create a temporary copies of the plugins to load */

  PluginInstList _instList;

  ATSUuid *_uuid = nullptr;
  std::error_code _ec;
  bool _preventiveCleaning = true;

  static constexpr const char *const _tag = "plugin_factory"; /** @brief log tag used by this class */
};
