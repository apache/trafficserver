/** @file

  An example plugin showing off how to use versioning.

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

#include <stdio.h>
#include <ts/ts.h>

#define PLUGIN_NAME "version"

void
TSPluginInit(int argc, const char *argv[])
{
  (void)argc; // unused
  (void)argv; // unused

  // Get the version:
  const char *ts_version = TSTrafficServerVersionGet();
  if (!ts_version) {
    TSError("[%s] Can't get Traffic Server version.", PLUGIN_NAME);
    return;
  }

  // Split it in major, minor, patch:
  int major_ts_version = 0;
  int minor_ts_version = 0;
  int patch_ts_version = 0;

  if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
    TSError("[%s] Can't extract versions.", PLUGIN_NAME);
    return;
  }

  TSPluginRegistrationInfo info;
  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  // partial compilation
  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed.", PLUGIN_NAME);
  }

  TSDebug(PLUGIN_NAME, "Running in Apache Traffic Server: v%d.%d.%d", major_ts_version, minor_ts_version, patch_ts_version);
}
