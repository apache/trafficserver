/** @file

  Statistics example plugin.

  This plugin demonstrates the statistics API and also serves as a
  regression test for TS-4840. If traffic_server is restarted, a
  plugin ought to be able to safely reattach to its statistics.

  This source is included as an example in the developers so if you
  change it, you may have to update the line numbers on
  doc/developer-guide/plugins/adding-statistics.en.rst

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

#include <ts/ts.h>
#include <cinttypes>
#include <ctime>

#define PLUGIN_NAME "statistics"

void
TSPluginInit(int /* argc */, const char * /* argv */[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  int id;
  const char name[] = "plugin." PLUGIN_NAME ".now";

  if (TSStatFindName(name, &id) == TS_ERROR) {
    id = TSStatCreate(name, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
    if (id == TS_ERROR) {
      TSError("[%s] failed to register '%s'", PLUGIN_NAME, name);
      return;
    }
  }

  TSError("[%s] %s registered with id %d", PLUGIN_NAME, name, id);

#if DEBUG
  TSReleaseAssert(id != TS_ERROR);
#endif

  // Set an initial value for our statistic.
  TSStatIntSet(id, time(nullptr));

  // Increment the statistic as time passes.
  TSStatIntIncrement(id, 1);

  TSDebug(PLUGIN_NAME, "%s is set to %" PRId64, name, TSStatIntGet(id));
  TSReleaseAssert(TSPluginRegister(&info) == TS_SUCCESS);
}
