/** @file

  Information about remap plugin libraries.

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

#include <unistd.h>

#include "RemapPluginInfo.h"
#include "tscore/ink_string.h"
#include "tscore/ink_memory.h"
#include "tscore/ink_apidefs.h"

#include "RemapPluginInfo.h"
#include "NextHopSelectionStrategy.h"
#ifdef PLUGIN_DSO_TESTS
#include "unit-tests/plugin_testing_common.h"
#else
#include "tscore/Diags.h"
#define PluginDebug Debug
#define PluginError Error
#endif

/**
 * @brief helper function that returns the function address from the plugin DSO
 *
 * There can be valid defined DSO symbols that are NULL
 * but when it comes to functions we can assume that
 * if not defined we can return nullptr and a valid address if the are defined.
 * @param symbol function symbol name
 * @param error error messages in case of symbol is not found
 * @return function address or nullptr if not found.
 */
template <class T>
T *
RemapPluginInfo::getFunctionSymbol(const char *symbol)
{
  std::string error; /* ignore the error, return nullptr if symbol not defined */
  void *address = nullptr;
  if (getSymbol(symbol, address, error)) {
    PluginDebug(_tag, "plugin '%s' found symbol '%s'", _configPath.c_str(), symbol);
  }
  return reinterpret_cast<T *>(address);
}

std::string
RemapPluginInfo::missingRequiredSymbolError(const std::string &pluginName, const char *required, const char *requiring)
{
  std::string error;
  error.assign("plugin ").append(pluginName).append(" missing required function ").append(required);
  if (requiring) {
    error.append(" if ").append(requiring).append(" is defined");
  }
  return error;
}

RemapPluginInfo::RemapPluginInfo(const fs::path &configPath, const fs::path &effectivePath, const fs::path &runtimePath)
  : PluginDso(configPath, effectivePath, runtimePath)
{
}

bool
RemapPluginInfo::load(std::string &error)
{
  error.clear();

  if (!PluginDso::load(error)) {
    return false;
  }

  init_cb               = getFunctionSymbol<Init_F>(TSREMAP_FUNCNAME_INIT);
  pre_config_reload_cb  = getFunctionSymbol<PreReload_F>(TSREMAP_FUNCNAME_PRE_CONFIG_RELOAD);
  post_config_reload_cb = getFunctionSymbol<PostReload_F>(TSREMAP_FUNCNAME_POST_CONFIG_RELOAD);
  done_cb               = getFunctionSymbol<Done_F>(TSREMAP_FUNCNAME_DONE);
  new_instance_cb       = getFunctionSymbol<New_Instance_F>(TSREMAP_FUNCNAME_NEW_INSTANCE);
  delete_instance_cb    = getFunctionSymbol<Delete_Instance_F>(TSREMAP_FUNCNAME_DELETE_INSTANCE);
  do_remap_cb           = getFunctionSymbol<Do_Remap_F>(TSREMAP_FUNCNAME_DO_REMAP);
  os_response_cb        = getFunctionSymbol<OS_Response_F>(TSREMAP_FUNCNAME_OS_RESPONSE);
  init_strategy_cb      = getFunctionSymbol<Init_Strategy_F>(TSREMAP_FUNCNAME_INIT_STRATEGY);

  /* Validate if the callback TSREMAP functions are specified correctly in the plugin. */
  bool valid = true;
  if (!init_cb) {
    error = missingRequiredSymbolError(_configPath.string(), TSREMAP_FUNCNAME_INIT);
    valid = false;
  } else if (!do_remap_cb) {
    error = missingRequiredSymbolError(_configPath.string(), TSREMAP_FUNCNAME_DO_REMAP);
    valid = false;
  } else if (!new_instance_cb && delete_instance_cb) {
    error = missingRequiredSymbolError(_configPath.string(), TSREMAP_FUNCNAME_NEW_INSTANCE, TSREMAP_FUNCNAME_DELETE_INSTANCE);
    valid = false;
  } else if (new_instance_cb && !delete_instance_cb) {
    error = missingRequiredSymbolError(_configPath.string(), TSREMAP_FUNCNAME_DELETE_INSTANCE, TSREMAP_FUNCNAME_NEW_INSTANCE);
    valid = false;
  }

  if (valid) {
    PluginDebug(_tag, "plugin '%s' callbacks validated", _configPath.c_str());
  } else {
    PluginError("plugin '%s' callbacks validation failed: %s", _configPath.c_str(), error.c_str());
  }
  return valid;
}

/* Initialize plugin (required). */
bool
RemapPluginInfo::init(std::string &error)
{
  TSRemapInterface ri;
  bool result = true;

  PluginDebug(_tag, "started initializing plugin '%s'", _configPath.c_str());

  /* A buffer to get the error from the plugin instance init function, be defensive here. */
  char tmpbuf[2048];
  ink_zero(tmpbuf);

  ink_zero(ri);
  ri.size            = sizeof(ri);
  ri.tsremap_version = TSREMAP_VERSION;

  setPluginContext();

  if (init_cb && init_cb(&ri, tmpbuf, sizeof(tmpbuf) - 1) != TS_SUCCESS) {
    error.assign("failed to initialize plugin ")
      .append(_configPath.string())
      .append(": ")
      .append(tmpbuf[0] ? tmpbuf : "Unknown plugin error");
    result = false;
  }

  resetPluginContext();

  PluginDebug(_tag, "finished initializing plugin '%s'", _configPath.c_str());

  return result;
}

