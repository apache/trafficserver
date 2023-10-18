/** @file

  Information about remap plugins.

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

#include <string>
#include <tuple>

#include "tscore/ink_platform.h"
#include "ts/apidefs.h"
#include "ts/remap.h"
#include "PluginDso.h"

class url_mapping;

extern thread_local PluginThreadContext *pluginThreadContext;

static constexpr const char *const TSREMAP_FUNCNAME_INIT               = "TSRemapInit";
static constexpr const char *const TSREMAP_FUNCNAME_PRE_CONFIG_RELOAD  = "TSRemapPreConfigReload";
static constexpr const char *const TSREMAP_FUNCNAME_POST_CONFIG_RELOAD = "TSRemapPostConfigReload";
static constexpr const char *const TSREMAP_FUNCNAME_DONE               = "TSRemapDone";
static constexpr const char *const TSREMAP_FUNCNAME_NEW_INSTANCE       = "TSRemapNewInstance";
static constexpr const char *const TSREMAP_FUNCNAME_DELETE_INSTANCE    = "TSRemapDeleteInstance";
static constexpr const char *const TSREMAP_FUNCNAME_DO_REMAP           = "TSRemapDoRemap";
static constexpr const char *const TSREMAP_FUNCNAME_OS_RESPONSE        = "TSRemapOSResponse";

/**
 * Holds information for a remap plugin, remap specific callback entry points for plugin init/done and instance init/done, do_remap,
 * origin server response,
 */
class RemapPluginInfo : public PluginDso
{
public:
  /// Initialization function, called on library load.
  using Init_F = TSReturnCode(TSRemapInterface *api_info, char *errbuf, int errbuf_size);
  /// Reload function, called to inform the plugin that configuration is going to be reloaded.
  using PreReload_F = void();
  /// Reload function, called to inform the plugin that configuration is done reloading.
  using PostReload_F = void(TSRemapReloadStatus);
  /// Called when remapping for a transaction has finished.
  using Done_F = void();
  /// Create an rule instance.
  using New_Instance_F = TSReturnCode(int argc, char *argv[], void **ih, char *errbuf, int errbuf_size);
  /// Delete a rule instance.
  using Delete_Instance_F = void(void *ih);
  /// Perform remap.
  using Do_Remap_F = TSRemapStatus(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri);
  /// I have no idea what this is for.
  using OS_Response_F = void(void *ih, TSHttpTxn rh, int os_response_type);

  void *dl_handle                       = nullptr; /* "handle" for the dynamic library */
  Init_F *init_cb                       = nullptr;
  PreReload_F *pre_config_reload_cb     = nullptr;
  PostReload_F *post_config_reload_cb   = nullptr;
  Done_F *done_cb                       = nullptr;
  New_Instance_F *new_instance_cb       = nullptr;
  Delete_Instance_F *delete_instance_cb = nullptr;
  Do_Remap_F *do_remap_cb               = nullptr;
  OS_Response_F *os_response_cb         = nullptr;

  RemapPluginInfo(const fs::path &configPath, const fs::path &effectivePath, const fs::path &runtimePath);
  ~RemapPluginInfo();

  /* Overload to add / execute remap plugin specific tasks during the plugin loading */
  bool load(std::string &error) override;

  /* Used by the factory to invoke callbacks during plugin load, init and unload  */
  bool init(std::string &error) override;
  void done(void) override;

  /* Used by the facility that handles remap plugin instances to invoke callbacks per plugin instance */
  bool initInstance(int argc, char **argv, void **ih, std::string &error);
  void doneInstance(void *ih);

  /* Used by the other parts of the traffic server core while handling requests */
  TSRemapStatus doRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri);
  void osResponse(void *ih, TSHttpTxn rh, int os_response_type);

  /* Used by traffic server core to indicate configuration reload */
  void indicatePreReload() override;
  void indicatePostReload(TSRemapReloadStatus reloadStatus) override;

protected:
  /* Utility to be used only with unit testing */
  std::string missingRequiredSymbolError(const std::string &pluginName, const char *required, const char *requiring = nullptr);
  template <class T> T *getFunctionSymbol(const char *symbol);
  void setPluginContext();
  void resetPluginContext();

  static constexpr const char *const _tag = "plugin_remap"; /** @brief log tag used by this class */

  PluginThreadContext *_tempContext = nullptr;
};

/**
 * struct host_hdr_info;
 * Used to store info about host header
 **/
struct host_hdr_info {
  const char *request_host;
  int host_len;
  int request_port;
};
