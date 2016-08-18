/**
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

/**
 * @file Plugin.cc
 */
#include "atscppapi/Plugin.h"
#include <ts/ts.h>
const std::string atscppapi::HOOK_TYPE_STRINGS[] = {
  std::string("HOOK_READ_REQUEST_HEADERS_PRE_REMAP"), std::string("HOOK_READ_REQUEST_HEADERS_POST_REMAP"),
  std::string("HOOK_SEND_REQUEST_HEADERS"),           std::string("HOOK_READ_RESPONSE_HEADERS"),
  std::string("HOOK_SEND_RESPONSE_HEADERS"),          std::string("HOOK_OS_DNS"),
  std::string("HOOK_READ_REQUEST_HEADERS"),           std::string("HOOK_READ_CACHE_HEADERS"),
  std::string("HOOK_CACHE_LOOKUP_COMPLETE"),          std::string("HOOK_SELECT_ALT")};

void
atscppapi::RegisterGlobalPlugin(std::string name, std::string vendor, std::string email)
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = const_cast<char *>(name.c_str());
  info.vendor_name   = const_cast<char *>(vendor.c_str());
  info.support_email = const_cast<char *>(email.c_str());
  if (TSPluginRegister(&info) != TS_SUCCESS)
    TSError("[Plugin.cc] Plugin registration failed.");
}
