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



#ifndef _CORE_API_H
#define _CORE_API_H

#include <stdarg.h>             // for va_list

#include "ink_llqueue.h"
#include "MgmtDefs.h"           // MgmtInt, MgmtFloat, etc

#include "mgmtapi.h"
#include "CfgContextDefs.h"
#include "Tokenizer.h"

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

TSError Init(const char *socket_path = NULL, TSInitOptionT options = TS_MGMT_OPT_DEFAULTS);
TSError Terminate();

void Diags(TSDiagsT mode, const char *fmt, va_list ap);

/***************************************************************************
 * Control Operations
 ***************************************************************************/
TSProxyStateT ProxyStateGet();
TSError ProxyStateSet(TSProxyStateT state, TSCacheClearT clear);

TSError Reconfigure();         // TS reread config files
TSError Restart(bool cluster); //restart TM
TSError HardRestart();         //restart traffic_cop
TSError Bounce(bool cluster);  //restart traffic_server
TSError StorageDeviceCmdOffline(char const* dev); // Storage device operation.

/***************************************************************************
 * Record Operations
 ***************************************************************************/
/* For remote implementation of this interface, these functions will have
   to marshal/unmarshal and send request across the network */
TSError MgmtRecordGet(const char *rec_name, TSRecordEle * rec_ele);

TSError MgmtRecordSet(const char *rec_name, const char *val, TSActionNeedT * action_need);
TSError MgmtRecordSetInt(const char *rec_name, MgmtInt int_val, TSActionNeedT * action_need);
TSError MgmtRecordSetCounter(const char *rec_name, MgmtIntCounter counter_val, TSActionNeedT *action_need);
TSError MgmtRecordSetFloat(const char *rec_name, MgmtFloat float_val, TSActionNeedT * action_need);
TSError MgmtRecordSetString(const char *rec_name, const char*string_val, TSActionNeedT * action_need);


/***************************************************************************
 * File Operations
 ***************************************************************************/
TSError ReadFile(TSFileNameT file, char **text, int *size, int *version);
TSError WriteFile(TSFileNameT file, char *text, int size, int version);

/***************************************************************************
 * Events
 ***************************************************************************/

TSError EventSignal(char *event_name, va_list ap);
TSError EventResolve(char *event_name);
TSError ActiveEventGetMlt(LLQ * active_events);
TSError EventIsActive(char *event_name, bool * is_current);
TSError EventSignalCbRegister(char *event_name, TSEventSignalFunc func, void *data);
TSError EventSignalCbUnregister(char *event_name, TSEventSignalFunc func);

/***************************************************************************
 * Snapshots
 ***************************************************************************/
TSError SnapshotTake(char *snapshot_name);
TSError SnapshotRestore(char *snapshot_name);
TSError SnapshotRemove(char *snapshot_name);
TSError SnapshotGetMlt(LLQ * snapshots);

TSError StatsReset(bool cluster, const char* name = NULL);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
