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

/* file_1.c:  an example program that opens files and reads them
 *            into a buffer
 *
 *
 *	Usage:
 *	  file_1.so <filename1> <filename2> ...
 *
 *              <filenamei> is the name of the ith file to
 *              be read.
 *
 */

#include <stdio.h>
#include <ts/ts.h>

#define PLUGIN_NAME "file_1"

void
TSPluginInit(int argc, const char *argv[])
{
  TSFile filep;
  char buf[4096];
  int i;
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
  }

  for (i = 1; i < argc; i++) {
    filep = TSfopen(argv[i], "r");
    if (!filep) {
      continue;
    }

    while (TSfgets(filep, buf, 4096)) {
      TSDebug(PLUGIN_NAME, "%s", buf);
    }

    TSfclose(filep);
  }
}
