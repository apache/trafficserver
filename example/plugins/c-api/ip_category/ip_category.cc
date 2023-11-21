/** @file

  Demonstrate a plugin using TSHttpSetCategoryIPSpaces.

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

/** A plugin that demnstrates implementing ACME_INTERNAL, ACME_EXTERNAL, and
 * ACME_ALL IP categories.
 *
 *
 *   Usage:
 *     # Place the following in plugin.config:
 *     ip_category.so
 */

#include <string>
#include <string_view>
#include <sys/socket.h>

#include "swoc/BufferWriter.h"
#include "swoc/IPAddr.h"
#include "swoc/IPRange.h"
#include "ts/apidefs.h"
#include "ts/ts.h"

namespace
{

std::string const PLUGIN_NAME = "ip_category";
DbgCtl dbg_ctl{"ip_category"};

} // anonymous namespace

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = PLUGIN_NAME.c_str();
  info.vendor_name   = "apache";
  info.support_email = "edge@yahooinc.com";
  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s]: failure calling TSPluginRegister.", PLUGIN_NAME.c_str());
    return;
  }
}
