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

/*****************************************************************************
 * Filename: InkMgmtAPIStub.cc
 * Purpose: This file implements the management api stub functions
 * Created: 12/17/00
 * Created by: Eric Wong
 *
 ***************************************************************************/

#include "mgmtapi.h"

/***************************************************************************
 * API Helper Functions for Data Carrier Structures
 ***************************************************************************/

/*--- INKList operations --------------------------------------------------*/
inkapi INKList
INKListCreate(void)
{
  return NULL;
}

inkapi void
INKListDestroy(INKList l)
{
  return;
}

inkapi INKError
INKListEnqueue(INKList l, void *data)
{
  return INK_ERR_OKAY;
}

inkapi void *
INKListDequeue(INKList l)
{
  return NULL;
}

inkapi bool
INKListIsEmpty(INKList l)
{
  return true;
}

inkapi int
INKListLen(INKList l)
{
  return 0;
}

/*--- INKIpAddr operations ------------------------------------------------*/
inkapi INKIpAddrList
INKIpAddrListCreate(void)
{
  return NULL;
}

inkapi void
INKIpAddrListDestroy(INKIpAddrList ip_addrl)
{
  return;
}

inkapi INKError
INKIpAddrListEnqueue(INKIpAddrList ip_addrl, INKIpAddrEle * ip_addr)
{
  return INK_ERR_OKAY;
}

inkapi INKIpAddrEle *
INKIpAddrListDequeue(INKIpAddrList ip_addrl)
{
  return NULL;
}

inkapi int
INKIpAddrListLen(INKIpAddrList ip_addrl)
{
  return 0;
}

inkapi bool
INKIpAddrListIsEmpty(INKIpAddrList ip_addrl)
{
  return true;
}

/*--- INKPortList operations ----------------------------------------------*/
inkapi INKPortList
INKPortListCreate()
{
  return NULL;
}

inkapi void
INKPortListDestroy(INKPortList portl)
{
  return;
}

inkapi INKError
INKPortListEnqueue(INKPortList portl, INKPortEle * port)
{
  return INK_ERR_OKAY;
}

inkapi INKPortEle *
INKPortListDequeue(INKPortList portl)
{
  return NULL;
}

inkapi int
INKPortListLen(INKPortList portl)
{
  return 0;
}

inkapi bool
INKPortListIsEmpty(INKPortList portl)
{
  return true;
}

/*--- INKStringList operations --------------------------------------------*/
inkapi INKStringList
INKStringListCreate()
{
  return NULL;
}

inkapi void
INKStringListDestroy(INKStringList strl)
{
  return;
}

inkapi INKError
INKStringListEnqueue(INKStringList strl, char *wildmat_ele)
{
  return INK_ERR_OKAY;
}

inkapi char *
INKStringListDequeue(INKStringList strl)
{
  return NULL;
}

inkapi bool
INKStringListIsEmpty(INKStringList strl)
{
  return true;
}

inkapi int
INKStringListLen(INKStringList strl)
{
  return 0;
}

/*--- INKDomainList operations --------------------------------------------*/
inkapi INKDomainList
INKDomainListCreate()
{
  return NULL;
}

inkapi void
INKDomainListDestroy(INKDomainList domainl)
{
  return;
}

inkapi INKError
INKDomainListEnqueue(INKDomainList domainl, INKDomain * domain)
{
  return INK_ERR_OKAY;
}

inkapi INKDomain *
INKDomainListDequeue(INKDomainList domainl)
{
  return NULL;
}

inkapi bool
INKDomainListIsEmpty(INKDomainList domainl)
{
  return true;
}

inkapi int
INKDomainListLen(INKDomainList domainl)
{
  return 0;
}

/*--- allocate/deallocate operations --------------------------------------*/
inkapi INKRecordEle *
INKRecordEleCreate(void)
{
  return NULL;
}

inkapi void
INKRecordEleDestroy(INKRecordEle * ele)
{
  return;
}

inkapi INKIpAddrEle *
INKIpAddrEleCreate(void)
{
  return NULL;
}

inkapi void
INKIpAddrEleDestroy(INKIpAddrEle * ele)
{
  return;
}

inkapi INKPortEle *
INKPortEleCreate(void)
{
  return NULL;
}

inkapi void
INKPortEleDestroy(INKPortEle * ele)
{
  return;
}

inkapi INKDomain *
INKDomainCreate()
{
  return NULL;
}

inkapi void
INKDomainDestroy(INKDomain * ele)
{
  return;
}

