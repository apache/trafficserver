/** @file

  an example plugin showing off how to use versioning

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

void
TSPluginInit(int argc, const char *argv[])
{
  (void)argc; // unused
  (void)argv; // unused

  // Get the version:
  const char *ts_version = TSTrafficServerVersionGet();
  if (!ts_version) {
    TSError("[version] Can't get Traffic Server verion.\n");
    return;
  }

  // Split it in major, minor, patch:
  int major_ts_version = 0;
  int minor_ts_version = 0;
  int patch_ts_version = 0;

  if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
    TSError("[version] Can't extract verions.\n");
    return;
  }

  TSPluginRegistrationInfo info;
  info.plugin_name   = "version-plugin";
  info.vendor_name   = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

// partial compilation
#if (TS_VERSION_NUMBER < 3000000)
  if (TSPluginRegister(TS_SDK_VERSION_2_0, &info) != TS_SUCCESS) {
#elif (TS_VERSION_NUMBER < 6000000)
  if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS) {
#else
  if (TSPluginRegister(&info) != TS_SUCCESS) {
#endif
    TSError("[version] Plugin registration failed. \n");
  }

  TSDebug("debug-version-plugin", "Running in Apache Traffic Server: v%d.%d.%d\n", major_ts_version, minor_ts_version,
          patch_ts_version);
}
