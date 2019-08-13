/** @file

  Unit tests for a class that deals with plugin Dynamic Shared Objects (DSO)

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

#define CATCH_CONFIG_MAIN /* include main function */
#include <catch.hpp>      /* catch unit-test framework */
#include <fstream>        /* ofstream */
#include <utime.h>

#include "plugin_testing_common.h"
#include "../PluginFactory.h"
#include "../PluginDso.h"

thread_local PluginThreadContext *pluginThreadContext;

std::error_code ec;
static void *INSTANCE_HANDLER = (void *)789;

/* Mock of PluginFactory just to get consisten UUID to be able to test consistently */
static fs::path tempComponent = fs::path("c71e2bab-90dc-4770-9535-c9304c3de38e");
class PluginFactoryUnitTest : public PluginFactory
{
public:
  PluginFactoryUnitTest(const fs::path &tempComponent)
  {
    _tempComponent      = tempComponent;
    _preventiveCleaning = false;
  }

protected:
  const char *
  getUuid()
  {
    return _tempComponent.c_str();
  }

  fs::path _tempComponent;
};

PluginDebugObject *
getDebugObject(const PluginDso &plugin)
{
  std::string error; /* ignore the error, return nullptr if symbol not defined */
  void *address = nullptr;
  plugin.getSymbol("getPluginDebugObjectTest", address, error);
  GetPluginDebugObjectFunction *getObject = reinterpret_cast<GetPluginDebugObjectFunction *>(address);
  if (getObject) {
    PluginDebugObject *object = reinterpret_cast<PluginDebugObject *>(getObject());
    return object;
  } else {
    return nullptr;
  }
}

/* The following are paths that are used commonly in the unit-tests */
static fs::path sandboxDir     = getTemporaryDir();
static fs::path runtimeRootDir = sandboxDir / "runtime";
static fs::path runtimeDir     = runtimeRootDir / tempComponent;
static fs::path searchDir      = sandboxDir / "search";
static fs::path pluginBuildDir = fs::current_path() / "unit-tests/.libs";

void
clean()
{
  fs::remove(sandboxDir, ec);
}

static void
setupConfigPathTest(const fs::path &configPath, const fs::path &pluginBuildPath, const fs::path &uuid, fs::path &effectivePath,
                    fs::path &runtimePath, time_t mtime = 0, bool append = false)
{
  std::string error;
  if (!append) {
    clean();
  }

  effectivePath = configPath.is_absolute() ? configPath : searchDir / configPath;
  runtimePath   = runtimeRootDir / uuid / effectivePath.relative_path();

  /* Create the directory structure and install plugins */
  fs::create_directories(effectivePath.parent_path(), ec);
  fs::copy(pluginBuildPath, effectivePath, ec);
  if (0 != mtime) {
    struct stat sb;
    struct utimbuf new_times;
    stat(effectivePath.c_str(), &sb);
    new_times.actime  = sb.st_atime; /* keep atime unchanged */
    new_times.modtime = mtime;       /* set mtime to current time */
    utime(effectivePath.c_str(), &new_times);
  }

  CHECK(fs::exists(effectivePath));
}

static PluginFactoryUnitTest *
getFactory(const fs::path &uuid)
{
  /* Instantiate and initialize a plugin factory. */
  PluginFactoryUnitTest *factory = new PluginFactoryUnitTest(uuid);
  factory->setRuntimeDir(runtimeRootDir);
  factory->addSearchDir(searchDir);
  return factory;
}

static void
teardownConfigPathTest(PluginFactoryUnitTest *factory)
{
  delete factory;
  clean();
}

static void
validateSuccessfulConfigPathTest(const RemapPluginInst *pluginInst, const std::string &error, const fs::path &effectivePath,
                                 const fs::path &runtimePath)
{
  CHECK(nullptr != pluginInst);
  CHECK("" == error);
  CHECK(effectivePath == pluginInst->_plugin.effectivePath());
  CHECK(runtimePath == pluginInst->_plugin.runtimePath());
}