inkapi INKSspec *
INKSspecCreate(void)
{
  return NULL;
}

inkapi void
INKSspecDestroy(INKSspec * ele)
{
  return;
}

inkapi INKPdSsFormat *
INKPdSsFormatCreate(void)
{
  return NULL;
}

inkapi void
INKPdSsFormatDestroy(INKPdSsFormat * ele)
{
  return;
}

inkapi INKAdminAccessEle *
INKAdminAccessEleCreate()
{
  return NULL;
}

inkapi void
INKAdminAccessEleDestroy(INKAdminAccessEle * ele)
{
  return;
}

inkapi INKArmSecurityEle *
INKArmSecurityEleCreate()
{
  return NULL;
}

inkapi void
INKArmSecurityEleDestroy(INKArmSecurityEle * ele)
{
  return;
}

inkapi INKBypassEle *
INKBypassEleCreate()
{
  return NULL;
}

inkapi void
INKBypassEleDestroy(INKBypassEle * ele)
{
  return;
}

inkapi INKCacheEle *
INKCacheEleCreate()
{
  return NULL;
}

inkapi void
INKCacheEleDestroy(INKCacheEle * ele)
{
  return;
}

inkapi INKCongestionEle *
INKCongestionEleCreate()
{
  return NULL;
}

inkapi void
INKCongestionEleDestroy(INKCongestionEle * ele)
{
  return;
}

inkapi INKHostingEle *
INKHostingEleCreate()
{
  return NULL;
}

inkapi void
INKHostingEleDestroy(INKHostingEle * ele)
{
  return;
}

inkapi INKIcpEle *
INKIcpEleCreate()
{
  return NULL;
}

inkapi void
INKIcpEleDestory(INKIcpEle * ele)
{
  return;
}

inkapi INKLogFilterEle *
INKLogFilterEleCreate()
{
  return NULL;
}

inkapi void
INKLogFilterEleDestroy(INKLogFilterEle * ele)
{
  return;
}

inkapi INKLogFormatEle *
INKLogFormatEleCreate()
{
  return NULL;
}

inkapi void
INKLogFormatEleDestroy(INKLogFormatEle * ele)
{
  return;
}

inkapi INKLogObjectEle *
INKLogObjectEleCreate()
{
  return NULL;
}

inkapi void
INKLogObjectEleDestroy(INKLogObjectEle * ele)
{
  return;
}

inkapi INKParentProxyEle *
INKParentProxyEleCreate()
{
  return NULL;
}

inkapi void
INKParentProxyEleDestroy(INKParentProxyEle * ele)
{
  return;
}

INKRemapEle *
INKRemapEleCreate()
{
  return NULL;
}

void
INKRemapEleDestroy(INKRemapEle * ele)
{
  return;
}

INKSplitDnsEle *
INKSplitDnsEleCreate()
{
  return NULL;
}

void
INKSplitDnsDestroy(INKSplitDnsEle * ele)
{
  return;
}

INKUpdateEle *
INKUpdateEleCreate()
{
  return NULL;
}

void
INKUpdateEleDestroy(INKUpdateEle * ele)
{
  return;
}

INKVirtIpAddrEle *
INKVirtIpAddrEleCreate()
{
  return NULL;
}

void
INKVirtIpAddrEleDestroy(INKVirtIpAddrEle * ele)
{
  return;
}

/***************************************************************************
 * API Core
 ***************************************************************************/

/*--- api initialization and shutdown -------------------------------------*/
inkapi INKError
INKInit()
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKTerminate()
{
  return INK_ERR_OKAY;
}

/*--- network operations --------------------------------------------------*/
inkapi INKError
INKConnect(INKIpAddr ip_addr, int port)
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKDisconnectCbRegister(INKDisconnectFunc * func, void *data)
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKDisconnectRetrySet(int retries, int retry_sleep_msec)
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKDisconnect()
{
  return INK_ERR_OKAY;
}

/*--- control operations --------------------------------------------------*/
inkapi INKProxyStateT
INKProxyStateGet()
{
  return INK_PROXY_ON;
}

inkapi INKError
INKProxyStateSet(INKProxyStateT proxy_state)
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKReconfigure()
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKRestart(bool cluster)
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKHardRestart()
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKActionDo(INKActionNeedT action)
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKBounce(bool cluster)
{
  return INK_ERR_OKAY;
}

/*--- diags output operations ---------------------------------------------*/
inkapi void
INKDiags(INKDiagsT mode, const char *fmt, ...)
{
  return;
}

