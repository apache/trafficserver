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

#include "plugin_testing_common.h"
#include "../PluginDso.h"

class PluginContext;
thread_local PluginThreadContext *pluginThreadContext;

std::error_code ec;

/* The following are dirs that are used commonly in the unit-tests */
static fs::path sandboxDir     = getTemporaryDir();
static fs::path runtimeDir     = sandboxDir / fs::path("runtime");
static fs::path searchDir      = sandboxDir / fs::path("search");
static fs::path pluginBuildDir = fs::current_path() / fs::path("unit-tests/.libs");

/* The following are paths used in all scenarios in the unit tests */
static fs::path configPath      = fs::path("plugin_v1.so");
static fs::path pluginBuildPath = pluginBuildDir / configPath;
static fs::path effectivePath   = searchDir / configPath;
static fs::path runtimePath     = runtimeDir / configPath;

void
clean()
{
  fs::remove(sandboxDir, ec);
}

/* Mock used only to make PluginDso concrete enough to be tested */
class PluginDsoUnitTest : public PluginDso
{
public:
  PluginDsoUnitTest(const fs::path &configPath, const fs::path &effectivePath, const fs::path &runtimePath)
    : PluginDso(configPath, effectivePath, runtimePath)
  {
    /* don't remove runtime DSO copy preventively so we can check if it was created properly */
    _preventiveCleaning = false;
  }

  virtual void
  indicatePreReload() override
  {
  }
  virtual void
  indicatePostReload(TSRemapReloadStatus reloadStatus) override
  {
  }
  virtual bool
  init(std::string &error) override
  {
    return true;
  }
  virtual void
  done() override
  {
  }
};

/*
 * The following scenario tests loading and unloading of plugins
 */
SCENARIO("loading plugins", "[plugin][core]")
{
  REQUIRE_FALSE(sandboxDir.empty());

  clean();
  std::string error;

  GIVEN("a valid plugin")
  {
    /* Setup the test fixture - search, runtime dirs and install a plugin with some defined callback functions */
    CHECK(fs::create_directories(searchDir, ec));
    CHECK(fs::create_directories(runtimeDir, ec));
    fs::copy(pluginBuildPath, searchDir, ec);

    /* Instantiate and initialize a plugin DSO instance. Make sure effective path exists, used to load */
    CHECK(fs::exists(effectivePath));
    PluginDsoUnitTest plugin(configPath, effectivePath, runtimePath);

    WHEN("loading a valid plugin")
    {
      bool result = plugin.load(error);

      THEN("expect it to successfully load")
      {
        CHECK(true == result);
        CHECK(error.empty());
        CHECK(effectivePath == plugin.effectivePath());
        CHECK(runtimePath == plugin.runtimePath());
        CHECK(fs::exists(runtimePath));
      }
      CHECK(fs::remove(sandboxDir, ec));
    }

    WHEN("loading a valid plugin")
    {
      bool result = plugin.load(error);

      THEN("expect saving the right DSO file modification time")
      {
        CHECK(true == result);
        CHECK(error.empty());
        std::error_code ec;
        fs::file_status fs = fs::status(effectivePath, ec);
        CHECK(plugin.modTime() == fs::modification_time(fs));
      }
      CHECK(fs::remove(sandboxDir, ec));
    }

    WHEN("loading a valid plugin but missing runtime dir")
    {
      CHECK(fs::remove(runtimeDir, ec));
      CHECK_FALSE(fs::exists(runtimePath));
      bool result = plugin.load(error);

      THEN("expect it to fail")
      {
        CHECK_FALSE(true == result);
        CHECK("failed to create a copy: No such file or directory" == error);
      }
      CHECK(fs::remove(sandboxDir, ec));
    }

    WHEN("loading a valid plugin twice in a row")
    {
      /* First attempt OK */
      bool result = plugin.load(error);
      CHECK(true == result);
      CHECK(error.empty());

      /* Second attempt */
      result = plugin.load(error);

      THEN("expect it to fail the second attempt")
      {
        CHECK_FALSE(true == result);
        CHECK("plugin already loaded" == error);
      }
      CHECK(fs::remove(sandboxDir, ec));
    }

    WHEN("explicitly unloading a valid but not loaded plugin")
    {
      /* Make sure it is not loaded, runtime DSO not present */
      CHECK_FALSE(fs::exists(runtimePath));

      /* Unload w/o loading beforehand */
      bool result = plugin.unload(error);

      THEN("expect the unload to fail")
      {
        CHECK(false == result);
        CHECK_FALSE(error.empty());
        CHECK_FALSE(fs::exists(runtimePath));
      }
      CHECK(fs::remove(sandboxDir, ec));
    }

    WHEN("unloading a valid plugin twice in a row")
    {
      /* First attempt OK */
      bool result = plugin.load(error);
      CHECK(true == result);
      CHECK(error.empty());
      result = plugin.unload(error);
      CHECK(true == result);
      CHECK("" == error);

      /* Second attempt */
      result = plugin.unload(error);

      THEN("expect it to fail the second attempt")
      {
        CHECK_FALSE(true == result);
        CHECK("no plugin loaded" == error);
      }
      CHECK(fs::remove(sandboxDir, ec));
    }

    WHEN("explicitly unloading a valid and loaded plugin")
    {
      /* Make sure it is not loaded, runtime DSO not present */
      CHECK_FALSE(fs::exists(runtimePath));

      /* Load and make sure it is loaded */
      CHECK(plugin.load(error));
      /* Effective and runtime path set */
      CHECK(effectivePath == plugin.effectivePath());
      CHECK(runtimePath == plugin.runtimePath());
      /* Runtime DSO should be present */
      CHECK(fs::exists(runtimePath));

      /* Unload */
      bool result = plugin.unload(error);

      THEN("expect it to successfully unload")
      {
        CHECK(true == result);
        CHECK(error.empty());
        /* Effective and runtime path still set */
        CHECK(effectivePath == plugin.effectivePath());
        CHECK(runtimePath == plugin.runtimePath());
        /* Runtime DSO should not be found anymore */
        CHECK_FALSE(fs::exists(runtimePath));
      }
      CHECK(fs::remove(sandboxDir, ec));
    }

    WHEN("implicitly unloading a valid and loaded plugin")
    {
      {
        PluginDsoUnitTest localPlugin(configPath, effectivePath, runtimePath);

        /* Load and make sure it is loaded */
        CHECK(localPlugin.load(error));
        /* Effective and runtime path set */
        CHECK(effectivePath == localPlugin.effectivePath());
        CHECK(runtimePath == localPlugin.runtimePath());
        /* Runtime DSO should be present */
        CHECK(fs::exists(runtimePath));

        /* Unload by going out of scope */
      }

      THEN("expect it to successfully unload and clean after itself")
      {
        /* Runtime path should be removed after unloading */
        CHECK_FALSE(fs::exists(runtimePath));
      }
      CHECK(fs::remove(sandboxDir, ec));
    }
  }

  GIVEN("a plugin instance initialized with an empty effective path")
  {
    std::string error;
    PluginDsoUnitTest plugin(configPath, /* effectivePath */ fs::path(), runtimePath);

    WHEN("loading the plugin")
    {
      bool result = plugin.load(error);

      THEN("expect the load to fail")
      {
        CHECK_FALSE(true == result);
        CHECK("empty effective path" == error);
        CHECK(plugin.effectivePath().empty());
        CHECK(0 == plugin.modTime());
        CHECK(runtimePath == plugin.runtimePath());
        CHECK_FALSE(fs::exists(runtimePath));
      }
    }
  }

  GIVEN("an invalid plugin")
  {
    /* Create the directory structure and install plugins */
    CHECK(fs::create_directories(searchDir, ec));
    CHECK(fs::create_directories(runtimeDir, ec));
    /* Create an invalid plugin and make sure the effective path to it exists */
    std::ofstream file(effectivePath.string());
    file << "Invalid plugin DSO content";
    file.close();
    CHECK(fs::exists(effectivePath));

    /* Instantiate and initialize a plugin DSO instance. */
    std::string error;
    PluginDsoUnitTest plugin(configPath, effectivePath, runtimePath);

    WHEN("loading an invalid plugin")
    {
      bool result = plugin.load(error);

      THEN("expect it to fail to load")
      {
        /* After calling load() the following should be set correctly */
        CHECK(effectivePath == plugin.effectivePath());
        CHECK(runtimePath == plugin.runtimePath());

        /* But the load should fail and an error should be returned */
        CHECK(false == result);
        CHECK_FALSE(error.empty());

        /* Runtime DSO should not exist since the load failed. */
        CHECK_FALSE(fs::exists(runtimePath));
      }
      CHECK(fs::remove(sandboxDir, ec));
    }
  }
}