SCENARIO("loading plugins", "[plugin][core]")
{
  REQUIRE_FALSE(sandboxDir.empty());

  fs::path effectivePath;
  fs::path runtimePath;
  std::string error;

  GIVEN("an existing plugin")
  {
    fs::path pluginName = fs::path("plugin_v1.so");
    fs::path buildPath  = pluginBuildDir / pluginName;

    WHEN("config using plugin file name only")
    {
      fs::path configPath = pluginName;
      CHECK(configPath.is_relative()); /* make sure this is relative path - this is what we are testing */

      setupConfigPathTest(configPath, buildPath, tempComponent, effectivePath, runtimePath);
      PluginFactoryUnitTest *factory = getFactory(tempComponent);
      RemapPluginInst *plugin        = factory->getRemapPlugin(configPath, 0, nullptr, error);

      THEN("expect it to successfully load") { validateSuccessfulConfigPathTest(plugin, error, effectivePath, runtimePath); }

      teardownConfigPathTest(factory);
    }

    WHEN("config is using plugin relative filename")
    {
      fs::path configPath = fs::path("subdir") / pluginName;
      CHECK(configPath.is_relative()); /* make sure this is relative path - this is what we are testing */

      setupConfigPathTest(configPath, buildPath, tempComponent, effectivePath, runtimePath);
      PluginFactoryUnitTest *factory = getFactory(tempComponent);
      RemapPluginInst *plugin        = factory->getRemapPlugin(configPath, 0, nullptr, error);

      THEN("expect it to successfully load") { validateSuccessfulConfigPathTest(plugin, error, effectivePath, runtimePath); }

      teardownConfigPathTest(factory);
    }

    WHEN("config is using plugin absolute path")
    {
      fs::path configPath = searchDir / "subdir" / pluginName;
      CHECK(configPath.is_absolute()); /* make sure this is absolute path - this is what we are testing */

      setupConfigPathTest(configPath, buildPath, tempComponent, effectivePath, runtimePath);
      PluginFactoryUnitTest *factory = getFactory(tempComponent);
      RemapPluginInst *plugin        = factory->getRemapPlugin(configPath, 0, nullptr, error);

      THEN("expect it to successfully load") { validateSuccessfulConfigPathTest(plugin, error, effectivePath, runtimePath); }

      teardownConfigPathTest(factory);
    }

    WHEN("config using nonexisting relative plugin file name")
    {
      fs::path relativeExistingPath = pluginName;
      CHECK(relativeExistingPath.is_relative());
      fs::path relativeNonexistingPath("subdir");
      relativeNonexistingPath /= fs::path("nonexisting_plugin.so");
      CHECK(relativeNonexistingPath.is_relative());

      setupConfigPathTest(relativeExistingPath, buildPath, tempComponent, effectivePath, runtimePath);
      PluginFactoryUnitTest *factory = getFactory(tempComponent);
      RemapPluginInst *plugin        = factory->getRemapPlugin(relativeNonexistingPath, 0, nullptr, error);

      THEN("expect it to fail with appropriate error message")
      {
        std::string expectedError;
        expectedError.append("failed to find plugin '").append(relativeNonexistingPath.string()).append("'");
        CHECK(nullptr == plugin);
        CHECK(expectedError == error);
      }

      teardownConfigPathTest(factory);
    }

    WHEN("config using nonexisting absolute plugin file name")
    {
      fs::path relativeExistingPath = pluginName;
      CHECK(relativeExistingPath.is_relative());
      fs::path absoluteNonexistingPath = searchDir / "subdir" / "nonexisting_plugin.so";
      CHECK(absoluteNonexistingPath.is_absolute());

      setupConfigPathTest(relativeExistingPath, buildPath, tempComponent, effectivePath, runtimePath);
      PluginFactoryUnitTest *factory = getFactory(tempComponent);
      RemapPluginInst *plugin        = factory->getRemapPlugin(absoluteNonexistingPath, 0, nullptr, error);

      THEN("expect it to fail with appropriate error message")
      {
        std::string expectedError;
        expectedError.append("failed to find plugin '").append(absoluteNonexistingPath.string()).append("'");
        CHECK(nullptr == plugin);
        CHECK(expectedError == error);
      }

      teardownConfigPathTest(factory);
    }
  }
}