/* Called when plugin is unloaded (optional). */
void
RemapPluginInfo::done()
{
  if (done_cb) {
    done_cb();
  }
}

bool
RemapPluginInfo::initInstance(int argc, char **argv, void **ih, std::string &error)
{
  TSReturnCode res = TS_SUCCESS;
  bool result      = true;

  PluginDebug(_tag, "started initializing instance of plugin '%s'", _configPath.c_str());

  /* A buffer to get the error from the plugin instance init function, be defensive here. */
  char tmpbuf[2048];
  ink_zero(tmpbuf);

  if (new_instance_cb) {
#if defined(freebsd) || defined(darwin)
    optreset = 1;
#endif
#if defined(__GLIBC__)
    optind = 0;
#else
    optind = 1;
#endif
    opterr = 0;
    optarg = nullptr;

    setPluginContext();

    res = new_instance_cb(argc, argv, ih, tmpbuf, sizeof(tmpbuf) - 1);

    resetPluginContext();

    if (TS_SUCCESS != res) {
      error.assign("failed to create instance for plugin ")
        .append(_configPath.string())
        .append(": ")
        .append(tmpbuf[0] ? tmpbuf : "Unknown plugin error");
      result = false;
    }
  }

  PluginDebug(_tag, "finished initializing instance of plugin '%s'", _configPath.c_str());

  return result;
}

void
RemapPluginInfo::doneInstance(void *ih)
{
  setPluginContext();

  if (delete_instance_cb) {
    delete_instance_cb(ih);
  }

  resetPluginContext();
}

TSRemapStatus
RemapPluginInfo::doRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  TSRemapStatus result = TSREMAP_NO_REMAP;

  setPluginContext();

  if (do_remap_cb) {
    result = do_remap_cb(ih, rh, rri);
  }

  resetPluginContext();

  return result;
}

void
RemapPluginInfo::osResponse(void *ih, TSHttpTxn rh, int os_response_type)
{
  setPluginContext();

  if (os_response_cb) {
    os_response_cb(ih, rh, os_response_type);
  }

  resetPluginContext();
}

RemapPluginInfo::~RemapPluginInfo() {}

void
RemapPluginInfo::indicatePreReload()
{
  setPluginContext();

  if (pre_config_reload_cb) {
    pre_config_reload_cb();
  }

  resetPluginContext();
}

void
RemapPluginInfo::indicatePostReload(TSRemapReloadStatus reloadStatus)
{
  setPluginContext();

  if (post_config_reload_cb) {
    post_config_reload_cb(reloadStatus);
  }

  resetPluginContext();
}

/* Initialize strategy (optional). */
std::shared_ptr<TSNextHopSelectionStrategy> RemapPluginInfo::initStrategy(void *ih)
{
  if (!init_strategy_cb) {
    PluginDebug(_tag, "plugin '%s' has no init_strategy_cb, returning nullptr", _configPath.c_str());
    return std::shared_ptr<TSNextHopSelectionStrategy>();
  }
  char tmpbuf[2048];
  ink_zero(tmpbuf);
  TSNextHopSelectionStrategy *strategy_raw = nullptr;
  if (init_strategy_cb(strategy_raw, ih, tmpbuf, sizeof(tmpbuf) - 1) != TS_SUCCESS) {
    PluginDebug(_tag, "plugin '%s' has init_strategy_cb but not success, returning error", _configPath.c_str());
    std::string error;
    error.assign("failed to initialize plugin ")
      .append(_configPath.string())
      .append(": ")
      .append(tmpbuf[0] ? tmpbuf : "Unknown plugin error");
    PluginError("plugin '%s' initializing strategy returned error: %s", _configPath.c_str(), error.c_str());
    return std::shared_ptr<TSNextHopSelectionStrategy>();
  }
  PluginDebug(_tag, "plugin '%s' has init_strategy_cb, returning strategy", _configPath.c_str());
  return std::shared_ptr<TSNextHopSelectionStrategy>(strategy_raw);
}


inline void
RemapPluginInfo::setPluginContext()
{
  _tempContext        = pluginThreadContext;
  pluginThreadContext = this;
  PluginDebug(_tag, "change plugin context from dso-addr:%p to dso-addr:%p", pluginThreadContext, _tempContext);
}

inline void
RemapPluginInfo::resetPluginContext()
{
  PluginDebug(_tag, "change plugin context from dso-addr:%p to dso-addr:%p (restore)", this, pluginThreadContext);
  pluginThreadContext = _tempContext;
}
