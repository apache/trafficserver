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

/************************* ClientAPI.h ****************************

 ********************************************************************/

#ifndef _Api_h_
#define _Api_h_

typedef enum
{
  TS_FID_OPTIONS_PROCESS,
  TS_FID_OPTIONS_PROCESS_FINISH,
  TS_FID_CONNECTION_FINISH,
  TS_FID_PLUGIN_FINISH,
  TS_FID_REQUEST_CREATE,
  TS_FID_HEADER_PROCESS,
  TS_FID_PARTIAL_BODY_PROCESS,
  TS_FID_REPORT
} TSPluginFuncId;

typedef enum
{
  TS_CONN_COMPLETE,
  TS_CONN_ERR,
  TS_READ_ERR,
  TS_WRITE_ERR,
  TS_TIME_EXPIRE
} TSConnectionStatus;

typedef enum
{
  TS_STOP_SUCCESS,
  TS_STOP_FAIL,
  TS_KEEP_GOING
} TSRequestAction;

typedef enum
{
  TS_SUM,
  TS_MAX,
  TS_MIN,
  TS_AVE
} TSReportCombiner;

#ifdef __cplusplus
extern "C"
{
#endif
  extern void TSPluginInit(int clientID);

  extern void TSReportSingleData(char *metric, char *unit, TSReportCombiner combiner, double value);

  extern void TSFuncRegister(TSPluginFuncId fid);

#ifdef __cplusplus
}
#endif


#endif                          // _Api_h_