SCENARIO("multiple search dirs + multiple or no plugins installed", "[plugin][core]")
{
  REQUIRE_FALSE(sandboxDir.empty());

  GIVEN("multiple search dirs specified for the plugin search")
  {
    /* Create the directory structure and install plugins */
    fs::path configPath              = fs::path("plugin_v1.so");
    fs::path pluginName              = fs::path("plugin_v1.so");
    fs::path searchDir1              = sandboxDir / "search1";
    fs::path searchDir2              = sandboxDir / "search2";
    fs::path searchDir3              = sandboxDir / "search3";
    std::vector<fs::path> searchDirs = {searchDir1, searchDir2, searchDir3};
    fs::path effectivePath1          = searchDir1 / configPath;
    fs::path effectivePath2          = searchDir2 / configPath;
    fs::path effectivePath3          = searchDir3 / configPath;
    fs::path runtimePath1            = runtimeDir / effectivePath1.relative_path();
    fs::path runtimePath2            = runtimeDir / effectivePath2.relative_path();
    fs::path runtimePath3            = runtimeDir / effectivePath3.relative_path();
    fs::path pluginBuildPath         = fs::current_path() / fs::path("unit-tests/.libs") / pluginName;

    std::string error;

    for (auto searchDir : searchDirs) {
      CHECK(fs::create_directories(searchDir, ec));
      fs::copy(pluginBuildPath, searchDir, ec);
    }
    CHECK(fs::create_directories(runtimeDir, ec));

    /* Instantiate and initialize a plugin DSO instance. */
    PluginFactoryUnitTest factory(tempComponent);
    factory.setRuntimeDir(runtimeRootDir);
    for (auto searchDir : searchDirs) {
      factory.addSearchDir(searchDir);
    }

    CHECK(fs::exists(effectivePath1));
    CHECK(fs::exists(effectivePath2));
    CHECK(fs::exists(effectivePath3));

    WHEN("loading an existing plugin using its absolute path but the plugin is not located in any of the search dirs")
    {
      /* Prepare "unregistered" directory containing a valid plugin but not registered with the factory as a search directory */
      fs::path unregisteredDir = sandboxDir / searchDir / "unregistered";
      CHECK(fs::create_directories(unregisteredDir, ec));
      fs::copy(pluginBuildPath, unregisteredDir, ec);
      fs::path abEffectivePath = unregisteredDir / pluginName;
      fs::path absRuntimePath  = runtimeDir / abEffectivePath.relative_path();
      CHECK(abEffectivePath.is_absolute());
      CHECK(fs::exists(abEffectivePath));

      /* Now use an absolute path containing the unregistered search directory */
      RemapPluginInst *pluginInst = factory.getRemapPlugin(abEffectivePath, 0, nullptr, error);

      THEN("Expect it to successfully load")
      {
        CHECK(nullptr != pluginInst);
        CHECK(error.empty());
        CHECK(abEffectivePath == pluginInst->_plugin.effectivePath());
        CHECK(absRuntimePath == pluginInst->_plugin.runtimePath());
      }
      CHECK(fs::remove(sandboxDir, ec));
    }

    WHEN("a valid plugin is found in the first search path")
    {
      RemapPluginInst *pluginInst = factory.getRemapPlugin(configPath, 0, nullptr, error);

      THEN("Expect it to successfully load the one found in the first search dir and copy it in the runtime dir")
      {
        CHECK(nullptr != pluginInst);
        CHECK(error.empty());
        CHECK(effectivePath1 == pluginInst->_plugin.effectivePath());
        CHECK(runtimePath1 == pluginInst->_plugin.runtimePath());
      }
      CHECK(fs::remove(sandboxDir, ec));
    }

    WHEN("the first search dir is missing the plugin but the second search has it")
    {
      CHECK(fs::remove(effectivePath1, ec));
      RemapPluginInst *pluginInst = factory.getRemapPlugin(configPath, 0, nullptr, error);

      THEN("Expect it to successfully load the one found in the second search dir")
      {
        CHECK(nullptr != pluginInst);
        CHECK(error.empty());
        CHECK(effectivePath2 == pluginInst->_plugin.effectivePath());
        CHECK(runtimePath2 == pluginInst->_plugin.runtimePath());
      }
      CHECK(fs::remove(sandboxDir, ec));
    }

    WHEN("the first and second search dir are missing the plugin but the third search has it")
    {
      CHECK(fs::remove(effectivePath1, ec));
      CHECK(fs::remove(effectivePath2, ec));
      RemapPluginInst *pluginInst = factory.getRemapPlugin(configPath, 0, nullptr, error);

      THEN("Expect it to successfully load the one found in the third search dir")
      {
        CHECK(nullptr != pluginInst);
        CHECK(error.empty());
        CHECK(effectivePath3 == pluginInst->_plugin.effectivePath());
        CHECK(runtimePath3 == pluginInst->_plugin.runtimePath());
      }
      CHECK(fs::remove(sandboxDir, ec));
    }

    WHEN("none of the search dirs contains a valid plugin")
    {
      CHECK(fs::remove(effectivePath1, ec));
      CHECK(fs::remove(effectivePath2, ec));
      CHECK(fs::remove(effectivePath3, ec));

      THEN("expect the plugin load to fail.")
      {
        RemapPluginInst *pluginInst = factory.getRemapPlugin(configPath, 0, nullptr, error);
        CHECK(nullptr == pluginInst);
        CHECK(std::string("failed to find plugin '").append(configPath.string()).append("'") == error);
        CHECK_FALSE(fs::exists(runtimePath1));
        CHECK_FALSE(fs::exists(runtimePath2));
        CHECK_FALSE(fs::exists(runtimePath3));
      }
      CHECK(fs::remove(sandboxDir, ec));
    }
  }
}

