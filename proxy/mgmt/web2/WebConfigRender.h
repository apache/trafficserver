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
 *
 *  WebConfigRender.h - html rendering and assembly for the Configuration
 *                      File Editor
 *
 *
 ****************************************************************************/

#ifndef _WEB_CONFIG_RENDER_H_
#define _WEB_CONFIG_RENDER_H_

#include "TextBuffer.h"

#include "P_RecCore.h"

int writeCacheRuleList(textBuffer * output);

int writeHostingRuleList(textBuffer * output);

int writeIcpRuleList(textBuffer * output);

int writeIpAllowRuleList(textBuffer * output);

int writeParentRuleList(textBuffer * output);

int writePartitionRuleList(textBuffer * output);

int writeRemapRuleList(textBuffer * output);

int writeSocksRuleList(textBuffer * output);

int writeSplitDnsRuleList(textBuffer * output);

int writeUpdateRuleList(textBuffer * output);

int writeVaddrsRuleList(textBuffer * output);

int writeSecondarySpecsTableElem(textBuffer * output, char *time, char *src_ip, char *prefix, char *suffix, char *port,
                                 char *method, char *scheme, char *mixt);


// -------------------- CONVERSION FUNCTIONS ------------------------------

//------------------------- SELECT FUNCTIONS ------------------------------

void writeRuleTypeSelect_cache(textBuffer * html, const char *listName);
void writeRuleTypeSelect_remap(textBuffer * html, const char *listName);
void writeRuleTypeSelect_socks(textBuffer * html, const char *listName);
void writeRuleTypeSelect_bypass(textBuffer * html, const char *listName);
void writeConnTypeSelect(textBuffer * html, const char *listName);
void writeIpActionSelect(textBuffer * html, const char *listName);
void writePdTypeSelect(textBuffer * html, const char *listName);
void writePdTypeSelect_hosting(textBuffer * html, const char *listName);
void writePdTypeSelect_splitdns(textBuffer * html, const char *listName);
void writeMethodSelect(textBuffer * html, const char *listName);
void writeMethodSelect_push(textBuffer * html, const char *listName);
void writeSchemeSelect(textBuffer * html, const char *listName);
void writeSchemeSelect_partition(textBuffer * html, const char *listName);
void writeSchemeSelect_remap(textBuffer * html, const char *listName);
void writeHeaderTypeSelect(textBuffer * html, const char *listName);
void writeCacheTypeSelect(textBuffer * html, const char *listName);
void writeMcTtlSelect(textBuffer * html, const char *listName);
void writeOnOffSelect(textBuffer * html, const char *listName);
void writeDenySelect(textBuffer * html, const char *listName);
void writeClientGroupTypeSelect(textBuffer * html, const char *listName);
void writeAccessTypeSelect(textBuffer * html, const char *listName);
void writeTreatmentTypeSelect(textBuffer * html, const char *listName);
void writeRoundRobinTypeSelect(textBuffer * html, const char *listName);
void writeRoundRobinTypeSelect_notrue(textBuffer * html, const char *listName);
void writeTrueFalseSelect(textBuffer * html, const char *listName);
void writeSizeFormatSelect(textBuffer * html, const char *listName);
void writeProtocolSelect(textBuffer * html, const char *listName);

#endif // _WEB_CONFIG_RENDER_H_
