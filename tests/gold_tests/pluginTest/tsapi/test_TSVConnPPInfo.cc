/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <fstream>
#include <cstdlib>
#include <arpa/inet.h>

#include <ts/ts.h>

namespace
{
#define PINAME "test_TSVConnPPInfo"
char PIName[] = PINAME;

DbgCtl dbg_ctl{PIName};

// NOTE:  It's important to flush this after writing so that a gold test using this plugin can examine the log before TS
// terminates.
//
std::fstream logFile;

void
handle_ssn_start(TSHttpSsn ssn)
{
  TSVConn   vconn = TSHttpSsnClientVConnGet(ssn);
  TSMgmtInt pp_ver;
  auto      ret = TSVConnPPInfoIntGet(vconn, TS_PP_INFO_VERSION, &pp_ver);
  if (ret == TS_SUCCESS && pp_ver != 0) {
    TSMgmtInt                 info[2];
    const struct sockaddr_in *addr[2];
    int                       addr_len[2];
    TSVConnPPInfoIntGet(vconn, TS_PP_INFO_PROTOCOL, &info[0]);
    TSVConnPPInfoIntGet(vconn, TS_PP_INFO_SOCK_TYPE, &info[1]);
    TSVConnPPInfoGet(vconn, TS_PP_INFO_SRC_ADDR, reinterpret_cast<const char **>(&addr[0]), &addr_len[0]);
    TSVConnPPInfoGet(vconn, TS_PP_INFO_DST_ADDR, reinterpret_cast<const char **>(&addr[1]), &addr_len[1]);

    logFile << "PP Info Received:" << "V" << pp_ver << "," << "P" << info[0] << "," << "T" << info[1] << "," << "SRC"
            << inet_ntoa(addr[0]->sin_addr) << "," << "DST" << inet_ntoa(addr[1]->sin_addr) << std::endl;
  }

  TSHttpSsnReenable(ssn, TS_EVENT_HTTP_CONTINUE);
}

int
globalContFunc(TSCont, TSEvent event, void *eventData)
{
  logFile << "Global: event=" << TSHttpEventNameLookup(event) << std::endl;

  Dbg(dbg_ctl, "Global: event=%s(%d) eventData=%p", TSHttpEventNameLookup(event), event, eventData);

  switch (event) {
  case TS_EVENT_HTTP_SSN_START:
    handle_ssn_start(static_cast<TSHttpSsn>(eventData));
    break;
  default:
    break;
  } // end switch

  return 0;
}

TSCont gCont;

} // end anonymous namespace

void
TSPluginInit(int /* argc ATS_UNUSED */, const char ** /* argv ATS_UNUSED */)
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PIName;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError(PINAME ": Plugin registration failed");

    return;
  }

  const char *fileSpec = std::getenv("OUTPUT_FILE");

  if (nullptr == fileSpec) {
    TSError(PINAME ": Environment variable OUTPUT_FILE not found.");

    return;
  }

  // Disable output buffering for logFile, so that explicit flushing is not necessary.
  logFile.rdbuf()->pubsetbuf(nullptr, 0);

  logFile.open(fileSpec, std::ios::out);
  if (!logFile.is_open()) {
    TSError(PINAME ": could not open log file \"%s\"", fileSpec);

    return;
  }

  // Mutex to protect the logFile object.
  TSMutex mtx = TSMutexCreate();
  gCont       = TSContCreate(globalContFunc, mtx);
  TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, gCont);
}