static int
getPluginVersion(const PluginDso &plugin)
{
  std::string error;
  void *s = nullptr;
  CHECK(plugin.getSymbol("pluginDsoVersionTest", s, error));
  int (*version)() = reinterpret_cast<int (*)()>(s);
  return version ? version() : -1;
}

SCENARIO("loading multiple version of the same plugin at the same time", "[plugin][core]")
{
  REQUIRE_FALSE(sandboxDir.empty());

  static fs::path uuid_t1 = fs::path("c71e2bab-90dc-4770-9535-c9304c3de381"); /* UUID at moment t1 */
  static fs::path uuid_t2 = fs::path("c71e2bab-90dc-4770-9535-e7304c3ee732"); /* UUID at moment t2 */

  fs::path effectivePath_v1;            /* expected effective path for DSO v1 */
  fs::path effectivePath_v2;            /* expected effective path for DSO v2 */
  fs::path runtimePath_v1;              /* expected runtime path for DSO v1 */
  fs::path runtimePath_v2;              /* expected runtime path for DSO v2 */
  void *tsRemapInitSym_v1_t1 = nullptr; /* callback address from DSO v1 at moment t1 */
  void *tsRemapInitSym_v1_t2 = nullptr; /* callback address from DSO v1 at moment t2 */
  void *tsRemapInitSym_v2_t2 = nullptr; /* callback address from DSO v2 at moment t2 */

  std::string error;
  std::string error1;
  std::string error2;

  fs::path configName   = fs::path("plugin.so");                     /* use same config name for all following tests */
  fs::path buildPath_v1 = pluginBuildDir / fs::path("plugin_v1.so"); /* DSO v1 */
  fs::path buildPath_v2 = pluginBuildDir / fs::path("plugin_v2.so"); /* DSO v1 */

  GIVEN("two different versions v1 and v2 of same plugin")
  {
    WHEN("(1) loading v1, (2) overwriting with v2 and then (3) reloading by using the same plugin name, "
         "(*) v1 and v2 DSOs modification time are different (changed)")
    {
      /* Simulate installing plugin plugin_v1.so (ver 1) as plugin.so and loading it at some point of time t1 */
      setupConfigPathTest(configName, buildPath_v1, uuid_t1, effectivePath_v1, runtimePath_v1, 1556825556);
      PluginFactoryUnitTest *factory1 = getFactory(uuid_t1);
      RemapPluginInst *plugin_v1      = factory1->getRemapPlugin(configName, 0, nullptr, error1);
      plugin_v1->_plugin.getSymbol("TSRemapInit", tsRemapInitSym_v1_t1, error);

      /* Simulate installing plugin plugin_v2.so (v1) as plugin.so and loading it at some point of time t2 */
      /* Note that during the installation plugin_v2.so (v2) is "barberically" overriding the existing plugin.so which was v1 */
      setupConfigPathTest(configName, buildPath_v2, uuid_t2, effectivePath_v2, runtimePath_v2, 1556825557);
      PluginFactoryUnitTest *factory2 = getFactory(uuid_t2);
      RemapPluginInst *plugin_v2      = factory2->getRemapPlugin(configName, 0, nullptr, error2);

      /* Make sure plugin.so was overriden */
      CHECK(effectivePath_v1 == effectivePath_v2);

      /* Although effective path is the same runtime paths should be different */
      CHECK(runtimePath_v1 != runtimePath_v2);

      THEN("expect both to be successfully loaded and used simultaneously")
      {
        /* Both loadings should succeed */
        validateSuccessfulConfigPathTest(plugin_v1, error1, effectivePath_v1, runtimePath_v1);
        validateSuccessfulConfigPathTest(plugin_v2, error2, effectivePath_v2, runtimePath_v2);

        /* Make sure what we installed and loaded first was v1 and after the plugin reload we run v2 */
        CHECK(1 == getPluginVersion(plugin_v1->_plugin));
        CHECK(2 == getPluginVersion(plugin_v2->_plugin));

        /* Make sure the symbols we get from the 2 loaded plugins don't yield the same callback function pointer */
        plugin_v1->_plugin.getSymbol("TSRemapInit", tsRemapInitSym_v1_t2, error);
        plugin_v2->_plugin.getSymbol("TSRemapInit", tsRemapInitSym_v2_t2, error);
        CHECK(nullptr != tsRemapInitSym_v1_t2);
        CHECK(nullptr != tsRemapInitSym_v2_t2);
        CHECK(tsRemapInitSym_v1_t2 != tsRemapInitSym_v2_t2);

        /* Make sure v1 callback functions addresses did not change for v1 after v2 was loaded */
        CHECK(tsRemapInitSym_v1_t1 == tsRemapInitSym_v1_t2);
      }

      teardownConfigPathTest(factory1);
      teardownConfigPathTest(factory2);
    }
  }

  GIVEN("two different versions v1 and v2 of same plugin")
  {
    WHEN("(1) loading v1, (2) overwriting with v2 and then (3) reloading by using the same plugin name, "
         "(*) v1 and v2 DSOs modification time are same (did NOT change)")
    {
      /* Simulate installing plugin plugin_v1.so (ver 1) as plugin.so and loading it at some point of time t1 */
      setupConfigPathTest(configName, buildPath_v1, uuid_t1, effectivePath_v1, runtimePath_v1, 1556825556);
      PluginFactoryUnitTest *factory1 = getFactory(uuid_t1);
      RemapPluginInst *plugin_v1      = factory1->getRemapPlugin(configName, 0, nullptr, error1);

      /* Simulate installing plugin plugin_v2.so (v1) as plugin.so and loading it at some point of time t2 */
      /* Note that during the installation plugin_v2.so (v2) is "barberically" overriding the existing plugin.so
         which was v1, since the modification time is exactly the same the new v2 plugin would not be loaded and
         we should get the same PluginDso address and same effective and runtime paths */
      setupConfigPathTest(configName, buildPath_v2, uuid_t2, effectivePath_v2, runtimePath_v2, 1556825556);
      PluginFactoryUnitTest *factory2 = getFactory(uuid_t2);
      RemapPluginInst *plugin_v2      = factory2->getRemapPlugin(configName, 0, nullptr, error2);

      /* Make sure plugin.so was overriden */
      CHECK(effectivePath_v1 == effectivePath_v2);

      THEN("expect only v1 plugin to be loaded since the timestamp has not changed")
      {
        /* Both getRemapPlugin() calls should succeed but only v1 plugin DSO should be used */
        validateSuccessfulConfigPathTest(plugin_v1, error1, effectivePath_v1, runtimePath_v1);
        validateSuccessfulConfigPathTest(plugin_v2, error2, effectivePath_v2, runtimePath_v1);

        /* Make sure we ended up with the same DSO object and runtime paths should be same - no new plugin was loaded */
        CHECK(&(plugin_v1->_plugin) == &(plugin_v2->_plugin));
        CHECK(plugin_v1->_plugin.runtimePath() == plugin_v2->_plugin.runtimePath());

        /* Make sure v2 DSO was NOT loaded both instances should return same v1 version */
        CHECK(1 == getPluginVersion(plugin_v1->_plugin));
        CHECK(1 == getPluginVersion(plugin_v2->_plugin));

        /* Make sure the symbols we get from the 2 loaded plugins yield the same callback function pointer */
        plugin_v1->_plugin.getSymbol("TSRemapInit", tsRemapInitSym_v1_t2, error);
        plugin_v2->_plugin.getSymbol("TSRemapInit", tsRemapInitSym_v2_t2, error);
        CHECK(nullptr != tsRemapInitSym_v1_t2);
        CHECK(nullptr != tsRemapInitSym_v2_t2);
        CHECK(tsRemapInitSym_v1_t2 == tsRemapInitSym_v2_t2);
      }

      teardownConfigPathTest(factory1);
      teardownConfigPathTest(factory2);
    }
  }

  /* Since factories share the list of loaded plugins to avoid unnecessary loading of unchanged plugins
   * lets check if destroying a factory impacts plugins loaded from another factory */
  GIVEN("configurations with and without plugins")
  {
    WHEN("loading a configuration without plugins and then reloading configuration with a plugin")
    {
      /* Simulate configuration without plugins - an unused factory */
      PluginFactoryUnitTest *factory1 = getFactory(uuid_t1);

      /* Now provision and load a plugin using a second factory */
      setupConfigPathTest(configName, buildPath_v2, uuid_t2, effectivePath_v2, runtimePath_v2, 1556825556);
      PluginFactoryUnitTest *factory2 = getFactory(uuid_t2);
      RemapPluginInst *plugin_v2      = factory2->getRemapPlugin(configName, 0, nullptr, error2);

      THEN("the plugin from the second factory to work")
      {
        validateSuccessfulConfigPathTest(plugin_v2, error2, effectivePath_v2, runtimePath_v2);

        /* Now delete the first factory and call a plugin from the second factory */
        delete factory1;
        CHECK(TSREMAP_NO_REMAP == plugin_v2->_plugin.doRemap(INSTANCE_HANDLER, nullptr, nullptr));
      }

      teardownConfigPathTest(factory2);
    }
  }
}

