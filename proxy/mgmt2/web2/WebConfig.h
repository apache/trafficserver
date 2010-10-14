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

/****************************************************************************
 *  WebConfig.h - code to process config file editor requests, and
 *                create responses
 *
 *
 ****************************************************************************/

#ifndef _WEB_CONFIG_H_
#define _WEB_CONFIG_H_

#include "INKMgmtAPI.h"
#include "WebHttpContext.h"

char *convertRules(INKFileNameT file, INKIntList errRules, char *rules[]);

char *formatCacheRule(char *rule);
char *formatHostingRule(char *rule);
char *formatIcpRule(char *rule);
char *formatIpAllowRule(char *rule);
char *formatMgmtAllowRule(char *rule);
char *formatParentRule(char *rule);
char *formatPartitionRule(char *rule);
char *formatRemapRule(char *rule);
char *formatSocksRule(char *rule);
char *formatSplitDnsRule(char *rule);
char *formatUpdateRule(char *rule);
char *formatVaddrsRule(char *rule);

int updateCacheConfig(char *rules[], int numRules, char **errBuff);
int updateHostingConfig(char *rules[], int numRules, char **errBuff);
int updateIcpConfig(char *rules[], int numRules, char **errBuff);
int updateIpAllowConfig(char *rules[], int numRules, char **errBuff);
int updateMgmtAllowConfig(char *rules[], int numRules, char **errBuff);
int updateParentConfig(char *rules[], int numRules, char **errBuff);
int updatePartitionConfig(char *rules[], int numRules, char **errBuff);
int updateRemapConfig(char *rules[], int numRules, char **errBuff);
int updateSocksConfig(char *rules[], int numRules, char **errBuff);
int updateSplitDnsConfig(char *rules[], int numRules, char **errBuff);
int updateUpdateConfig(char *rules[], int numRules, char **errBuff);
int updateVaddrsConfig(char *rules[], int numRules, char **errBuff);

#endif // _WEB_CONFIG_H_