/*--- direct file operations ----------------------------------------------*/
inkapi INKError
INKConfigFileRead(INKFileNameT file, char **text, int *size, int *version)
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKConfigFileWrite(INKFileNameT file, char *text, int size, int version)
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKReadFromUrl(char *url, char **header, int *headerSize, char **body, int *bodySize)
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKReadFromUrlEx(char *url, char **header, int *headerSize, char **body, int *bodySize, int timeout)
{
  return INK_ERR_OKAY;
}

/*--- snapshot operations -------------------------------------------------*/
inkapi INKError
INKSnapshotTake(char *snapshot_name)
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKSnapshotRestore(char *snapshot_name)
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKSnapshotsGet(INKStringList snapshots)
{
  return INK_ERR_OKAY;
}

/*--- variable operations -------------------------------------------------*/
inkapi INKError
INKRecordGet(char *rec_name, INKRecordEle * rec_val)
{
  return INK_ERR_OKAY;
}

INKError
INKRecordGetInt(char *rec_name, INKInt * int_val)
{
  return INK_ERR_OKAY;
}

INKError
INKRecordGetCounter(char *rec_name, INKCounter * counter_val)
{
  return INK_ERR_OKAY;
}

INKError
INKRecordGetFloat(char *rec_name, INKFloat * float_val)
{
  return INK_ERR_OKAY;
}

INKError
INKRecordGetString(char *rec_name, INKString * string_val)
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKRecordGetMlt(INKList rec_list)
{
  return INK_ERR_OKAY;
}


inkapi INKError
INKRecordSet(char *rec_name, INKString val, INKActionNeedT * action_need)
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKRecordSetInt(char *rec_name, INKInt int_val, INKActionNeedT * action_need)
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKRecordSetCounter(char *rec_name, INKCounter counter_val, INKActionNeedT * action_nee)
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKRecordSetFloat(char *rec_name, INKFloat float_val, INKActionNeedT * action_nee)
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKRecordSetString(const char *rec_name, const char *str_val, INKActionNeedT * action_nee)
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKRecordSetMlt(INKList rec_list, INKActionNeedT * action_need)
{
  return INK_ERR_OKAY;
}


/*--- alarms --------------------------------------------------------------*/
inkapi INKError
INKAlarmResolve(char *alarm_name)
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKActiveAlarmGetMlt(INKList active_alarms)
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKAlarmIsActive(char *alarm_name, bool * is_current)
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKAlarmSignalCbRegister(char *alarm_name, INKAlarmSignalFunc * func, void *data)
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKAlarmSignalCbUnregister(char *alarm_name, INKAlarmSignalFunc * func)
{
  return INK_ERR_OKAY;
}


/*--- abstracted file operations ------------------------------------------*/

inkapi INKCfgContext
INKCfgContextCreate(INKFileNameT file)
{
  return NULL;
}

inkapi INKError
INKCfgContextDestroy(INKCfgContext ctx)
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKCfgContextCommit(INKCfgContext ctx, INKActionNeedT * action_need)
{
  return INK_ERR_OKAY;
}

inkapi INKError
INKCfgContextGet(INKCfgContext ctx)
{
  return INK_ERR_OKAY;
}

/*--- INKCfgContext Operations ---------------------------------------------*/
int
INKCfgContextGetCount(INKCfgContext ctx)
{
  return 0;
}

INKCfgEle *
INKCfgContextGetEleAt(INKCfgContext ctx, int index)
{
  return NULL;
}

INKCfgEle *
INKCfgContextGetFirst(INKCfgContext ctx, INKCfgIterState * state)
{
  return NULL;
}

INKCfgEle *
INKCfgContextGetNext(INKCfgContext ctx, INKCfgIterState * state)
{
  return NULL;
}

INKError
INKCfgContextMoveEleUp(INKCfgContext ctx, int index)
{
  return INK_ERR_OKAY;
}

INKError
INKCfgContextMoveEleDown(INKCfgContext ctx, int index)
{
  return INK_ERR_OKAY;
}

INKError
INKCfgContextAppendEle(INKCfgContext ctx, INKCfgEle * ele)
{
  return INK_ERR_OKAY;
}

INKError
INKCfgContextInsertEleAt(INKCfgContext ctx, INKCfgEle * ele, int index)
{
  return INK_ERR_OKAY;
}

INKError
INKCfgContextRemoveEleAt(INKCfgContext ctx, int index)
{
  return INK_ERR_OKAY;
}

bool
INKIsValid(INKCfgEle * ele)
{
  return true;
}