SCENARIO("notifying plugins of config reload", "[plugin][core]")
{
  REQUIRE_FALSE(sandboxDir.empty());

  /* use 2 copies of the same plugin to test */
  fs::path configName1 = fs::path("plugin_testing_calls_1.so");
  fs::path configName2 = fs::path("plugin_testing_calls_2.so");
  fs::path buildPath   = pluginBuildDir / fs::path("plugin_testing_calls.so");

  static fs::path uuid_t1 = fs::path("c71e2bab-90dc-4770-9535-c9304c3de381"); /* UUID at moment t1 */
  static fs::path uuid_t2 = fs::path("c71e2bab-90dc-4770-9535-e7304c3ee732"); /* UUID at moment t2 */

  fs::path effectivePath1;
  fs::path effectivePath2;
  fs::path runtimePath1;
  fs::path runtimePath2;

  std::string error;

  GIVEN("simple configuration with 1 plugin and 1 factory")
  {
    WHEN("indicating config reload")
    {
      /* Simulate configuration without plugins - an unused factory */
      setupConfigPathTest(configName1, buildPath, uuid_t1, effectivePath1, runtimePath1, 1556825556);
      PluginFactoryUnitTest *factory1 = getFactory(uuid_t1);
      RemapPluginInst *plugin1        = factory1->getRemapPlugin(configName1, 0, nullptr, error);

      /* check if loaded successfully */
      validateSuccessfulConfigPathTest(plugin1, error, effectivePath1, runtimePath1);

      /* Prapare the debug object */
      PluginDebugObject *debugObject = getDebugObject(plugin1->_plugin);
      debugObject->clear();

      THEN("expect 'done' methods to be called for plugin and the instance but not the 'reload config' methods")
      {
        /* Simulate reloading the config */
        factory1->indicateReload();

        /* was "done" method called? */
        CHECK(1 == debugObject->doneCalled);
        CHECK(1 == debugObject->deleteInstanceCalled);
        CHECK(0 == debugObject->reloadConfigCalled);
      }

      teardownConfigPathTest(factory1);
    }
  }

  GIVEN("configuration with 2 plugins loaded by 1 factory")
  {
    WHEN("indicating config reload")
    {
      /* Simulate configuration without plugins - an unused factory */
      setupConfigPathTest(configName1, buildPath, uuid_t1, effectivePath1, runtimePath1, 1556825556);
      setupConfigPathTest(configName2, buildPath, uuid_t1, effectivePath2, runtimePath2, 1556825556, /* append */ true);
      PluginFactoryUnitTest *factory1 = getFactory(uuid_t1);
      RemapPluginInst *plugin1        = factory1->getRemapPlugin(configName1, 0, nullptr, error);
      RemapPluginInst *plugin2        = factory1->getRemapPlugin(configName2, 0, nullptr, error);

      /* check if loaded successfully */
      validateSuccessfulConfigPathTest(plugin1, error, effectivePath1, runtimePath1);
      validateSuccessfulConfigPathTest(plugin2, error, effectivePath2, runtimePath2);

      /* Prapare the debug objects */
      PluginDebugObject *debugObject1 = getDebugObject(plugin1->_plugin);
      PluginDebugObject *debugObject2 = getDebugObject(plugin2->_plugin);
      debugObject1->clear();
      debugObject2->clear();

      THEN("expect 'done' methods to be called but not the 'reload config' methods")
      {
        /* Simulate reloading the config */
        factory1->indicateReload();

        /* Was "done" method called? */
        CHECK(1 == debugObject1->doneCalled);
        CHECK(1 == debugObject1->deleteInstanceCalled);
        CHECK(0 == debugObject1->reloadConfigCalled);
        CHECK(1 == debugObject2->doneCalled);
        CHECK(1 == debugObject2->deleteInstanceCalled);
        CHECK(0 == debugObject2->reloadConfigCalled);
      }

      teardownConfigPathTest(factory1);
    }
  }

  GIVEN("configuration with 1 plugin loaded by 2 separate factories")
  {
    WHEN("indicating config reload")
    {
      /* Simulate configuration without plugins - an unused factory */
      setupConfigPathTest(configName1, buildPath, uuid_t1, effectivePath1, runtimePath1, 1556825556);
      PluginFactoryUnitTest *factory1 = getFactory(uuid_t1);
      PluginFactoryUnitTest *factory2 = getFactory(uuid_t2);
      RemapPluginInst *plugin1        = factory1->getRemapPlugin(configName1, 0, nullptr, error);
      RemapPluginInst *plugin2        = factory2->getRemapPlugin(configName1, 0, nullptr, error);

      /* Prapare the debug objects */
      PluginDebugObject *debugObject1 = getDebugObject(plugin1->_plugin);
      PluginDebugObject *debugObject2 = getDebugObject(plugin2->_plugin);

      THEN("expect instance 'done' to be always called, but plugin 'done' called only after destroying one factory")
      {
        debugObject2->clear();
        factory2->indicateReload();
        CHECK(0 == debugObject2->doneCalled);
        CHECK(1 == debugObject2->deleteInstanceCalled);
        CHECK(1 == debugObject2->reloadConfigCalled);

        delete factory2;

        debugObject1->clear();
        factory1->indicateReload();
        CHECK(1 == debugObject1->doneCalled);
        CHECK(1 == debugObject1->deleteInstanceCalled);
        CHECK(0 == debugObject1->reloadConfigCalled);

        delete factory1;
      }

      clean();
    }
  }
}
