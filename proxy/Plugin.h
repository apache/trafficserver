/** @file

  Plugin init declarations

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
#include "tscore/List.h"

typedef enum { RELOAD_OFF, RELOAD_ON, RELOAD_COUNT } PluginDynamicReloadMode;

// read records.config to parse plugin related configs
void parsePluginConfig();

bool isPluginDynamicReloadEnabled();

struct PluginRegInfo {
  PluginRegInfo();
  ~PluginRegInfo();

  bool plugin_registered = false;
  char *plugin_path      = nullptr;

  char *plugin_name   = nullptr;
  char *vendor_name   = nullptr;
  char *support_email = nullptr;

  void *dlh = nullptr;

  LINK(PluginRegInfo, link);
};

// Plugin registration vars
extern DLL<PluginRegInfo> plugin_reg_list;
extern PluginRegInfo *plugin_reg_current;

bool plugin_init(bool validateOnly = false);
bool plugin_dso_load(const char *path, void *&handle, void *&init, std::string &error);

/** Abstract interface class for plugin based continuations.

    The primary intended use of this is for logging so that continuations
    that generate logging messages can generate plugin local data in a
    generic way.

    The core will at appropriate times dynamically cast the continuation
    to this class and if successful access the plugin data via these
    methods.

    Plugins should mix this in to continuations for which it is useful.
    The default implementations return empty / invalid responses and should
    be overridden by the plugin.
 */
class PluginIdentity
{
public:
  /// Make sure destructor is virtual.
  virtual ~PluginIdentity() {}
  /** Get the plugin tag.
      The returned string must have a lifetime at least as long as the plugin.
      @return A string identifying the plugin or @c NULL.
  */
  virtual char const *
  getPluginTag() const
  {
    return nullptr;
  }
  /** Get the plugin instance ID.
      A plugin can create multiple subsidiary instances. This is used as the
      identifier for those to distinguish the instances.
  */
  virtual int64_t
  getPluginId() const
  {
    return 0;
  }
};
