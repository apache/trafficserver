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

#include "INKMgmtAPI.h"
#include "CfgContextDefs.h"
#include "Tokenizer.h"

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

INKError Init(const char *socket_path = NULL, TSInitOptionT options = TS_MGMT_OPT_DEFAULTS);
INKError Terminate();

void Diags(INKDiagsT mode, const char *fmt, va_list ap);

/***************************************************************************
 * Control Operations
 ***************************************************************************/
INKProxyStateT ProxyStateGet();
INKError ProxyStateSet(INKProxyStateT state, INKCacheClearT clear);

INKError Reconfigure();         // TS reread config files
INKError Restart(bool cluster); //restart TM
INKError HardRestart();         //restart traffic_cop

/***************************************************************************
 * Record Operations
 ***************************************************************************/
/* For remote implementation of this interface, these functions will have
   to marshal/unmarshal and send request across the network */
INKError MgmtRecordGet(const char *rec_name, INKRecordEle * rec_ele);

INKError MgmtRecordSet(const char *rec_name, const char *val, INKActionNeedT * action_need);
INKError MgmtRecordSetInt(const char *rec_name, MgmtInt int_val, INKActionNeedT * action_need);
INKError MgmtRecordSetCounter(const char *rec_name, MgmtIntCounter counter_val, INKActionNeedT *action_need);
INKError MgmtRecordSetFloat(const char *rec_name, MgmtFloat float_val, INKActionNeedT * action_need);
INKError MgmtRecordSetString(const char *rec_name, const char*string_val, INKActionNeedT * action_need);


/***************************************************************************
 * File Operations
 ***************************************************************************/
INKError ReadFile(INKFileNameT file, char **text, int *size, int *version);
INKError WriteFile(INKFileNameT file, char *text, int size, int version);

/***************************************************************************
 * Events
 ***************************************************************************/

INKError EventSignal(char *event_name, va_list ap);
INKError EventResolve(char *event_name);
INKError ActiveEventGetMlt(LLQ * active_events);
INKError EventIsActive(char *event_name, bool * is_current);
INKError EventSignalCbRegister(char *event_name, INKEventSignalFunc func, void *data);
INKError EventSignalCbUnregister(char *event_name, INKEventSignalFunc func);

/***************************************************************************
 * Snapshots
 ***************************************************************************/
INKError SnapshotTake(char *snapshot_name);
INKError SnapshotRestore(char *snapshot_name);
INKError SnapshotRemove(char *snapshot_name);
INKError SnapshotGetMlt(LLQ * snapshots);

INKError StatsReset();

/***************************************************************************
 * Miscellaneous Utility
 ***************************************************************************/
INKError EncryptToFile(const char *passwd, const char *filepath);

/*-------------------------------------------------------------
 * rmserver.cfg
 *-------------------------------------------------------------*/

/*  Define the lists in rmserver.cfg  */
#define RM_LISTTAG_PROXY      "Name=\"Proxy\""
#define RM_LISTTAG_PNA_RDT    "Name=\"PNARedirector\""
#define RM_LISTTAG_SCU_ADMIN  "Name=\"SecureAdmin\""
#define RM_LISTTAG_CNN_REALM  "Name=\"ConnectRealm\""
#define RM_LISTTAG_ADMIN_FILE "Name=\"RealSystem Administrator Files\""
#define RM_LISTTAG_AUTH       "Name=\"Authority\""
#define RM_LISTTAG_END        "</List>"

/* define the configurable Var of rmserver.cfg */
#define RM_ADMIN_PORT      "AdminPort"
#define RM_PNA_PORT        "PNAPort"
#define RM_MAX_PROXY_CONN  "MaxProxyConnections"
#define RM_MAX_GWBW        "MaxGatewayBandwidth"
#define RM_MAX_PXBW        "MaxProxyBandwidth"
#define RM_PNA_RDT_PORT    "ListenPort"
#define RM_PNA_RDT_IP      "ProxyHost"
#define RM_IP              "Address"
#define RM_REALM           "Realm"

typedef enum
{
  INK_RM_LISTTAG_SCU_ADMIN,
  INK_RM_LISTTAG_CNN_REALM,
  INK_RM_LISTTAG_ADMIN_FILE,
  INK_RM_LISTTAG_AUTH,
  INK_RM_LISTTAG_PROXY,
  INK_RM_LISTTAG_RTSP_RDT,
  INK_RM_LISTTAG_PNA_RDT,
  INK_RM_LISTTAG_IP
} INKRmServerListT;

typedef enum
{
  INK_RM_RULE_ADMIN_PORT = 0,
  INK_RM_RULE_SCU_ADMIN_REALM,
  INK_RM_RULE_CNN_REALM,
  INK_RM_RULE_ADMIN_FILE_REALM,
  INK_RM_RULE_PNA_PORT,
  INK_RM_RULE_MAX_PROXY_CONN,
  INK_RM_RULE_MAX_GWBW,
  INK_RM_RULE_MAX_PXBW,
  INK_RM_RULE_AUTH_REALM,
  INK_RM_RULE_PNA_RDT_PORT,
  INK_RM_RULE_PNA_RDT_IP
} INKRmServerRuleT;

INKString RmdeXMLize(INKString XMLline, int *lengthp);
INKString RmXMLize(INKString line, int *lengthp);
INKString GetRmCfgPath();
INKError ReadRmCfgFile(char **);
void RmReadCfgList(Tokenizer * Tp, tok_iter_state * Tstate, char **, INKRmServerListT ListType);
INKError WriteRmCfgFile(char *text);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
