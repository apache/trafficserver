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

#include "RemapPluginInfo.h"
#include "PluginFactory.h"
#ifdef PLUGIN_DSO_TESTS
#include "unit-tests/plugin_testing_common.h"
#else
#include "tscore/Diags.h"
#endif

#include <algorithm> /* std::swap */

RemapPluginInst::RemapPluginInst(RemapPluginInfo &plugin) : _plugin(plugin)
{
  _plugin.acquire();
  _plugin.incInstanceCount();
}

RemapPluginInst::~RemapPluginInst()
{
  _plugin.decInstanceCount();
  _plugin.release();
}

bool
RemapPluginInst::init(int argc, char **argv, std::string &error)
{
  bool result = false;
  result      = _plugin.initInstance(argc, argv, &_instance, error);

  return result;
}

void
RemapPluginInst::done()
{
  _plugin.doneInstance(_instance);
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

  Debug(_tag, "created plugin factory %s", getUuid());
}

PluginFactory::~PluginFactory()
{
  _instList.apply([](RemapPluginInst *pluginInst) -> void { delete pluginInst; });
  _instList.clear();

  fs::remove(_runtimeDir, _ec);

  Debug(_tag, "destroyed plugin factory %s", getUuid());
  delete _uuid;
}

PluginFactory &
PluginFactory::addSearchDir(const fs::path &searchDir)
{
  _searchDirs.push_back(searchDir);
  Debug(_tag, "added plugin search dir %s", searchDir.c_str());
  return *this;
}

PluginFactory &
PluginFactory::setRuntimeDir(const fs::path &runtimeDir)
{
  _runtimeDir = runtimeDir / fs::path(getUuid());
  Debug(_tag, "set plugin runtime dir %s", runtimeDir.c_str());
  return *this;
}

const char *
PluginFactory::getUuid()
{
  return _uuid ? _uuid->getString() : "uknown";
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
PluginFactory::getRemapPlugin(const fs::path &configPath, int argc, char **argv, std::string &error)
{
  /* Discover the effective path by looking into the search dirs */
  fs::path effectivePath = getEffectivePath(configPath);
  if (effectivePath.empty()) {
    error.assign("failed to find plugin '").append(configPath.string()).append("'");
    return nullptr;
  }

  /* Only one plugin with this effective path can be loaded by a plugin factory */
  RemapPluginInfo *plugin = dynamic_cast<RemapPluginInfo *>(findByEffectivePath(effectivePath));
  RemapPluginInst *inst   = nullptr;

  if (nullptr == plugin) {
    /* The plugin requested have not been loaded yet. */
    Debug(_tag, "plugin '%s' has not been loaded yet, loading as remap plugin", configPath.c_str());

    fs::path runtimePath;
    runtimePath /= _runtimeDir;
    runtimePath /= effectivePath.relative_path();

    fs::path parent = runtimePath.parent_path();
    if (!fs::create_directories(parent, _ec)) {
      error.assign("failed to create plugin runtime dir");
      return nullptr;
    }

    plugin = new RemapPluginInfo(configPath, effectivePath, runtimePath);
    if (nullptr != plugin) {
      if (plugin->load(error)) {
        _list.append(plugin);

        if (plugin->init(error)) {
          inst = new RemapPluginInst(*plugin);
          inst->init(argc, argv, error);
          _instList.append(inst);
        }

        if (_preventiveCleaning) {
          clean(error);
        }
      } else {
        return nullptr;
      }
    }
  } else {
    Debug(_tag, "plugin '%s' has already been loaded", configPath.c_str());
    inst = new RemapPluginInst(*plugin);
    inst->init(argc, argv, error);
    _instList.append(inst);
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
      return fs::canonical(configPath.string(), _ec);
    } else {
      return fs::path();
    }
  }

  fs::path path;

  for (auto dir : _searchDirs) {
    fs::path candidatePath = dir / configPath;
    if (fs::exists(candidatePath)) {
      path = fs::canonical(candidatePath, _ec);
      break;
    }
  }

  return path;
}

/**
 * @brief Find a plugin by path from our linked plugin list by using plugin effective (canonical) path
 *
 * @param path effective (caninical) path
 * @return plugin found or nullptr if not found
 */
PluginDso *
PluginFactory::findByEffectivePath(const fs::path &path)
{
  struct stat sb;
  time_t mtime = 0;
  if (0 == stat(path.c_str(), &sb)) {
    mtime = sb.st_mtime;
  }
  auto spot = std::find_if(_list.begin(), _list.end(), [&](PluginDso const &plugin) -> bool {
    return (0 == path.string().compare(plugin.effectivePath().string()) && (mtime == plugin.modTime()));
  });
  return spot == _list.end() ? nullptr : static_cast<PluginDso *>(spot);
}

/**
 * @brief Tell all plugins (that so wish) that remap.config is being reloaded
 *
 * This method would be useful only in case configs are reloaded independently from
 * factory/plugins instantiation and initialization.
 */
void
PluginFactory::indicateReload()
{
  Debug(_tag, "indicated config reload to factory '%s'", getUuid());

  _instList.apply([](RemapPluginInst &pluginInst) -> void { pluginInst.done(); });

  _list.apply([](PluginDso &plugin) -> void {
    if (1 == plugin.instanceCount()) {
      plugin.done();
    } else {
      plugin.indicateReload();
    }
  });
}

void
PluginFactory::clean(std::string &error)
{
  fs::remove(_runtimeDir, _ec);
}
