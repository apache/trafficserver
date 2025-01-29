/** @file

  Functionality allowing to load all plugins from a single config reload.

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

#include <unordered_map>

#include "proxy/http/remap/RemapPluginInfo.h"
#include "records/RecCore.h"
#include "proxy/http/remap/PluginFactory.h"
#ifdef PLUGIN_DSO_TESTS
#include "unit-tests/plugin_testing_common.h"
#else
#include "tscore/Diags.h"
#define PluginDbg   Dbg
#define PluginError Error
#endif
#include "../../../iocore/eventsystem/P_EventSystem.h"

#include <algorithm> /* std::swap */
#include <filesystem>

RemapPluginInst::RemapPluginInst(RemapPluginInfo &plugin) : _plugin(plugin)
{
  _plugin.acquire();
}

RemapPluginInst::~RemapPluginInst()
{
  _plugin.release();
}

RemapPluginInst *
RemapPluginInst::init(RemapPluginInfo *plugin, int argc, char **argv, std::string &error)
{
  RemapPluginInst *inst = new RemapPluginInst(*plugin);
  if (plugin->initInstance(argc, argv, &(inst->_instance), error)) {
    plugin->incInstanceCount();
    return inst;
  }
  delete inst;
  return nullptr;
}

void
RemapPluginInst::done()
{
  _plugin.decInstanceCount();
  _plugin.doneInstance(_instance);

  if (0 == _plugin.instanceCount()) {
    _plugin.done();
  }
}

TSRemapStatus
RemapPluginInst::doRemap(TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  return _plugin.doRemap(_instance, rh, rri);
}

void
RemapPluginInst::osResponse(TSHttpTxn rh, int os_response_type)
{
  _plugin.osResponse(_instance, rh, os_response_type);
}

PluginFactory::PluginFactory()
{
  _uuid = new ATSUuid();
  if (nullptr != _uuid) {
    _uuid->initialize(TS_UUID_V4);
    if (!_uuid->valid()) {
      /* Destroy and mark failure */
      delete _uuid;
      _uuid = nullptr;
    }
  }

  PluginDbg(_dbg_ctl(), "created plugin factory %s", getUuid());
}

PluginFactory::~PluginFactory()
{
  _instList.apply([](RemapPluginInst *pluginInst) -> void { delete pluginInst; });
  _instList.clear();

  if (!TSSystemState::is_event_system_shut_down()) {
    uint32_t elevate_access = 0;

    REC_ReadConfigInteger(elevate_access, "proxy.config.plugin.load_elevated");
    ElevateAccess access(elevate_access ? ElevateAccess::FILE_PRIVILEGE : 0);

    fs::remove_all(_runtimeDir, _ec);
  } else {
    fs::remove_all(_runtimeDir, _ec); // Try anyways
  }

  PluginDbg(_dbg_ctl(), "destroyed plugin factory %s", getUuid());
  delete _uuid;
}

PluginFactory &
PluginFactory::addSearchDir(const fs::path &searchDir)
{
  _searchDirs.push_back(searchDir);
  PluginDbg(_dbg_ctl(), "added plugin search dir %s", searchDir.c_str());
  return *this;
}

PluginFactory &
PluginFactory::setRuntimeDir(const fs::path &runtimeDir)
{
  _runtimeDir = runtimeDir / fs::path(getUuid());
  PluginDbg(_dbg_ctl(), "set plugin runtime dir %s", runtimeDir.c_str());
  return *this;
}

PluginFactory &
PluginFactory::setCompilerPath(const fs::path &compilerPath)
{
  _compilerPath = compilerPath;
  PluginDbg(_dbg_ctl(), "set plugin compiler path %s", compilerPath.c_str());
  return *this;
}

const char *
PluginFactory::getUuid()
{
  return _uuid ? _uuid->getString() : "unknown";
}

/**
 * @brief Loads, initializes and return a valid Remap Plugin instance.
 *
 * @param configPath plugin path as specified in the plugin
 * @param argc number of parameters passed to the plugin during instance initialization
 * @param argv parameters passed to the plugin during instance initialization
 * @param context Plugin context is used from continuations to guarantee correct reference counting against the plugin.
 * @param error human readable message if something goes wrong, empty otherwise
 * @return pointer to a plugin instance, nullptr if failure
 */
