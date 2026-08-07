/** @file

  Test the TSPortDescriptor API.

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

namespace
{
constexpr char PLUGIN_NAME[] = "port_descriptor";

int
accept_connection(TSCont /* contp */, TSEvent event, void *edata)
{
  if (event != TS_EVENT_NET_ACCEPT) {
    TSError("[%s] unexpected accept event: %d", PLUGIN_NAME, event);
    return TS_EVENT_ERROR;
  }

  TSVConnClose(static_cast<TSVConn>(edata));
  return TS_EVENT_NONE;
}
} // namespace

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info{PLUGIN_NAME, "Apache Software Foundation", "dev@trafficserver.apache.org"};

  TSReleaseAssert(TSPluginRegister(&info) == TS_SUCCESS);
  TSReleaseAssert(argc == 2);

  TSPortDescriptor descriptor;
  TSCont           contp = TSContCreate(accept_connection, TSMutexCreate());

  TSReleaseAssert(TSPortDescriptorAccept(&descriptor, contp) == TS_ERROR);
  TSReleaseAssert(TSPortDescriptorParse(nullptr, &descriptor) == TS_ERROR);
  TSReleaseAssert(TSPortDescriptorAccept(&descriptor, contp) == TS_ERROR);
  TSReleaseAssert(TSPortDescriptorParse(argv[1], nullptr) == TS_ERROR);
  TSReleaseAssert(TSPortDescriptorParse(argv[1], &descriptor) == TS_SUCCESS);
  TSReleaseAssert(TSPortDescriptorAccept(nullptr, contp) == TS_ERROR);
  TSReleaseAssert(TSPortDescriptorAccept(&descriptor, nullptr) == TS_ERROR);
  TSReleaseAssert(TSPortDescriptorAccept(&descriptor, contp) == TS_SUCCESS);
}
