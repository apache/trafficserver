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
 * Filename: CfgContextUtils.h (based on FileOp.h)
 * ------------------------------------------------------------------------
 * Purpose:
 * 1) Contains helper functions to convert "values" from TokenList to Ele
 * format and from Ele Format to file Rule Format
 * 2) contains helper functions to check for valid info
 *
 ***************************************************************************/

#ifndef _CONTEXT_UTILS_H_
#define _CONTEXT_UTILS_H_

#include "CfgContextDefs.h"
#include "mgmtapi.h"
#include "GenericParser.h"
#include "CfgContextImpl.h"

/***************************************************************************
 * Conversion Functions
 ***************************************************************************/
/* INKIpAddrEle <==> (single type)  ip_a/cidr_a
 *                   (range type)   ip_a/cidr_a-ip_b/cidr_b */
TSIpAddrEle *string_to_ip_addr_ele(const char *str);
char *ip_addr_ele_to_string(TSIpAddrEle *ele);

/* INKIpAddr <==> ip_addr_string */
char *ip_addr_to_string(TSIpAddr ip);
TSIpAddr string_to_ip_addr(const char *str);

/* IpAddrList <==> ip_addr1, ip_addr2, ip_addr3, ... */
char *ip_addr_list_to_string(IpAddrList *list, const char *delimiter);
TSIpAddrList string_to_ip_addr_list(const char *str_list, const char *delimiter);

/* PortList <==> port_1<delim>port_2<delim>port_3<delim>...<delim>port_n
 * (Note: each port can be a port range */
char *port_list_to_string(PortList *ports, const char *delimiter);
TSPortList string_to_port_list(const char *str_list, const char *delimiter);

/* TSPortEle <==> port_a / port_a#port_b */
char *port_ele_to_string(TSPortEle *ele);
TSPortEle *string_to_port_ele(const char *str);

/* TSProxyList <==> proxy_1, proxy_2,... */
// char* proxy_list_to_string(ProxyList *list);

/* TSStringList(of char*'s)==> elem1<delimiter>elem2<delimiter>elem3 */
char *string_list_to_string(TSStringList list, const char *delimiter);
TSStringList string_to_string_list(const char *str, const char *delimiter);

/* TSIntList(of char*'s)==> elem1<delimiter>elem2<delimiter>elem3 */
char *int_list_to_string(TSIntList list, const char *delimiter);
TSIntList string_to_int_list(const char *str, const char *delimiter);

/* TSDomain */
TSDomain *string_to_domain(const char *str);
char *domain_to_string(TSDomain *domain);

/* TSDomainList */
TSDomainList string_to_domain_list(const char *str_list, const char *delimiter);
char *domain_list_to_string(TSDomainList list, const char *delimiter);

/* pd, pd_val, TSSspec ==> <pd_type>#<pd_value>#<sspecs> */
char *pdest_sspec_to_string(TSPrimeDestT pd, char *prim_dest_val, TSSspec *sspec);
/* <pd_type>#<pd_value>#<sspecs> ==> TSPdSsFormat */
TSMgmtError string_to_pdss_format(const char *str, TSPdSsFormat *pdss);

/* ?h?m?s <==> TSHmsTime */
char *hms_time_to_string(TSHmsTime time);
TSMgmtError string_to_hms_time(const char *str, TSHmsTime *time);

/* string ==> time struct */
TSMgmtError string_to_time_struct(const char *str, TSSspec *sspec);

/* string ==> TSHdrT */
TSHdrT string_to_header_type(const char *str);
char *header_type_to_string(TSHdrT hdr);

/* TSSchemeT <==> string */
TSSchemeT string_to_scheme_type(const char *scheme);
char *scheme_type_to_string(TSSchemeT scheme);

/* TSMethodT <==> string */
TSMethodT string_to_method_type(const char *method);
char *method_type_to_string(TSMethodT method);

/* TSConnectT <==> string */
char *connect_type_to_string(TSConnectT conn);
TSConnectT string_to_connect_type(const char *conn);

/* TSMcTtlt <==> string */
char *multicast_type_to_string(TSMcTtlT mc);

/* TSRrT <==> string */
TSRrT string_to_round_robin_type(const char *rr);
char *round_robin_type_to_string(TSRrT rr);

