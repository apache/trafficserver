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

#include "RemapPluginInfo.h"
#include "tscore/ink_string.h"
#include "tscore/ink_memory.h"

RemapPluginInfo::List RemapPluginInfo::g_list;

RemapPluginInfo::RemapPluginInfo(ts::file::path &&library_path) : path(std::move(library_path)) {}

RemapPluginInfo::~RemapPluginInfo()
{
  if (dl_handle) {
    dlclose(dl_handle);
  }
}

//
// Find a plugin by path from our linked list
//
RemapPluginInfo *
RemapPluginInfo::find_by_path(std::string_view library_path)
{
  auto spot = std::find_if(g_list.begin(), g_list.end(),
                           [&](self_type const &info) -> bool { return 0 == library_path.compare(info.path.view()); });
  return spot == g_list.end() ? nullptr : static_cast<self_type *>(spot);
}

//
// Add a plugin to the linked list
//
void
RemapPluginInfo::add_to_list(RemapPluginInfo *pi)
{
  g_list.append(pi);
}

//
// Remove and delete all plugins from a list.
//
void
RemapPluginInfo::delete_list()
{
  g_list.apply([](self_type *info) -> void { delete info; });
  g_list.clear();
}

//
// Tell all plugins (that so wish) that remap.config is being reloaded
//
void
RemapPluginInfo::indicate_reload()
{
  g_list.apply([](self_type *info) -> void {
    if (info->config_reload_cb) {
      info->config_reload_cb();
    }
  });
}
