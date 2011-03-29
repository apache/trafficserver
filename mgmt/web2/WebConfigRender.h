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
#include "WebHttpContext.h"

#include "P_RecCore.h"

int writeCacheConfigTable(WebHttpContext * whc);
int writeCacheRuleList(textBuffer * output);
int writeCacheConfigForm(WebHttpContext * whc);

int writeHostingConfigTable(WebHttpContext * whc);
int writeHostingRuleList(textBuffer * output);
int writeHostingConfigForm(WebHttpContext * whc);

int writeIcpConfigTable(WebHttpContext * whc);
int writeIcpRuleList(textBuffer * output);
int writeIcpConfigForm(WebHttpContext * whc);

int writeIpAllowConfigTable(WebHttpContext * whc);
int writeIpAllowRuleList(textBuffer * output);
int writeIpAllowConfigForm(WebHttpContext * whc);

int writeMgmtAllowConfigTable(WebHttpContext * whc);
int writeMgmtAllowRuleList(textBuffer * output);
int writeMgmtAllowConfigForm(WebHttpContext * whc);

int writeParentConfigTable(WebHttpContext * whc);
int writeParentRuleList(textBuffer * output);
int writeParentConfigForm(WebHttpContext * whc);

int writeVolumeConfigTable(WebHttpContext * whc);
int writeVolumeRuleList(textBuffer * output);
int writeVolumeConfigForm(WebHttpContext * whc);

int writeRemapConfigTable(WebHttpContext * whc);
int writeRemapRuleList(textBuffer * output);
int writeRemapConfigForm(WebHttpContext * whc);

int writeSocksConfigTable(WebHttpContext * whc);
int writeSocksRuleList(textBuffer * output);
int writeSocksConfigForm(WebHttpContext * whc);

int writeSplitDnsConfigTable(WebHttpContext * whc);
int writeSplitDnsRuleList(textBuffer * output);
int writeSplitDnsConfigForm(WebHttpContext * whc);

int writeUpdateConfigTable(WebHttpContext * whc);
int writeUpdateRuleList(textBuffer * output);
int writeUpdateConfigForm(WebHttpContext * whc);

int writeVaddrsConfigTable(WebHttpContext * whc);
int writeVaddrsRuleList(textBuffer * output);
int writeVaddrsConfigForm(WebHttpContext * whc);

int writeSecondarySpecsForm(WebHttpContext * whc, TSFileNameT file);
int writeSecondarySpecsTableElem(textBuffer * output, char *time, char *src_ip, char *prefix, char *suffix, char *port,
                                 char *method, char *scheme);


// -------------------- CONVERSION FUNCTIONS ------------------------------

int convert_cache_ele_to_html_format(TSCacheEle * ele,
                                     char *ruleType,
                                     char *pdType,
                                     char *time,
                                     char *src_ip,
                                     char *prefix,
                                     char *suffix,
                                     char *port, char *method, char *scheme, char *time_period);

int convert_hosting_ele_to_html_format(TSHostingEle * ele, char *pdType, char *volumes);

int convert_icp_ele_to_html_format(TSIcpEle * ele,
                                   char *name,
                                   char *host_ip,
                                   char *peer_type,
                                   char *proxy_port, char *icp_port, char *mc_state, char *mc_ip, char *mc_ttl);

int convert_ip_allow_ele_to_html_format(TSIpAllowEle * ele, char *src_ip, char *action);

int convert_mgmt_allow_ele_to_html_format(TSMgmtAllowEle * ele, char *src_ip, char *action);

int convert_parent_ele_to_html_format(TSParentProxyEle * ele,
                                      char *pdType,
                                      char *time,
                                      char *src_ip,
                                      char *prefix,
                                      char *suffix,
                                      char *port,
                                      char *method,
                                      char *scheme, char *parents, char *round_robin, char *direct);

int convert_volume_ele_to_html_format(TSVolumeEle * ele,
                                         char *part_num, char *scheme, char *size, char *size_fmt);

int convert_remap_ele_to_html_format(TSRemapEle * ele,
                                     char *rule_type,
                                     char *from_scheme, char *from_port, char *from_path,
                                     char *to_scheme, char *to_port, char *to_path);

int convert_socks_ele_to_html_format(TSSocksEle * ele,
                                     char *rule_type, char *dest_ip, char *user, char *passwd, char *servers, char *rr);

int convert_split_dns_ele_to_html_format(TSSplitDnsEle * ele,
                                         char *pdType, char *dns_server, char *def_domain, char *search_list);

int convert_update_ele_to_html_format(TSUpdateEle * ele, char *hdrs, char *offset, char *interval, char *depth);

int convert_virt_ip_addr_ele_to_html_format(TSVirtIpAddrEle * ele, char *ip, char *sub_intr);

int convert_pdss_to_html_format(TSPdSsFormat info,
                                char *pdType,
                                char *time,
                                char *src_ip,
                                char *prefix, char *suffix, char *port, char *method, char *scheme);

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
void writeSchemeSelect_volume(textBuffer * html, const char *listName);
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
