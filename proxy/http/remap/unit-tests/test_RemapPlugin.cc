/** @file

  Unit tests for a class that deals with remap plugins

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
#include <string>

#include "plugin_testing_common.h"
#include "../RemapPluginInfo.h"

thread_local PluginThreadContext *pluginThreadContext;

static void *INSTANCE_HANDLER = (void *)789;
std::error_code ec;

/* The following are paths that are used commonly in the unit-tests */
static fs::path sandboxDir     = getTemporaryDir();
static fs::path runtimeDir     = sandboxDir / "runtime";
static fs::path searchDir      = sandboxDir / "search";
static fs::path pluginBuildDir = fs::current_path() / "unit-tests/.libs";

void
clean()
{
  fs::remove(sandboxDir, ec);
}

/* Mock used only to make unit testing convenient to check if callbacks are really called and check errors */
class RemapPluginUnitTest : public RemapPluginInfo
{
public:
  RemapPluginUnitTest(const fs::path &configPath, const fs::path &effectivePath, const fs::path &runtimePath)
    : RemapPluginInfo(configPath, effectivePath, runtimePath)
  {
  }
  std::string
  getError(const char *required, const char *requiring = nullptr)
  {
    return missingRequiredSymbolError(_configPath.string(), required, requiring);
  }

  PluginDebugObject *
  getDebugObject()
  {
    std::string error; /* ignore the error, return nullptr if symbol not defined */
    void *address = nullptr;
    getSymbol("getPluginDebugObjectTest", address, error);
    GetPluginDebugObjectFunction *getObject = reinterpret_cast<GetPluginDebugObjectFunction *>(address);
    if (getObject) {
      PluginDebugObject *object = reinterpret_cast<PluginDebugObject *>(getObject());
      return object;
    } else {
      return nullptr;
    }
  }
};

RemapPluginUnitTest *
setupSandBox(const fs::path configPath)
{
  std::string error;
  clean();

  /* Create the directory structure and install plugins */
  CHECK(fs::create_directories(searchDir, ec));
  fs::copy(pluginBuildDir / configPath, searchDir, ec);
  CHECK(fs::create_directories(runtimeDir, ec));

  fs::path effectivePath   = searchDir / configPath;
  fs::path runtimePath     = runtimeDir / configPath;
  fs::path pluginBuildPath = pluginBuildDir / configPath;

  /* Instantiate and initialize a plugin DSO instance. */
  RemapPluginUnitTest *plugin = new RemapPluginUnitTest(configPath, effectivePath, runtimePath);

  return plugin;
}

bool
loadPlugin(RemapPluginUnitTest *plugin, std::string &error, PluginDebugObject *&debugObject)
{
  bool result = plugin->load(error);
  debugObject = plugin->getDebugObject();
  return result;
}

void
cleanupSandBox(RemapPluginInfo *plugin)
{
  delete plugin;
  clean();
}

SCENARIO("loading remap plugins", "[plugin][core]")
{
  REQUIRE_FALSE(sandboxDir.empty());

  std::string error;
  PluginDebugObject *debugObject = nullptr;

  GIVEN("a plugin which has only minimum required call back functions")
  {
    fs::path pluginConfigPath   = fs::path("plugin_required_cb.so");
    RemapPluginUnitTest *plugin = setupSandBox(pluginConfigPath);

    WHEN("loading")
    {
      bool result = loadPlugin(plugin, error, debugObject);

      THEN("expect it to successfully load")
      {
        CHECK(true == result);
        CHECK(error.empty());
      }
      cleanupSandBox(plugin);
    }
  }

  GIVEN("a plugin which is missing the plugin TSREMAP_FUNCNAME_INIT function")
  {
    fs::path pluginConfigPath   = fs::path("plugin_missing_init.so");
    RemapPluginUnitTest *plugin = setupSandBox(pluginConfigPath);

    WHEN("loading")
    {
      bool result = loadPlugin(plugin, error, debugObject);

      THEN("expect it to successfully load")
      {
        CHECK_FALSE(result);
        CHECK(error == plugin->getError(TSREMAP_FUNCNAME_INIT));
      }
      cleanupSandBox(plugin);
    }
  }

  GIVEN("a plugin which is missing the TSREMAP_FUNCNAME_DO_REMAP function")
  {
    fs::path pluginConfigPath   = fs::path("plugin_missing_doremap.so");
    RemapPluginUnitTest *plugin = setupSandBox(pluginConfigPath);

    WHEN("loading")
    {
      bool result = loadPlugin(plugin, error, debugObject);

      THEN("expect it to fail")
      {
        CHECK_FALSE(result);
        CHECK(error == plugin->getError(TSREMAP_FUNCNAME_DO_REMAP));
      }
      cleanupSandBox(plugin);
    }
  }

  GIVEN("a plugin which has TSREMAP_FUNCNAME_NEW_INSTANCE but is missing the TSREMAP_FUNCNAME_DELETE_INSTANCE function")
  {
    fs::path pluginConfigPath   = fs::path("plugin_missing_deleteinstance.so");
    RemapPluginUnitTest *plugin = setupSandBox(pluginConfigPath);

    WHEN("loading")
    {
      bool result = loadPlugin(plugin, error, debugObject);

      THEN("expect it to fail")
      {
        CHECK_FALSE(result);
        CHECK(error == plugin->getError(TSREMAP_FUNCNAME_DELETE_INSTANCE, TSREMAP_FUNCNAME_NEW_INSTANCE));
      }
      cleanupSandBox(plugin);
    }
  }

  GIVEN("a plugin which has TSREMAP_FUNCNAME_DELETE_INSTANCE but is missing the TSREMAP_FUNCNAME_NEW_INSTANCE function")
  {
    fs::path pluginConfigPath   = fs::path("plugin_missing_newinstance.so");
    RemapPluginUnitTest *plugin = setupSandBox(pluginConfigPath);

    WHEN("loading")
    {
      bool result = loadPlugin(plugin, error, debugObject);

      THEN("expect it to fail")
      {
        CHECK_FALSE(result);
        CHECK(error == plugin->getError(TSREMAP_FUNCNAME_NEW_INSTANCE, TSREMAP_FUNCNAME_DELETE_INSTANCE));
      }
      cleanupSandBox(plugin);
    }
  }
}

