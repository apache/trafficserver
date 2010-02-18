/** @file

  A brief file description

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

/* file-1.c:  an example program that opens files and reads them  
 *            into a buffer       
 *
 *
 *	Usage:	
 *	(NT): File.dll <filename1> <filename2> ...
 *	(Solaris): file-1.so <filename1> <filename2> ...
 *
 *              <filenamei> is the name of the ith file to
 *              be read.
 *
 */

#include <stdio.h>
#include <ts/ts.h>

int
check_ts_version()
{

  const char *ts_version = INKTrafficServerVersionGet();
  int result = 0;

  if (ts_version) {
    int major_ts_version = 0;
    int minor_ts_version = 0;
    int patch_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
      return 0;
    }

    /* Need at least TS 5.2 */
    if (major_ts_version > 5) {
      result = 1;
    } else if (major_ts_version == 5) {
      if (minor_ts_version >= 2) {
        result = 1;
      }
    }
  }

  return result;
}

void
INKPluginInit(int argc, const char *argv[])
{
  INKFile filep;
  char buf[4096];
  int i;
  INKPluginRegistrationInfo info;

  info.plugin_name = "file_plugin";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (!INKPluginRegister(INK_SDK_VERSION_5_2, &info)) {
    INKError("Plugin registration failed.\n");
  }

  if (!check_ts_version()) {
    INKError("Plugin requires Traffic Server 5.2.0 or later\n");
    return;
  }

  for (i = 1; i < argc; i++) {
    filep = INKfopen(argv[i], "r");
    if (!filep) {
      continue;
    }

    while (INKfgets(filep, buf, 4096)) {
      INKDebug("debug-file", "%s", buf);
    }

    INKfclose(filep);
  }
}

