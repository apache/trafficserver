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

#include <ts/ts.h>

namespace
{
#define PINAME "test_TSHttpSsnInfo"
char PIName[] = PINAME;

DbgCtl dbg_ctl{PIName};

// NOTE:  It's important to flush this after writing so that a gold test using this plugin can examine the log before TS
// terminates.
//
std::fstream logFile;

void
handle_ssn_close(TSHttpSsn ssn)
{
  if (TSHttpSsnClientProtocolStackContains(ssn, "h2")) {
    TSMgmtInt count[11];
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[0], 0);
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[1], 1);
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[2], 2);
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[3], 3);
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[4], 4);
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[5], 5);
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[6], 6);
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[7], 7);
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[8], 8);
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[9], 9);
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[10], TS_SSN_INFO_RECEIVED_FRAME_COUNT_H2_UNKNOWN);

    logFile << "H2 Frames Received:" << "D" << count[0] << "," << "H" << count[1] << "," << "PR" << count[2] << "," << "RS"
            << count[3] << "," << "S" << count[4] << "," << "PP" << count[5] << "," << "P" << count[6] << "," << "G" << count[7]
            << "," << "WU" << count[8] << "," << "C" << count[9] << "," << "U" << count[10] << std::endl;
  } else {
    TSMgmtInt count[15];
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[0], 0);
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[1], 1);
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[2], 2);
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[3], 3);
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[4], 4);
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[5], 5);
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[6], 6);
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[7], 7);
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[8], 8);
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[9], 9);
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[10], 10);
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[11], 11);
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[12], 12);
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[13], 13);
    TSHttpSsnInfoIntGet(ssn, TS_SSN_INFO_RECEIVED_FRAME_COUNT, &count[14], TS_SSN_INFO_RECEIVED_FRAME_COUNT_H2_UNKNOWN);

    logFile << "H3 Frames Received:" << "D" << count[0] << "," << "H" << count[1] << "," << "Ra" << count[2] << "," << "CP"
            << count[3] << "," << "S" << count[4] << "," << "PP" << count[5] << "," << "Rb" << count[6] << "," << "G" << count[7]
            << "," << "Rc" << count[8] << "," << "Rd" << count[9] << "," << "UND" << count[10] << "," << "UND" << count[11] << ","
            << "UND" << count[12] << "," << "MPI" << count[13] << "," << "U" << count[14] << std::endl;
  }

  TSHttpSsnReenable(ssn, TS_EVENT_HTTP_CONTINUE);
}

int
globalContFunc(TSCont, TSEvent event, void *eventData)
{
  logFile << "Global: event=" << TSHttpEventNameLookup(event) << std::endl;

  Dbg(dbg_ctl, "Global: event=%s(%d) eventData=%p", TSHttpEventNameLookup(event), event, eventData);

  switch (event) {
  case TS_EVENT_HTTP_SSN_CLOSE:
    handle_ssn_close(static_cast<TSHttpSsn>(eventData));
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
  TSHttpHookAdd(TS_HTTP_SSN_CLOSE_HOOK, gCont);
}