void
prepCallTest(bool toFail, PluginDebugObject *debugObject)
{
  debugObject->clear();
  debugObject->fail = toFail; // Tell the mock init to succeed or succeed.
}

void
checkCallTest(bool shouldHaveFailed, bool result, const std::string &error, std::string &expectedError, int &called)
{
  CHECK(1 == called); // Init was called.
  if (shouldHaveFailed) {
    CHECK(false == result);
    CHECK(error == expectedError); // Appropriate error was returned.
  } else {
    CHECK(true == result); // Init successful - returned TS_SUCCESS.
    CHECK(error.empty());  // No error was returned.
  }
}

SCENARIO("invoking plugin init", "[plugin][core]")
{
  REQUIRE_FALSE(sandboxDir.empty());

  std::string error;
  PluginDebugObject *debugObject = nullptr;

  GIVEN("plugin init function")
  {
    fs::path pluginConfigPath   = fs::path("plugin_testing_calls.so");
    RemapPluginUnitTest *plugin = setupSandBox(pluginConfigPath);

    bool result = loadPlugin(plugin, error, debugObject);
    CHECK(true == result);

    WHEN("init succeeds")
    {
      prepCallTest(/* toFail */ false, debugObject);

      result = plugin->init(error);

      THEN("expect init to be called, success code and no error to be returned")
      {
        std::string expectedError;

        checkCallTest(/* shouldHaveFailed */ false, result, error, expectedError, debugObject->initCalled);
      }
      cleanupSandBox(plugin);
    }

    WHEN("init fails")
    {
      prepCallTest(/* toFail */ true, debugObject);

      result = plugin->init(error);

      THEN("expect init to be called, failure code and an error to be returned")
      {
        std::string expectedError;
        expectedError.assign("failed to initialize plugin ").append(pluginConfigPath.string()).append(": Init failed");

        checkCallTest(/* shouldHaveFailed */ true, result, error, expectedError, debugObject->initCalled);
      }
      cleanupSandBox(plugin);
    }
  }
}

SCENARIO("invoking plugin instance init", "[plugin][core]")
{
  REQUIRE_FALSE(sandboxDir.empty());

  std::string error;
  PluginDebugObject *debugObject = nullptr;
  void *ih                       = nullptr; // Instance handler pointer.

  /* a sample test set of parameters */
  static const char *args[] = {"arg1", "arg2", "arg3"};
  static char **ARGV        = const_cast<char **>(args);
  static char ARGC          = sizeof ARGV;

  GIVEN("an instance init function")
  {
    fs::path pluginConfigPath   = fs::path("plugin_testing_calls.so");
    RemapPluginUnitTest *plugin = setupSandBox(pluginConfigPath);

    bool result = loadPlugin(plugin, error, debugObject);
    CHECK(true == result);

    WHEN("instance init succeeds")
    {
      prepCallTest(/* toFail */ false, debugObject);
      debugObject->input_ih = INSTANCE_HANDLER; /* this is what the plugin instance init will return */

      result = plugin->initInstance(ARGC, ARGV, &ih, error);

      THEN("expect init to be called successfully with no error and expected instance handler")
      {
        std::string expectedError;

        checkCallTest(/* shouldHaveFailed */ false, result, error, expectedError, debugObject->initInstanceCalled);

        /* Verify expected handler */
        CHECK(INSTANCE_HANDLER == ih);
        /* Plugin received the parameters that we passed */
        CHECK(ARGC == debugObject->argc);
        CHECK(ARGV == debugObject->argv);
        for (int i = 0; i < 3; i++) {
          CHECK(0 == strcmp(ARGV[i], debugObject->argv[i]));
        }
      }
      cleanupSandBox(plugin);
    }

    WHEN("instance init fails")
    {
      prepCallTest(/* toFail */ true, debugObject);

      result = plugin->initInstance(ARGC, ARGV, &ih, error);

      THEN("expect init to be called but failed with expected error and no instance handler")
      {
        std::string expectedError;
        expectedError.assign("failed to create instance for plugin ").append(pluginConfigPath.string()).append(": Init failed");

        checkCallTest(/* shouldHaveFailed */ true, result, error, expectedError, debugObject->initInstanceCalled);

        /* Ideally instance handler should not be touched in case of failure */
        CHECK(nullptr == ih);
        /* Plugin received the parameters that we passed */
        CHECK(ARGC == debugObject->argc);
        CHECK(ARGV == debugObject->argv);
        for (int i = 0; i < 3; i++) {
          CHECK(0 == strcmp(ARGV[i], debugObject->argv[i]));
        }
      }
      cleanupSandBox(plugin);
    }
  }
}

