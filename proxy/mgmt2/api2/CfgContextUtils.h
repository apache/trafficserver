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
#include "INKMgmtAPI.h"
#include "GenericParser.h"
#include "CfgContextImpl.h"

/***************************************************************************
 * Conversion Functions
 ***************************************************************************/
/* INKIpAddrEle <==> (single type)  ip_a/cidr_a
 *                   (range type)   ip_a/cidr_a-ip_b/cidr_b */
INKIpAddrEle *string_to_ip_addr_ele(const char *str);
char *ip_addr_ele_to_string(INKIpAddrEle * ele);

/* INKIpAddr <==> ip_addr_string */
char *ip_addr_to_string(INKIpAddr ip);
INKIpAddr string_to_ip_addr(const char *str);

/* IpAddrList <==> ip_addr1, ip_addr2, ip_addr3, ... */
char *ip_addr_list_to_string(IpAddrList * list, const char *delimiter);
INKIpAddrList string_to_ip_addr_list(const char *str_list, const char *delimiter);

/* PortList <==> port_1<delim>port_2<delim>port_3<delim>...<delim>port_n 
 * (Note: each port can be a port range */
char *port_list_to_string(PortList * ports, const char *delimiter);
INKPortList string_to_port_list(const char *str_list, const char *delimiter);

/* INKPortEle <==> port_a / port_a#port_b */
char *port_ele_to_string(INKPortEle * ele);
INKPortEle *string_to_port_ele(const char *str);

/* INKProxyList <==> proxy_1, proxy_2,... */
//char* proxy_list_to_string(ProxyList *list);

/* INKStringList(of char*'s)==> elem1<delimiter>elem2<delimiter>elem3 */
char *string_list_to_string(INKStringList list, const char *delimiter);
INKStringList string_to_string_list(const char *str, const char *delimiter);

/* INKIntList(of char*'s)==> elem1<delimiter>elem2<delimiter>elem3 */
char *int_list_to_string(INKIntList list, const char *delimiter);
INKIntList string_to_int_list(const char *str, const char *delimiter);

/* INKDomain */
INKDomain *string_to_domain(const char *str);
char *domain_to_string(INKDomain * domain);

/* INKDomainList */
INKDomainList string_to_domain_list(const char *str_list, const char *delimiter);
char *domain_list_to_string(INKDomainList list, const char *delimiter);

/* pd, pd_val, INKSspec ==> <pd_type>#<pd_value>#<sspecs> */
char *pdest_sspec_to_string(INKPrimeDestT pd, char *prim_dest_val, INKSspec * sspec);
/* <pd_type>#<pd_value>#<sspecs> ==> INKPdSsFormat */
INKError string_to_pdss_format(const char *str, INKPdSsFormat * pdss);

/* ?h?m?s <==> INKHmsTime */
char *hms_time_to_string(INKHmsTime time);
INKError string_to_hms_time(const char *str, INKHmsTime * time);

/* string ==> time struct */
INKError string_to_time_struct(const char *str, INKSspec * sspec);

/* string ==> INKHdrT */
INKHdrT string_to_header_type(const char *str);
char *header_type_to_string(INKHdrT hdr);

/* INKSchemeT <==> string */
INKSchemeT string_to_scheme_type(const char *scheme);
char *scheme_type_to_string(INKSchemeT scheme);

/* INKMethodT <==> string */
INKMethodT string_to_method_type(const char *method);
char *method_type_to_string(INKMethodT method);

/* INKMixtTagT <==> string */
INKMixtTagT string_to_mixt_type(const char *mixt);
char *mixt_type_to_string(INKMixtTagT mixt);

/* INKConnectT <==> string */
char *connect_type_to_string(INKConnectT conn);
INKConnectT string_to_connect_type(const char *conn);

/* INKMcTtlt <==> string */
char *multicast_type_to_string(INKMcTtlT mc);

/* INKRrT <==> string */
INKRrT string_to_round_robin_type(const char *rr);
char *round_robin_type_to_string(INKRrT rr);

/* INKFileNameT <==> string */
char *filename_to_string(INKFileNameT file);

char *nntp_acc_type_to_string(INKNntpAccessT acc);
INKNntpTreatmentT string_to_nntp_treat_type(const char *treat);
INKCongestionSchemeT string_to_congest_scheme_type(const char *scheme);

INKAccessT string_to_admin_acc_type(const char *access);
char *admin_acc_type_to_string(INKAccessT access);

/***************************************************************************
 * Tokens-to-Struct Conversion Functions
 ***************************************************************************/
Token *tokens_to_pdss_format(TokenList * tokens, Token * first_tok, INKPdSsFormat * pdss);