RemapPluginInst *
PluginFactory::getRemapPlugin(const fs::path &configPath, int argc, char **argv, std::string &error, bool dynamicReloadEnabled)
{
  /* Discover the effective path by looking into the search dirs */
  fs::path effectivePath = getEffectivePath(configPath);
  if (effectivePath.empty()) {
    error.assign("failed to find plugin '").append(configPath.string()).append("'");
    // The error will be reported by the caller but add debug log entry with this tag for convenience.
    PluginDbg(_dbg_ctl(), "%s", error.c_str());
    return nullptr;
  }

  // The plugin may have opt out by `TSPluginDSOReloadEnable`, let's check and overwrite
  if (dynamicReloadEnabled && PluginDso::loadedPlugins()->isPluginInDsoOptOutTable(effectivePath)) {
    // plugin not interested to be reload.
    PluginDbg(_dbg_ctl(), "Plugin %s not interested in taking part of the reload.", effectivePath.c_str());
    dynamicReloadEnabled = false;
  }

  /* Only one plugin with this effective path can be loaded by a plugin factory */
  RemapPluginInfo *plugin = dynamic_cast<RemapPluginInfo *>(findByEffectivePath(effectivePath, dynamicReloadEnabled));
  RemapPluginInst *inst   = nullptr;

  if (nullptr == plugin) {
    /* The plugin requested have not been loaded yet. */
    PluginDbg(_dbg_ctl(), "plugin '%s' has not been loaded yet, loading as remap plugin", configPath.c_str());

    fs::path runtimePath;

    // if dynamic reload enabled then create a temporary location to copy .so and load from there
    // else load from original location
    if (dynamicReloadEnabled) {
      runtimePath /= _runtimeDir;
      runtimePath /= effectivePath.relative_path();

      // Special case for Cripts
      if (!runtimePath.string().ends_with(".so")) {
        if (_compilerPath.empty()) {
          error.assign("compiler path not set for compiling plugins");
          return nullptr;
        }

        // Add .so to the source file, so e.g. example.cc.so. ToDo: libswoc doesn't allow appending to the extension.
        std::string newPath = runtimePath.string();

        newPath.append(".so");
        runtimePath = newPath;
      }

      fs::path parent = runtimePath.parent_path();
      PluginDbg(_dbg_ctl(), "Using effectivePath: [%s] runtimePath: [%s] parent: [%s]", effectivePath.c_str(), runtimePath.c_str(),
                parent.c_str());
      if (!fs::create_directories(parent, _ec)) {
        error.assign("failed to create plugin runtime dir");
        return nullptr;
      }
    } else {
      runtimePath = effectivePath;
      PluginDbg(_dbg_ctl(), "Using effectivePath: [%s] runtimePath: [%s]", effectivePath.c_str(), runtimePath.c_str());
    }

    plugin = new RemapPluginInfo(configPath, effectivePath, runtimePath);
    if (nullptr != plugin) {
      if (plugin->load(error, _compilerPath)) {
        if (plugin->init(error)) {
          PluginDso::loadedPlugins()->add(plugin);
          inst = RemapPluginInst::init(plugin, argc, argv, error);
          if (nullptr != inst) {
            /* Plugin loading and instance init went fine. */
            _instList.append(inst);
          }
        } else {
          /* Plugin DSO load succeeded but instance init failed. */
          PluginDbg(_dbg_ctl(), "plugin '%s' instance init failed", configPath.c_str());
          plugin->unload(error);
          delete plugin;
        }

      } else {
        /* Plugin DSO load failed. */
        PluginDbg(_dbg_ctl(), "plugin '%s' DSO load failed", configPath.c_str());
        delete plugin;
      }
    }
  } else {
    PluginDbg(_dbg_ctl(), "plugin '%s' has already been loaded", configPath.c_str());
    inst = RemapPluginInst::init(plugin, argc, argv, error);
    if (nullptr != inst) {
      _instList.append(inst);
    }
  }

  return inst;
}

/**
 * @brief full path to the first plugin found in the search path which will be used to be copied to runtime location and loaded.
 *
 * @param configPath path specified in the config file, it can be relative path.
 * @return full path to the plugin.
 */
fs::path
PluginFactory::getEffectivePath(const fs::path &configPath)
{
  if (configPath.is_absolute()) {
    if (fs::exists(configPath)) {
      return fs::canonical(configPath, _ec);
    } else {
      return fs::path();
    }
  }

  fs::path path;

  for (auto const &dir : _searchDirs) {
    fs::path candidatePath = dir / configPath;
    if (fs::exists(candidatePath)) {
      path = fs::canonical(candidatePath, _ec);
      break;
    }
  }

  return path;
}

void
PluginFactory::cleanup()
{
  std::error_code ec;
  std::string     path(RecConfigReadRuntimeDir());

  try {
    if (path.starts_with("/") && std::filesystem::is_directory(path)) {
      for (const auto &entry : std::filesystem::directory_iterator(path, ec)) {
        if (entry.is_directory()) {
          std::string dir_name   = entry.path().string();
          int         dash_count = std::count(dir_name.begin(), dir_name.end(), '-'); // All UUIDs have 4 dashes

          if (dash_count == 4) {
            std::filesystem::remove_all(dir_name, ec);
          }
        }
      }
    }
  } catch (std::exception &e) {
    PluginError("Error cleaning up runtime directory: %s", e.what());
  }
}

/**
 * @brief Find a plugin by path from our linked plugin list by using plugin effective (canonical) path
 *
 * @param path effective (caninical) path
 * @return plugin found or nullptr if not found
 */
PluginDso *
PluginFactory::findByEffectivePath(const fs::path &path, bool dynamicReloadEnabled)
{
  return PluginDso::loadedPlugins()->findByEffectivePath(path, dynamicReloadEnabled);
}

/**
 * @brief Tell all plugins instantiated by this factory that the configuration
 * they are using is no longer the active one.
 *
 * This method would be useful only in case configs are reloaded independently from
 * factory/plugins instantiation and initialization.
 */
void
PluginFactory::deactivate()
{
  PluginDbg(_dbg_ctl(), "deactivate configuration used by factory '%s'", getUuid());

  _instList.apply([](RemapPluginInst &pluginInst) -> void { pluginInst.done(); });
}

/**
 * @brief Tell all plugins (that so wish) that remap.config is going to be reloaded
 */
void
PluginFactory::indicatePreReload()
{
  PluginDso::loadedPlugins()->indicatePreReload(getUuid());
}

/**
 * @brief Tell all plugins (that so wish) that remap.config is done reloading
 */
void
PluginFactory::indicatePostReload(bool reloadSuccessful)
{
  /* Find out which plugins (DSO) are actually instantiated by this factory */
  std::unordered_map<PluginDso *, int> pluginUsed;
  for (auto &inst : _instList) {
    pluginUsed[&(inst._plugin)]++;
  }

  PluginDso::loadedPlugins()->indicatePostReload(reloadSuccessful, pluginUsed, getUuid());
}