SCENARIO("unloading the plugin", "[plugin][core]")
{
  REQUIRE_FALSE(sandboxDir.empty());

  std::string error;
  PluginDebugObject *debugObject = nullptr;

  GIVEN("a 'done' function")
  {
    fs::path pluginConfigPath   = fs::path("plugin_testing_calls.so");
    RemapPluginUnitTest *plugin = setupSandBox(pluginConfigPath);

    bool result = loadPlugin(plugin, error, debugObject);
    CHECK(true == result);

    WHEN("'done' is called")
    {
      debugObject->clear();

      plugin->done();

      THEN("expect it to run") { CHECK(1 == debugObject->doneCalled); }
      cleanupSandBox(plugin);
    }
  }

  GIVEN("a 'delete_instance' function")
  {
    fs::path pluginConfigPath   = fs::path("plugin_testing_calls.so");
    RemapPluginUnitTest *plugin = setupSandBox(pluginConfigPath);

    bool result = loadPlugin(plugin, error, debugObject);
    CHECK(true == result);

    WHEN("'delete_instance' is called")
    {
      debugObject->clear();

      plugin->doneInstance(INSTANCE_HANDLER);

      THEN("expect it to run and receive the right instance handler")
      {
        CHECK(1 == debugObject->deleteInstanceCalled);
        CHECK(INSTANCE_HANDLER == debugObject->ih);
      }
      cleanupSandBox(plugin);
    }
  }
}

SCENARIO("config reload", "[plugin][core]")
{
  REQUIRE_FALSE(sandboxDir.empty());

  std::string error;
  PluginDebugObject *debugObject = nullptr;

  GIVEN("a 'config reload' callback function")
  {
    fs::path pluginConfigPath   = fs::path("plugin_testing_calls.so");
    RemapPluginUnitTest *plugin = setupSandBox(pluginConfigPath);

    bool result = loadPlugin(plugin, error, debugObject);
    CHECK(true == result);

    WHEN("'config reload' failed")
    {
      debugObject->clear();

      plugin->indicatePreReload();
      plugin->indicatePostReload(TSREMAP_CONFIG_RELOAD_FAILURE);

      THEN("expect it to run")
      {
        CHECK(1 == debugObject->preReloadConfigCalled);
        CHECK(1 == debugObject->postReloadConfigCalled);
        CHECK(TSREMAP_CONFIG_RELOAD_FAILURE == debugObject->postReloadConfigStatus);
      }
      cleanupSandBox(plugin);
    }

    WHEN("'config reload' is successful and the plugin is part of the new configuration")
    {
      debugObject->clear();

      plugin->indicatePreReload();
      plugin->indicatePostReload(TSREMAP_CONFIG_RELOAD_SUCCESS_PLUGIN_USED);

      THEN("expect it to run")
      {
        CHECK(1 == debugObject->preReloadConfigCalled);
        CHECK(1 == debugObject->postReloadConfigCalled);
        CHECK(TSREMAP_CONFIG_RELOAD_SUCCESS_PLUGIN_USED == debugObject->postReloadConfigStatus);
      }
      cleanupSandBox(plugin);
    }

    WHEN("'config reload' is successful and the plugin is part of the new configuration")
    {
      debugObject->clear();

      plugin->indicatePreReload();
      plugin->indicatePostReload(TSREMAP_CONFIG_RELOAD_SUCCESS_PLUGIN_UNUSED);

      THEN("expect it to run")
      {
        CHECK(1 == debugObject->preReloadConfigCalled);
        CHECK(1 == debugObject->postReloadConfigCalled);
        CHECK(TSREMAP_CONFIG_RELOAD_SUCCESS_PLUGIN_UNUSED == debugObject->postReloadConfigStatus);
      }
      cleanupSandBox(plugin);
    }
  }
}
