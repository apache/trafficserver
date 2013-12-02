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

#ifndef __PLUGIN_H__
#define __PLUGIN_H__

#include "List.h"

// need to keep syncronized with TSSDKVersion
//   in ts/ts.h.in
typedef enum
{
  PLUGIN_SDK_VERSION_UNKNOWN = -1,
  PLUGIN_SDK_VERSION_2_0,
  PLUGIN_SDK_VERSION_3_0,
  PLUGIN_SDK_VERSION_4_0
} PluginSDKVersion;

struct PluginRegInfo
{
  PluginRegInfo();
  ~PluginRegInfo();

  bool plugin_registered;
  char *plugin_path;

  PluginSDKVersion sdk_version;
  char *plugin_name;
  char *vendor_name;
  char *support_email;

  LINK(PluginRegInfo, link);
};

// Plugin registration vars
extern DLL<PluginRegInfo> plugin_reg_list;
extern PluginRegInfo *plugin_reg_current;

void plugin_init(void);

#endif /* __PLUGIN_H__ */
