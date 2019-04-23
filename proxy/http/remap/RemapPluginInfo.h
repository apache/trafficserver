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
#include "tscore/ink_platform.h"
#include "tscpp/util/IntrusiveDList.h"
#include "tscore/ts_file.h"
#include "ts/apidefs.h"
#include "ts/remap.h"

static constexpr const char *const TSREMAP_FUNCNAME_INIT            = "TSRemapInit";
static constexpr const char *const TSREMAP_FUNCNAME_CONFIG_RELOAD   = "TSRemapConfigReload";
static constexpr const char *const TSREMAP_FUNCNAME_DONE            = "TSRemapDone";
static constexpr const char *const TSREMAP_FUNCNAME_NEW_INSTANCE    = "TSRemapNewInstance";
static constexpr const char *const TSREMAP_FUNCNAME_DELETE_INSTANCE = "TSRemapDeleteInstance";
static constexpr const char *const TSREMAP_FUNCNAME_DO_REMAP        = "TSRemapDoRemap";
static constexpr const char *const TSREMAP_FUNCNAME_OS_RESPONSE     = "TSRemapOSResponse";

/** Information for a remap plugin.
 *  This stores the name of the library file and the callback entry points.
 */
class RemapPluginInfo
{
public:
  using self_type = RemapPluginInfo; ///< Self reference type.

  /// Initialization function, called on library load.
  using Init_F = TSReturnCode(TSRemapInterface *api_info, char *errbuf, int errbuf_size);
  /// Reload function, called to inform the plugin of a configuration reload.
  using Reload_F = void();
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

  self_type *_next = nullptr;
  self_type *_prev = nullptr;
  using Linkage    = ts::IntrusiveLinkage<self_type>;
  using List       = ts::IntrusiveDList<Linkage>;

  ts::file::path path;
  void *dl_handle                       = nullptr; /* "handle" for the dynamic library */
  Init_F *init_cb                       = nullptr;
  Reload_F *config_reload_cb            = nullptr;
  Done_F *done_cb                       = nullptr;
  New_Instance_F *new_instance_cb       = nullptr;
  Delete_Instance_F *delete_instance_cb = nullptr;
  Do_Remap_F *do_remap_cb               = nullptr;
  OS_Response_F *os_response_cb         = nullptr;

  explicit RemapPluginInfo(ts::file::path &&library_path);
  ~RemapPluginInfo();

  static self_type *find_by_path(std::string_view library_path);
  static void add_to_list(self_type *pi);
  static void delete_list();
  static void indicate_reload();

  /// Singleton list of remap plugin info instances.
  static List g_list;
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
