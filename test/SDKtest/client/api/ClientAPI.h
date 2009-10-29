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
  INK_FID_OPTIONS_PROCESS,
  INK_FID_OPTIONS_PROCESS_FINISH,
  INK_FID_CONNECTION_FINISH,
  INK_FID_PLUGIN_FINISH,
  INK_FID_REQUEST_CREATE,
  INK_FID_HEADER_PROCESS,
  INK_FID_PARTIAL_BODY_PROCESS,
  INK_FID_REPORT
} INKPluginFuncId;

typedef enum
{
  INK_CONN_COMPLETE,
  INK_CONN_ERR,
  INK_READ_ERR,
  INK_WRITE_ERR,
  INK_TIME_EXPIRE
} INKConnectionStatus;

typedef enum
{
  INK_STOP_SUCCESS,
  INK_STOP_FAIL,
  INK_KEEP_GOING
} INKRequestAction;

typedef enum
{
  INK_SUM,
  INK_MAX,
  INK_MIN,
  INK_AVE
} INKReportCombiner;

#ifdef __cplusplus
extern "C"
{
#endif
  extern void INKPluginInit(int clientID);

  extern void INKReportSingleData(char *metric, char *unit, INKReportCombiner combiner, double value);

  extern void INKFuncRegister(INKPluginFuncId fid);

#ifdef __cplusplus
}
#endif


#endif                          // _Api_h_