/* TSFileNameT <==> string */
const char *filename_to_string(TSFileNameT file);

TSCongestionSchemeT string_to_congest_scheme_type(const char *scheme);

TSAccessT string_to_admin_acc_type(const char *access);
char *admin_acc_type_to_string(TSAccessT access);

/***************************************************************************
 * Tokens-to-Struct Conversion Functions
 ***************************************************************************/
Token *tokens_to_pdss_format(TokenList *tokens, Token *first_tok, TSPdSsFormat *pdss);

/***************************************************************************
 * Validation Functions
 ***************************************************************************/
bool isNumber(const char *strNum);
bool ccu_checkIpAddr(const char *addr, const char *min_addr = "0.0.0.0", const char *max_addr = "255.255.255.255");
bool ccu_checkIpAddrEle(TSIpAddrEle *ele);
bool ccu_checkPortNum(int port);
bool ccu_checkPortEle(TSPortEle *ele);
bool ccu_checkPdSspec(TSPdSsFormat pdss);
bool ccu_checkUrl(char *url);
bool ccu_checkTimePeriod(TSSspec *sspec);

char *chopWhiteSpaces_alloc(char *str);

/***************************************************************************
 * General Helper Functions
 ***************************************************************************/
CfgEleObj *create_ele_obj_from_rule_node(Rule *rule);
CfgEleObj *create_ele_obj_from_ele(TSCfgEle *ele);
TSRuleTypeT get_rule_type(TokenList *token_list, TSFileNameT file);

/***************************************************************************
 * Copy Helper Functions
 ***************************************************************************/
// these are mainly used by the C++ CfgEleObj subclasses when they need
// to make copies of their m_ele data class member

void copy_cfg_ele(TSCfgEle *src_ele, TSCfgEle *dst_ele);
void copy_sspec(TSSspec *src, TSSspec *dst);
void copy_pdss_format(TSPdSsFormat *src_pdss, TSPdSsFormat *dst_pdss);
void copy_hms_time(TSHmsTime *src, TSHmsTime *dst);
TSIpAddrEle *copy_ip_addr_ele(TSIpAddrEle *src_ele);
TSPortEle *copy_port_ele(TSPortEle *src_ele);
TSDomain *copy_domain(TSDomain *src_dom);

TSIpAddrList copy_ip_addr_list(TSIpAddrList list);
TSPortList copy_port_list(TSPortList list);
TSDomainList copy_domain_list(TSDomainList list);
TSStringList copy_string_list(TSStringList list);
TSIntList copy_int_list(TSIntList list);

TSCacheEle *copy_cache_ele(TSCacheEle *ele);
TSCongestionEle *copy_congestion_ele(TSCongestionEle *ele);
TSHostingEle *copy_hosting_ele(TSHostingEle *ele);
TSIcpEle *copy_icp_ele(TSIcpEle *ele);
TSIpAllowEle *copy_ip_allow_ele(TSIpAllowEle *ele);
TSLogFilterEle *copy_log_filter_ele(TSLogFilterEle *ele);
TSLogFormatEle *copy_log_format_ele(TSLogFormatEle *ele);
TSLogObjectEle *copy_log_object_ele(TSLogObjectEle *ele);
TSParentProxyEle *copy_parent_proxy_ele(TSParentProxyEle *ele);
TSVolumeEle *copy_volume_ele(TSVolumeEle *ele);
TSPluginEle *copy_plugin_ele(TSPluginEle *ele);
TSRemapEle *copy_remap_ele(TSRemapEle *ele);
TSSocksEle *copy_socks_ele(TSSocksEle *ele);
TSSplitDnsEle *copy_split_dns_ele(TSSplitDnsEle *ele);
TSStorageEle *copy_storage_ele(TSStorageEle *ele);
TSVirtIpAddrEle *copy_virt_ip_addr_ele(TSVirtIpAddrEle *ele);
INKCommentEle *copy_comment_ele(INKCommentEle *ele);

/***************************************************************************
 * Functions needed by implementation but must be hidden from user
 ***************************************************************************/
INKCommentEle *comment_ele_create(char *comment);
void comment_ele_destroy(INKCommentEle *ele);

#endif