/*
 * The following scenario tests finding symbols inside the DSO.
 */
SCENARIO("looking for symbols inside a plugin DSO", "[plugin][core]")
{
  REQUIRE_FALSE(sandboxDir.empty());

  clean();
  std::string error;

  /* Setup the test fixture - search, runtime dirs and install a plugin with some defined callback functions */
  CHECK(fs::create_directories(searchDir, ec));
  CHECK(fs::create_directories(runtimeDir, ec));
  fs::copy(pluginBuildDir / configPath, searchDir, ec);

  /* Initialize a plugin DSO instance */
  PluginDsoUnitTest plugin(configPath, effectivePath, runtimePath);

  /* Now test away. */
  GIVEN("plugin loaded successfully")
  {
    CHECK(plugin.load(error));

    WHEN("looking for an existing symbol")
    {
      THEN("expect to find it")
      {
        void *s = nullptr;
        CHECK(plugin.getSymbol("TSRemapInit", s, error));
        CHECK(nullptr != s);
        CHECK(error.empty());
      }
      CHECK(fs::remove(sandboxDir, ec));
    }

    WHEN("looking for non-existing symbol")
    {
      THEN("expect not to find it and get an error")
      {
        void *s = nullptr;
        CHECK_FALSE(plugin.getSymbol("NONEXISTING_SYMBOL", s, error));
        CHECK(nullptr == s);
        CHECK_FALSE(error.empty());
      }
      CHECK(fs::remove(sandboxDir, ec));
    }

    WHEN("looking for multiple existing symbols")
    {
      THEN("expect to find them all")
      {
        std::vector<const char *> list{"TSRemapInit",           "TSRemapDone",       "TSRemapDoRemap", "TSRemapNewInstance",
                                       "TSRemapDeleteInstance", "TSRemapOSResponse", "TSPluginInit",   "pluginDsoVersionTest"};
        for (auto symbol : list) {
          void *s = nullptr;
          CHECK(plugin.getSymbol(symbol, s, error));
          CHECK(nullptr != s);
          CHECK(error.empty());
        }
      }
      CHECK(fs::remove(sandboxDir, ec));
    }

    /* The following version function is used only for unit-testing of the plugin factory functionality */
    WHEN("using a symbol to call the corresponding version function")
    {
      THEN("expect to return the version number")
      {
        void *s = nullptr;
        CHECK(plugin.getSymbol("pluginDsoVersionTest", s, error));
        int (*version)() = reinterpret_cast<int (*)()>(s);
        int ver          = version ? version() : -1;
        CHECK(1 == ver);
      }
      CHECK(fs::remove(sandboxDir, ec));
    }
  }
}