/***************************************************************************
 * Validation Functions
 ***************************************************************************/
bool isNumber(const char *strNum);
bool ccu_checkIpAddr(const char *addr, const char *min_addr = "0.0.0.0", const char *max_addr = "255.255.255.255");
bool ccu_checkIpAddrEle(INKIpAddrEle * ele);
bool ccu_checkPortNum(int port);
bool ccu_checkPortEle(INKPortEle * ele);
bool ccu_checkPdSspec(INKPdSsFormat pdss);
bool ccu_checkUrl(char *url);
bool ccu_checkTimePeriod(INKSspec * sspec);

char *chopWhiteSpaces_alloc(char *str);

/***************************************************************************
 * General Helper Functions
 ***************************************************************************/
CfgEleObj *create_ele_obj_from_rule_node(Rule * rule);
CfgEleObj *create_ele_obj_from_ele(INKCfgEle * ele);
INKRuleTypeT get_rule_type(TokenList * token_list, INKFileNameT file);


/***************************************************************************
 * Copy Helper Functions
 ***************************************************************************/
// these are mainly used by the C++ CfgEleObj subclasses when they need
// to make copies of their m_ele data class member

void copy_cfg_ele(INKCfgEle * src_ele, INKCfgEle * dst_ele);
void copy_sspec(INKSspec * src, INKSspec * dst);
void copy_pdss_format(INKPdSsFormat * src_pdss, INKPdSsFormat * dst_pdss);
void copy_hms_time(INKHmsTime * src, INKHmsTime * dst);
INKIpAddrEle *copy_ip_addr_ele(INKIpAddrEle * src_ele);
INKPortEle *copy_port_ele(INKPortEle * src_ele);
INKDomain *copy_domain(INKDomain * src_dom);

INKIpAddrList copy_ip_addr_list(INKIpAddrList list);
INKPortList copy_port_list(INKPortList list);
INKDomainList copy_domain_list(INKDomainList list);
INKStringList copy_string_list(INKStringList list);
INKIntList copy_int_list(INKIntList list);

INKAdminAccessEle *copy_admin_access_ele(INKAdminAccessEle * ele);
INKCacheEle *copy_cache_ele(INKCacheEle * ele);
INKCongestionEle *copy_congestion_ele(INKCongestionEle * ele);
INKFilterEle *copy_filter_ele(INKFilterEle * ele);
INKFtpRemapEle *copy_ftp_remap_ele(INKFtpRemapEle * ele);
INKHostingEle *copy_hosting_ele(INKHostingEle * ele);
INKIcpEle *copy_icp_ele(INKIcpEle * ele);
INKIpAllowEle *copy_ip_allow_ele(INKIpAllowEle * ele);
INKLogFilterEle *copy_log_filter_ele(INKLogFilterEle * ele);
INKLogFormatEle *copy_log_format_ele(INKLogFormatEle * ele);
INKLogObjectEle *copy_log_object_ele(INKLogObjectEle * ele);
INKMgmtAllowEle *copy_mgmt_allow_ele(INKMgmtAllowEle * ele);
INKNntpAccessEle *copy_nntp_access_ele(INKNntpAccessEle * ele);
INKNntpSrvrEle *copy_nntp_srvr_ele(INKNntpSrvrEle * ele);
INKParentProxyEle *copy_parent_proxy_ele(INKParentProxyEle * ele);
INKPartitionEle *copy_partition_ele(INKPartitionEle * ele);
INKPluginEle *copy_plugin_ele(INKPluginEle * ele);
INKRemapEle *copy_remap_ele(INKRemapEle * ele);
INKSocksEle *copy_socks_ele(INKSocksEle * ele);
INKSplitDnsEle *copy_split_dns_ele(INKSplitDnsEle * ele);
INKStorageEle *copy_storage_ele(INKStorageEle * ele);
INKUpdateEle *copy_update_ele(INKUpdateEle * ele);
INKVirtIpAddrEle *copy_virt_ip_addr_ele(INKVirtIpAddrEle * ele);
INKCommentEle *copy_comment_ele(INKCommentEle * ele);
#ifdef OEM
INKRmServerEle *copy_rmserver_ele(INKRmServerEle * ele);
INKVscanEle *copy_vscan_ele(INKVscanEle * ele);
INKVsTrustedHostEle *copy_vs_trusted_host_ele(INKVsTrustedHostEle * ele);
INKVsExtensionEle *copy_vs_extension_ele(INKVsExtensionEle * ele);
#endif

/***************************************************************************
 * Functions needed by implementation but must be hidden from user
 ***************************************************************************/
INKCommentEle *comment_ele_create(char *comment);
void comment_ele_destroy(INKCommentEle * ele);

#endif
