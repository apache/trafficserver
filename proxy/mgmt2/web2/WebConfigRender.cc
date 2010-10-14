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
 *  WebConfigRender.cc - html rendering/assembly for Config File Editor
 *
 *
 ****************************************************************************/

#include "ink_platform.h"

#include "ink_hash_table.h"
#include "I_Version.h"
#include "SimpleTokenizer.h"

#include "WebConfigRender.h"
#include "WebHttpRender.h"

#include "MgmtUtils.h"
#include "LocalManager.h"

#include "INKMgmtAPI.h"
#include "CfgContextUtils.h"

//-------------------------------------------------------------------------
// Defines
//-------------------------------------------------------------------------

// Get rid of previous definition of MAX_RULE_SIZE.
#undef MAX_RULE_SIZE

#define MAX_RULE_SIZE         512
#define MAX_RULE_PART_SIZE    64
#define BORDER_COLOR          "#cccccc"

//---------------------- TABLE FUNCTIONS ----------------------------------




//-------------------------------------------------------------------------
// writeCacheConfigTable
//-------------------------------------------------------------------------
int
writeCacheConfigTable(WebHttpContext * whc)
{
  INKCacheEle *ele;
  char ruleType[MAX_RULE_PART_SIZE];
  char pdType[MAX_RULE_PART_SIZE];
  char time[MAX_RULE_PART_SIZE];
  char src_ip[MAX_RULE_PART_SIZE];
  char prefix[MAX_RULE_PART_SIZE];
  char suffix[MAX_RULE_PART_SIZE];
  char port[MAX_RULE_PART_SIZE];
  char method[MAX_RULE_PART_SIZE];
  char scheme[MAX_RULE_PART_SIZE];
  char time_period[MAX_RULE_PART_SIZE];
  char mixt[MAX_RULE_PART_SIZE];
  int count;

  textBuffer *output = whc->response_bdy;
  INKCfgContext ctx = INKCfgContextCreate(INK_FNAME_CACHE_OBJ);
  INKError err = INKCfgContextGet(ctx);
  if (err != INK_ERR_OKAY) {
    mgmt_log(stderr, "[writeCacheConfigTable] Error: INKCfgContextGet failed");
    INKCfgContextDestroy(ctx);
    return WEB_HTTP_ERR_FAIL;
  }

  HtmlRndrTableOpen(output, "100%", 1, 0, 0, BORDER_COLOR);

  // write the table headings
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_RULE_TYPE);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_TYPE);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_VALUE);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_TIME_PERIOD);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SECONDARY_SPEC);
  HtmlRndrTdClose(output);

  HtmlRndrTrClose(output);

  count = INKCfgContextGetCount(ctx);
  for (int i = 0; i < count; i++) {
    ele = (INKCacheEle *) INKCfgContextGetEleAt(ctx, i);

    memset(ruleType, 0, MAX_RULE_PART_SIZE);
    memset(pdType, 0, MAX_RULE_PART_SIZE);
    memset(time, 0, MAX_RULE_PART_SIZE);
    memset(src_ip, 0, MAX_RULE_PART_SIZE);
    memset(prefix, 0, MAX_RULE_PART_SIZE);
    memset(suffix, 0, MAX_RULE_PART_SIZE);
    memset(port, 0, MAX_RULE_PART_SIZE);
    memset(method, 0, MAX_RULE_PART_SIZE);
    memset(scheme, 0, MAX_RULE_PART_SIZE);
    memset(time_period, 0, MAX_RULE_PART_SIZE);
    memset(mixt, 0, MAX_RULE_PART_SIZE);
    if (convert_cache_ele_to_html_format(ele, ruleType, pdType, time, src_ip,
                                         prefix, suffix, port, method, scheme,
                                         time_period, mixt) != WEB_HTTP_ERR_OKAY) {
      Debug("config", "[writeCacheConfigTable] invalid Ele, can't format - SKIP");
      continue;                 // invalid ele, so skip to next one
    }
    // write the rule info into the table row
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_CENTER);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(ruleType, strlen(ruleType));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(pdType, strlen(pdType));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(ele->cache_info.pd_val, strlen(ele->cache_info.pd_val));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    if (strlen(time_period) > 0)
      output->copyFrom(time_period, strlen(time_period));
    HtmlRndrTdClose(output);

    writeSecondarySpecsTableElem(output, time, src_ip, prefix, suffix, port, method, scheme, mixt);

    HtmlRndrTrClose(output);
  }                             // end for loop

  // no rules
  if (count == 0) {
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 5);
    HtmlRndrSpace(output, 2);
    HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_NO_RULES);
    HtmlRndrTdClose(output);
    HtmlRndrTrClose(output);
  }

  HtmlRndrTableClose(output);
  INKCfgContextDestroy(ctx);
  return WEB_HTTP_ERR_OKAY;
}


//-------------------------------------------------------------------------
// writeHostingConfigTable
//-------------------------------------------------------------------------
int
writeHostingConfigTable(WebHttpContext * whc)
{
  INKHostingEle *ele;
  char pdType[MAX_RULE_PART_SIZE];
  char partitions[MAX_RULE_PART_SIZE];
  int count;

  textBuffer *output = whc->response_bdy;
  INKCfgContext ctx = INKCfgContextCreate(INK_FNAME_HOSTING);
  INKError err = INKCfgContextGet(ctx);
  if (err != INK_ERR_OKAY) {
    Debug("config", "[writeHostingConfigTable] Error: INKCfgContextGet failed");
    INKCfgContextDestroy(ctx);
    return WEB_HTTP_ERR_FAIL;
  }

  HtmlRndrTableOpen(output, "100%", 1, 0, 0, BORDER_COLOR);

  // write the table headings
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_TYPE);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_VALUE);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PARTITIONS);
  HtmlRndrTdClose(output);

  count = INKCfgContextGetCount(ctx);
  for (int i = 0; i < count; i++) {
    ele = (INKHostingEle *) INKCfgContextGetEleAt(ctx, i);

    memset(pdType, 0, MAX_RULE_PART_SIZE);
    memset(partitions, 0, MAX_RULE_PART_SIZE);
    if (convert_hosting_ele_to_html_format(ele, pdType, partitions) != WEB_HTTP_ERR_OKAY) {
      Debug("config", "[writeHostingConfigTable] invalid Ele, can't format - SKIP");
      continue;                 // invalid ele, so skip to next one
    }
    // write the rule info into the table row
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_CENTER);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(pdType, strlen(pdType));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(ele->pd_val, strlen(ele->pd_val));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    if (strlen(partitions) > 0)
      output->copyFrom(partitions, strlen(partitions));
    HtmlRndrTdClose(output);

    HtmlRndrTrClose(output);
  }                             // end for loop

  // no rules
  if (count == 0) {
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 3);
    HtmlRndrSpace(output, 2);
    HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_NO_RULES);
    HtmlRndrTdClose(output);
    HtmlRndrTrClose(output);
  }

  HtmlRndrTableClose(output);
  INKCfgContextDestroy(ctx);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// writeIcpConfigTable
//-------------------------------------------------------------------------
int
writeIcpConfigTable(WebHttpContext * whc)
{
  INKIcpEle *ele;
  char name[MAX_RULE_PART_SIZE];
  char host_ip[MAX_RULE_PART_SIZE];
  char peer_type[MAX_RULE_PART_SIZE];
  char proxy_port[MAX_RULE_PART_SIZE];
  char icp_port[MAX_RULE_PART_SIZE];
  char mc_state[MAX_RULE_PART_SIZE];
  char mc_ip[MAX_RULE_PART_SIZE];
  char mc_ttl[MAX_RULE_PART_SIZE];
  int count;

  textBuffer *output = whc->response_bdy;
  INKCfgContext ctx = INKCfgContextCreate(INK_FNAME_ICP_PEER);
  INKError err = INKCfgContextGet(ctx);
  if (err != INK_ERR_OKAY) {
    Debug("config", "[writeICPConfigTable] Error: INKCfgContextGet failed");
    INKCfgContextDestroy(ctx);
    return WEB_HTTP_ERR_FAIL;
  }

  HtmlRndrTableOpen(output, "100%", 1, 0, 0, BORDER_COLOR);

  // write the table headings
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PEER_HOST);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PEER_IP);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PEER_TYPE);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PEER_PORT);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_ICP_PORT);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_MCAST_STATE);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_MCAST_IP);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_MCAST_TTL);
  HtmlRndrTdClose(output);

  count = INKCfgContextGetCount(ctx);
  for (int i = 0; i < count; i++) {
    ele = (INKIcpEle *) INKCfgContextGetEleAt(ctx, i);

    memset(name, 0, MAX_RULE_PART_SIZE);
    memset(host_ip, 0, MAX_RULE_PART_SIZE);
    memset(peer_type, 0, MAX_RULE_PART_SIZE);
    memset(proxy_port, 0, MAX_RULE_PART_SIZE);
    memset(icp_port, 0, MAX_RULE_PART_SIZE);
    memset(mc_state, 0, MAX_RULE_PART_SIZE);
    memset(mc_ip, 0, MAX_RULE_PART_SIZE);
    memset(mc_ttl, 0, MAX_RULE_PART_SIZE);
    if (convert_icp_ele_to_html_format(ele, name, host_ip, peer_type, proxy_port, icp_port, mc_state, mc_ip, mc_ttl) !=
        WEB_HTTP_ERR_OKAY) {
      Debug("config", "[writeIcpConfigTable] invalid Ele, can't format - SKIP");
      continue;                 // invalid ele, so skip to next one
    }
    // write the rule info into the table row
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_CENTER);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(name, strlen(name));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(host_ip, strlen(host_ip));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(peer_type, strlen(peer_type));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(proxy_port, strlen(proxy_port));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(icp_port, strlen(icp_port));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(mc_state, strlen(mc_state));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    if (strlen(mc_ip) > 0)
      output->copyFrom(mc_ip, strlen(mc_ip));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    if (strlen(mc_ttl) > 0)
      output->copyFrom(mc_ttl, strlen(mc_ttl));
    HtmlRndrTdClose(output);

    HtmlRndrTrClose(output);
  }                             // end for loop

  // no rules
  if (count == 0) {
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 8);
    HtmlRndrSpace(output, 2);
    HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_NO_RULES);
    HtmlRndrTdClose(output);
    HtmlRndrTrClose(output);
  }

  HtmlRndrTableClose(output);
  INKCfgContextDestroy(ctx);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// writeIpAllowConfigTable
//-------------------------------------------------------------------------
int
writeIpAllowConfigTable(WebHttpContext * whc)
{
  INKIpAllowEle *ele;
  char src_ip[MAX_RULE_PART_SIZE];
  char action[MAX_RULE_PART_SIZE];
  int count;

  textBuffer *output = whc->response_bdy;
  INKCfgContext ctx = INKCfgContextCreate(INK_FNAME_IP_ALLOW);
  INKError err = INKCfgContextGet(ctx);
  if (err != INK_ERR_OKAY) {
    mgmt_log(stderr, "[writeIpAllowConfigTable] Error: INKCfgContextGet failed");
    INKCfgContextDestroy(ctx);
    return WEB_HTTP_ERR_FAIL;
  }

  HtmlRndrTableOpen(output, "100%", 1, 0, 0, BORDER_COLOR);

  // write the table headings
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_IP_ACTION);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SOURCE_IP);
  HtmlRndrTdClose(output);

  HtmlRndrTrClose(output);

  count = INKCfgContextGetCount(ctx);
  for (int i = 0; i < count; i++) {
    ele = (INKIpAllowEle *) INKCfgContextGetEleAt(ctx, i);

    memset(src_ip, 0, MAX_RULE_PART_SIZE);
    memset(action, 0, MAX_RULE_PART_SIZE);

    if (convert_ip_allow_ele_to_html_format(ele, src_ip, action) != WEB_HTTP_ERR_OKAY) {
      Debug("config", "[writeIpAllowConfigTable] invalid Ele, can't format - SKIP");
      continue;                 // invalid ele, so skip to next one
    }
    // write the rule info into the table row
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_CENTER);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(action, strlen(action));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(src_ip, strlen(src_ip));
    HtmlRndrTdClose(output);

    HtmlRndrTrClose(output);
  }                             // end for loop

  // no rules
  if (count == 0) {
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 2);
    HtmlRndrSpace(output, 2);
    HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_NO_RULES);
    HtmlRndrTdClose(output);
    HtmlRndrTrClose(output);
  }

  HtmlRndrTableClose(output);
  INKCfgContextDestroy(ctx);
  return WEB_HTTP_ERR_OKAY;
}



//-------------------------------------------------------------------------
// writeMgmtAllowConfigTable
//-------------------------------------------------------------------------
int
writeMgmtAllowConfigTable(WebHttpContext * whc)
{
  INKMgmtAllowEle *ele;
  char src_ip[MAX_RULE_PART_SIZE];
  char action[MAX_RULE_PART_SIZE];
  int count;

  textBuffer *output = whc->response_bdy;
  INKCfgContext ctx = INKCfgContextCreate(INK_FNAME_MGMT_ALLOW);
  INKError err = INKCfgContextGet(ctx);
  if (err != INK_ERR_OKAY) {
    mgmt_log(stderr, "[writeMgmtAllowConfigTable] Error: INKCfgContextGet failed");
    INKCfgContextDestroy(ctx);
    return WEB_HTTP_ERR_FAIL;
  }

  HtmlRndrTableOpen(output, "100%", 1, 0, 0, BORDER_COLOR);

  // write the table headings
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_IP_ACTION);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SOURCE_IP);
  HtmlRndrTdClose(output);

  HtmlRndrTrClose(output);

  count = INKCfgContextGetCount(ctx);
  for (int i = 0; i < count; i++) {
    ele = (INKMgmtAllowEle *) INKCfgContextGetEleAt(ctx, i);

    memset(src_ip, 0, MAX_RULE_PART_SIZE);
    memset(action, 0, MAX_RULE_PART_SIZE);
    if (convert_mgmt_allow_ele_to_html_format(ele, src_ip, action) != WEB_HTTP_ERR_OKAY) {
      Debug("config", "[writeMgmtAllowConfigTable] invalid Ele, can't format - SKIP");
      continue;                 // invalid ele, so skip to next one
    }
    // write the rule info into the table row
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_CENTER);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(action, strlen(action));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(src_ip, strlen(src_ip));
    HtmlRndrTdClose(output);

    HtmlRndrTrClose(output);
  }                             // end for loop

  // no rules
  if (count == 0) {
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 2);
    HtmlRndrSpace(output, 2);
    HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_NO_RULES);
    HtmlRndrTdClose(output);
    HtmlRndrTrClose(output);
  }

  HtmlRndrTableClose(output);
  INKCfgContextDestroy(ctx);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// writeParentConfigTable
//-------------------------------------------------------------------------
int
writeParentConfigTable(WebHttpContext * whc)
{
  INKParentProxyEle *ele;
  char pdType[MAX_RULE_PART_SIZE];
  char time[MAX_RULE_PART_SIZE];
  char src_ip[MAX_RULE_PART_SIZE];
  char prefix[MAX_RULE_PART_SIZE];
  char suffix[MAX_RULE_PART_SIZE];
  char port[MAX_RULE_PART_SIZE];
  char method[MAX_RULE_PART_SIZE];
  char scheme[MAX_RULE_PART_SIZE];
  char mixt[MAX_RULE_PART_SIZE];
  char parents[MAX_RULE_PART_SIZE];
  char round_robin[MAX_RULE_PART_SIZE];
  char direct[MAX_RULE_PART_SIZE];
  int count;

  textBuffer *output = whc->response_bdy;
  INKCfgContext ctx = INKCfgContextCreate(INK_FNAME_PARENT_PROXY);
  INKError err = INKCfgContextGet(ctx);
  if (err != INK_ERR_OKAY) {
    mgmt_log(stderr, "[writeParentConfigTable] Error: INKCfgContextGet failed");
    INKCfgContextDestroy(ctx);
    return WEB_HTTP_ERR_FAIL;
  }

  HtmlRndrTableOpen(output, "100%", 1, 0, 0, BORDER_COLOR);

  // write the table headings
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_TYPE);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_VALUE);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PARENTS);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_ROUND_ROBIN);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_GO_DIRECT);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SECONDARY_SPEC);
  HtmlRndrTdClose(output);

  HtmlRndrTrClose(output);

  count = INKCfgContextGetCount(ctx);
  for (int i = 0; i < count; i++) {
    ele = (INKParentProxyEle *) INKCfgContextGetEleAt(ctx, i);

    memset(pdType, 0, MAX_RULE_PART_SIZE);
    memset(time, 0, MAX_RULE_PART_SIZE);
    memset(src_ip, 0, MAX_RULE_PART_SIZE);
    memset(prefix, 0, MAX_RULE_PART_SIZE);
    memset(suffix, 0, MAX_RULE_PART_SIZE);
    memset(port, 0, MAX_RULE_PART_SIZE);
    memset(method, 0, MAX_RULE_PART_SIZE);
    memset(scheme, 0, MAX_RULE_PART_SIZE);
    memset(mixt, 0, MAX_RULE_PART_SIZE);
    memset(parents, 0, MAX_RULE_PART_SIZE);
    memset(round_robin, 0, MAX_RULE_PART_SIZE);
    memset(direct, 0, MAX_RULE_PART_SIZE);
    if (convert_parent_ele_to_html_format
        (ele, pdType, time, src_ip, prefix, suffix, port, method, scheme, mixt, parents, round_robin,
         direct) != WEB_HTTP_ERR_OKAY) {
      Debug("config", "[writeParentConfigTable] invalid Ele, can't format - SKIP");
      continue;                 // invalid ele, so skip to next one
    }
    // write the rule info into the table row
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_CENTER);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(pdType, strlen(pdType));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(ele->parent_info.pd_val, strlen(ele->parent_info.pd_val));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    if (strlen(parents) > 0)
      output->copyFrom(parents, strlen(parents));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    if (strlen(round_robin) > 0)
      output->copyFrom(round_robin, strlen(round_robin));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    if (strlen(direct) > 0)
      output->copyFrom(direct, strlen(direct));
    HtmlRndrTdClose(output);

    writeSecondarySpecsTableElem(output, time, src_ip, prefix, suffix, port, method, scheme, mixt);
  }                             // end for loop

  // no rules
  if (count == 0) {
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 6);
    HtmlRndrSpace(output, 2);
    HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_NO_RULES);
    HtmlRndrTdClose(output);
    HtmlRndrTrClose(output);
  }

  HtmlRndrTableClose(output);
  INKCfgContextDestroy(ctx);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// writePartionConfigTable
//-------------------------------------------------------------------------
int
writePartitionConfigTable(WebHttpContext * whc)
{
  // now write each rule as a row in the table
  INKPartitionEle *ele;
  char part_num[MAX_RULE_PART_SIZE];
  char scheme[MAX_RULE_PART_SIZE];
  char size[MAX_RULE_PART_SIZE];
  char size_fmt[MAX_RULE_PART_SIZE];
  int count;

  textBuffer *output = whc->response_bdy;
  INKCfgContext ctx = INKCfgContextCreate(INK_FNAME_PARTITION);
  INKError err = INKCfgContextGet(ctx);
  if (err != INK_ERR_OKAY) {
    Debug("config", "[writePartitionConfigTable] Error: INKCfgContextGet failed");
    INKCfgContextDestroy(ctx);
    return WEB_HTTP_ERR_FAIL;
  }

  HtmlRndrTableOpen(output, "100%", 1, 0, 0, BORDER_COLOR);

  // write the table headings
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PARTITION_NUM);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SCHEME);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PSIZE);
  HtmlRndrTdClose(output);

  count = INKCfgContextGetCount(ctx);
  for (int i = 0; i < count; i++) {
    ele = (INKPartitionEle *) INKCfgContextGetEleAt(ctx, i);

    memset(part_num, 0, MAX_RULE_PART_SIZE);
    memset(scheme, 0, MAX_RULE_PART_SIZE);
    memset(size, 0, MAX_RULE_PART_SIZE);
    memset(size_fmt, 0, MAX_RULE_PART_SIZE);
    if (convert_partition_ele_to_html_format(ele, part_num, scheme, size, size_fmt) != WEB_HTTP_ERR_OKAY) {
      Debug("config", "[writePartitionConfigTable] invalid Ele, can't format - SKIP");
      continue;                 // invalid ele, so skip to next one
    }
    // write the rule info into the table row
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_CENTER);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(part_num, strlen(part_num));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(scheme, strlen(scheme));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(size, strlen(size));
    switch (ele->size_format) {
    case INK_SIZE_FMT_PERCENT:
      output->copyFrom("%", strlen("%"));
      break;
    case INK_SIZE_FMT_ABSOLUTE:
      output->copyFrom(" MB", strlen(" MB"));
      break;
    default:
      // Handled here:
      // INK_SIZE_FMT_UNDEFINED
      break;
    }
    HtmlRndrTdClose(output);

    HtmlRndrTrClose(output);
  }                             // end for loop

  // no rules
  if (count == 0) {
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 3);
    HtmlRndrSpace(output, 2);
    HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_NO_RULES);
    HtmlRndrTdClose(output);
    HtmlRndrTrClose(output);
  }

  HtmlRndrTableClose(output);
  INKCfgContextDestroy(ctx);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// writeRemapConfigTable
//-------------------------------------------------------------------------
int
writeRemapConfigTable(WebHttpContext * whc)
{
  INKRemapEle *ele;
  char rule_type[MAX_RULE_PART_SIZE];
  char from_scheme[MAX_RULE_PART_SIZE];
  char from_port[MAX_RULE_PART_SIZE];
  char from_path[MAX_RULE_PART_SIZE];
  char to_scheme[MAX_RULE_PART_SIZE];
  char to_port[MAX_RULE_PART_SIZE];
  char to_path[MAX_RULE_PART_SIZE];
  char mixt[MAX_RULE_PART_SIZE];

  int count;

  textBuffer *output = whc->response_bdy;
  INKCfgContext ctx = INKCfgContextCreate(INK_FNAME_REMAP);
  INKError err = INKCfgContextGet(ctx);
  if (err != INK_ERR_OKAY) {
    mgmt_log(stderr, "[writeRemapConfigTable] Error: INKCfgContextGet failed");
    INKCfgContextDestroy(ctx);
    return WEB_HTTP_ERR_FAIL;
  }

  HtmlRndrTableOpen(output, "100%", 1, 0, 0, BORDER_COLOR);

  // write the table headings
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_RULE_TYPE);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_FROM_SCHEME);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_FROM_HOST);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_FROM_PORT);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_FROM_PATH);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_TO_SCHEME);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_TO_HOST);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_TO_PORT);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_TO_PATH);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_MIXT_SCHEME);
  HtmlRndrTdClose(output);

  HtmlRndrTrClose(output);

  count = INKCfgContextGetCount(ctx);
  for (int i = 0; i < count; i++) {
    ele = (INKRemapEle *) INKCfgContextGetEleAt(ctx, i);

    memset(rule_type, 0, MAX_RULE_PART_SIZE);
    memset(from_scheme, 0, MAX_RULE_PART_SIZE);
    memset(from_port, 0, MAX_RULE_PART_SIZE);
    memset(from_path, 0, MAX_RULE_PART_SIZE);
    memset(to_scheme, 0, MAX_RULE_PART_SIZE);
    memset(to_port, 0, MAX_RULE_PART_SIZE);
    memset(to_path, 0, MAX_RULE_PART_SIZE);
    memset(mixt, 0, MAX_RULE_PART_SIZE);
    if (convert_remap_ele_to_html_format
        (ele, rule_type, from_scheme, from_port, from_path, to_scheme, to_port, to_path, mixt) != WEB_HTTP_ERR_OKAY) {
      Debug("config", "[writeRemapConfigTable] invalid Ele, can't format - SKIP");
      continue;                 // invalid ele, so skip to next one
    }
    // write the rule info into the table row
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_CENTER);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(rule_type, strlen(rule_type));
    HtmlRndrTdClose(output);

    // from url
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(from_scheme, strlen(from_scheme));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(ele->from_host, strlen(ele->from_host));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(from_port, strlen(from_port));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(from_path, strlen(from_path));
    HtmlRndrTdClose(output);

    // to url
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(to_scheme, strlen(to_scheme));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(ele->to_host, strlen(ele->to_host));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(to_port, strlen(to_port));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(to_path, strlen(to_path));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(mixt, strlen(mixt));
    HtmlRndrTdClose(output);

    HtmlRndrTrClose(output);
  }                             // end for loop

  // no rules
  if (count == 0) {
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 10);
    HtmlRndrSpace(output, 2);
    HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_NO_RULES);
    HtmlRndrTdClose(output);
    HtmlRndrTrClose(output);
  }

  HtmlRndrTableClose(output);
  INKCfgContextDestroy(ctx);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// writeSocksConfigTable
//-------------------------------------------------------------------------
int
writeSocksConfigTable(WebHttpContext * whc)
{
  INKSocksEle *ele;
  char rule_type[MAX_RULE_PART_SIZE];
  char user[MAX_RULE_PART_SIZE];
  char passwd[MAX_RULE_PART_SIZE];
  char servers[MAX_RULE_PART_SIZE];
  char dest_ip[MAX_RULE_PART_SIZE];
  char rr[MAX_RULE_PART_SIZE];

  int count;

  textBuffer *output = whc->response_bdy;
  INKCfgContext ctx = INKCfgContextCreate(INK_FNAME_SOCKS);
  INKError err = INKCfgContextGet(ctx);
  if (err != INK_ERR_OKAY) {
    mgmt_log(stderr, "[writeSocksConfigTable] Error: INKCfgContextGet failed");
    INKCfgContextDestroy(ctx);
    return WEB_HTTP_ERR_FAIL;
  }

  HtmlRndrTableOpen(output, "100%", 1, 0, 0, BORDER_COLOR);

  // write the table headings
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_RULE_TYPE);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_USER);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PASSWORD);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_DEST_IP);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SOCKS_SERVER);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_ROUND_ROBIN);
  HtmlRndrTdClose(output);

  HtmlRndrTrClose(output);

  count = INKCfgContextGetCount(ctx);
  for (int i = 0; i < count; i++) {
    ele = (INKSocksEle *) INKCfgContextGetEleAt(ctx, i);

    memset(rule_type, 0, MAX_RULE_PART_SIZE);
    memset(user, 0, MAX_RULE_PART_SIZE);
    memset(passwd, 0, MAX_RULE_PART_SIZE);
    memset(dest_ip, 0, MAX_RULE_PART_SIZE);
    memset(servers, 0, MAX_RULE_PART_SIZE);
    memset(rr, 0, MAX_RULE_PART_SIZE);
    if (convert_socks_ele_to_html_format(ele, rule_type, dest_ip, user, passwd, servers, rr) != WEB_HTTP_ERR_OKAY) {
      Debug("config", "[writeSocksConfigTable] invalid Ele, can't format - SKIP");
      continue;                 // invalid ele, so skip to next one
    }
    // write the rule info into the table row
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_CENTER);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(rule_type, strlen(rule_type));
    HtmlRndrTdClose(output);

    // username
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(user, strlen(user));
    HtmlRndrTdClose(output);

    // password
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(passwd, strlen(passwd));
    HtmlRndrTdClose(output);

    // dest ips
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(dest_ip, strlen(dest_ip));
    HtmlRndrTdClose(output);

    // socks servers
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(servers, strlen(servers));
    HtmlRndrTdClose(output);

    // round robin
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(rr, strlen(rr));
    HtmlRndrTdClose(output);

    HtmlRndrTrClose(output);
  }                             // end for loop

  // no rules
  if (count == 0) {
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 7);
    HtmlRndrSpace(output, 2);
    HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_NO_RULES);
    HtmlRndrTdClose(output);
    HtmlRndrTrClose(output);
  }

  HtmlRndrTableClose(output);
  INKCfgContextDestroy(ctx);
  return WEB_HTTP_ERR_OKAY;
}


//-------------------------------------------------------------------------
// writeSplitDnsConfigTable
//-------------------------------------------------------------------------
int
writeSplitDnsConfigTable(WebHttpContext * whc)
{
  // now write each rule as a row in the table
  INKSplitDnsEle *ele;
  char pdType[MAX_RULE_PART_SIZE];
  char dns_server[MAX_RULE_PART_SIZE];
  char def_domain[MAX_RULE_PART_SIZE];
  char search_list[MAX_RULE_PART_SIZE];
  int count;

  textBuffer *output = whc->response_bdy;
  INKCfgContext ctx = INKCfgContextCreate(INK_FNAME_SPLIT_DNS);
  INKError err = INKCfgContextGet(ctx);
  if (err != INK_ERR_OKAY) {
    Debug("config", "[writeSplitDnsConfigTable] Error: INKCfgContextGet failed");
    INKCfgContextDestroy(ctx);
    return WEB_HTTP_ERR_FAIL;
  }

  HtmlRndrTableOpen(output, "100%", 1, 0, 0, BORDER_COLOR);

  // write the table headings
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_TYPE);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_VALUE);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_DNS_SERVER_IP);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_DOMAIN_NAME);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SEARCH_LIST);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  count = INKCfgContextGetCount(ctx);
  for (int i = 0; i < count; i++) {
    ele = (INKSplitDnsEle *) INKCfgContextGetEleAt(ctx, i);

    memset(pdType, 0, MAX_RULE_PART_SIZE);
    memset(dns_server, 0, MAX_RULE_PART_SIZE);
    memset(def_domain, 0, MAX_RULE_PART_SIZE);
    memset(search_list, 0, MAX_RULE_PART_SIZE);
    if (convert_split_dns_ele_to_html_format(ele, pdType, dns_server, def_domain, search_list) != WEB_HTTP_ERR_OKAY) {
      Debug("config", "[writeSplitDnsConfigTable] invalid Ele, can't format - SKIP");
      continue;                 // invalid ele, so skip to next one
    }
    // write the rule info into the table row
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_CENTER);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(pdType, strlen(pdType));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(ele->pd_val, strlen(ele->pd_val));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    if (strlen(dns_server) > 0)
      output->copyFrom(dns_server, strlen(dns_server));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    if (strlen(def_domain) > 0)
      output->copyFrom(def_domain, strlen(def_domain));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    if (strlen(search_list) > 0)
      output->copyFrom(search_list, strlen(search_list));
    HtmlRndrTdClose(output);

    HtmlRndrTrClose(output);
  }                             // end for loop

  // no rules
  if (count == 0) {
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 5);
    HtmlRndrSpace(output, 2);
    HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_NO_RULES);
    HtmlRndrTdClose(output);
    HtmlRndrTrClose(output);
  }

  HtmlRndrTableClose(output);
  INKCfgContextDestroy(ctx);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// writeUpdateConfigTable
//-------------------------------------------------------------------------
int
writeUpdateConfigTable(WebHttpContext * whc)
{
  // now write each rule as a row in the table
  INKUpdateEle *ele;
  char hdrs[MAX_RULE_PART_SIZE];
  char offset[MAX_RULE_PART_SIZE];
  char interval[MAX_RULE_PART_SIZE];
  char depth[MAX_RULE_PART_SIZE];
  int count;

  textBuffer *output = whc->response_bdy;
  INKCfgContext ctx = INKCfgContextCreate(INK_FNAME_UPDATE_URL);
  INKError err = INKCfgContextGet(ctx);
  if (err != INK_ERR_OKAY) {
    Debug("config", "[writeUpdateConfigTable] Error: INKCfgContextGet failed");
    INKCfgContextDestroy(ctx);
    return WEB_HTTP_ERR_FAIL;
  }

  HtmlRndrTableOpen(output, "100%", 1, 0, 0, BORDER_COLOR);

  // write the table headings
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_URL);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_REQUEST_HDR);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_OFFSET_HOUR);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_INTERVAL);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_RECUR_DEPTH);
  HtmlRndrTdClose(output);

  HtmlRndrTrClose(output);

  count = INKCfgContextGetCount(ctx);
  for (int i = 0; i < count; i++) {
    ele = (INKUpdateEle *) INKCfgContextGetEleAt(ctx, i);

    memset(hdrs, 0, MAX_RULE_PART_SIZE);
    memset(offset, 0, MAX_RULE_PART_SIZE);
    memset(interval, 0, MAX_RULE_PART_SIZE);
    memset(depth, 0, MAX_RULE_PART_SIZE);
    if (convert_update_ele_to_html_format(ele, hdrs, offset, interval, depth) != WEB_HTTP_ERR_OKAY) {
      Debug("config", "[writeUpdateConfigTable] invalid Ele, can't format - SKIP");
      continue;                 // invalid ele, so skip to next one
    }
    // write the rule info into the table row
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_CENTER);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(ele->url, strlen(ele->url));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    if (strlen(hdrs) > 0)
      output->copyFrom(hdrs, strlen(hdrs));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(offset, strlen(offset));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(interval, strlen(interval));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    if (strlen(depth) > 0)
      output->copyFrom(depth, strlen(depth));
    HtmlRndrTdClose(output);

    HtmlRndrTrClose(output);
  }                             // end for loop

  // no rules
  if (count == 0) {
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 5);
    HtmlRndrSpace(output, 2);
    HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_NO_RULES);
    HtmlRndrTdClose(output);
    HtmlRndrTrClose(output);
  }

  HtmlRndrTableClose(output);
  INKCfgContextDestroy(ctx);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// writeVaddrsConfigTable
//-------------------------------------------------------------------------
int
writeVaddrsConfigTable(WebHttpContext * whc)
{
  // now write each rule as a row in the table
  INKVirtIpAddrEle *ele;
  char ip[MAX_RULE_PART_SIZE];
  char sub_intr[MAX_RULE_PART_SIZE];
  int count;

  textBuffer *output = whc->response_bdy;
  INKCfgContext ctx = INKCfgContextCreate(INK_FNAME_VADDRS);
  INKError err = INKCfgContextGet(ctx);
  if (err != INK_ERR_OKAY) {
    Debug("config", "[writeVaddrsConfigTable] Error: INKCfgContextGet failed");
    INKCfgContextDestroy(ctx);
    return WEB_HTTP_ERR_FAIL;
  }

  HtmlRndrTableOpen(output, "100%", 1, 0, 0, BORDER_COLOR);

  // write the table headings
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_VIRTUAL_IP);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_ETH_INTERFACE);
  HtmlRndrTdClose(output);

  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SUB_INTERFACE);
  HtmlRndrTdClose(output);

  HtmlRndrTrClose(output);

  count = INKCfgContextGetCount(ctx);
  for (int i = 0; i < count; i++) {
    ele = (INKVirtIpAddrEle *) INKCfgContextGetEleAt(ctx, i);

    memset(ip, 0, MAX_RULE_PART_SIZE);
    memset(sub_intr, 0, MAX_RULE_PART_SIZE);
    if (convert_virt_ip_addr_ele_to_html_format(ele, ip, sub_intr) != WEB_HTTP_ERR_OKAY) {
      Debug("config", "[writeVaddrsConfigTable] invalid Ele, can't format - SKIP");
      continue;                 // invalid ele, so skip to next one
    }
    // write the rule info into the table row
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_CENTER);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(ip, strlen(ip));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(ele->intr, strlen(ele->intr));
    HtmlRndrTdClose(output);

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrSpace(output, 2);
    output->copyFrom(sub_intr, strlen(sub_intr));
    HtmlRndrTdClose(output);

    HtmlRndrTrClose(output);
  }                             // end for loop

  // no rules
  if (count == 0) {
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 3);
    HtmlRndrSpace(output, 2);
    HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_NO_RULES);
    HtmlRndrTdClose(output);
    HtmlRndrTrClose(output);
  }

  HtmlRndrTableClose(output);
  INKCfgContextDestroy(ctx);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------- RULE LIST FUNCTIONS --------------------------------





//-------------------------------------------------------------------------
// writeCacheRuleList
//-------------------------------------------------------------------------
int
writeCacheRuleList(textBuffer * output)
{
  INKCacheEle *ele;
  int count, i;
  const char ruleList[] = "var ruleList = new Object();\n";

  INKCfgContext ctx = INKCfgContextCreate(INK_FNAME_CACHE_OBJ);
  INKError err = INKCfgContextGet(ctx);
  if (err != INK_ERR_OKAY) {
    mgmt_log(stderr, "[writeCacheRuleList] Error INKCfgContextGet");
    return WEB_HTTP_ERR_FAIL;
  }

  output->copyFrom(ruleList, strlen(ruleList));

  char rule[MAX_RULE_SIZE];
  char ruleType[MAX_RULE_PART_SIZE];
  char pdType[MAX_RULE_PART_SIZE];
  char time[MAX_RULE_PART_SIZE];
  char src_ip[MAX_RULE_PART_SIZE];
  char prefix[MAX_RULE_PART_SIZE];
  char suffix[MAX_RULE_PART_SIZE];
  char port[MAX_RULE_PART_SIZE];
  char method[MAX_RULE_PART_SIZE];
  char scheme[MAX_RULE_PART_SIZE];
  char time_period[MAX_RULE_PART_SIZE];
  char mixt[MAX_RULE_PART_SIZE];

  count = INKCfgContextGetCount(ctx);
  for (i = 0; i < count; i++) {
    ele = (INKCacheEle *) INKCfgContextGetEleAt(ctx, i);

    memset(ruleType, 0, MAX_RULE_PART_SIZE);
    memset(pdType, 0, MAX_RULE_PART_SIZE);
    memset(time, 0, MAX_RULE_PART_SIZE);
    memset(src_ip, 0, MAX_RULE_PART_SIZE);
    memset(prefix, 0, MAX_RULE_PART_SIZE);
    memset(suffix, 0, MAX_RULE_PART_SIZE);
    memset(port, 0, MAX_RULE_PART_SIZE);
    memset(method, 0, MAX_RULE_PART_SIZE);
    memset(scheme, 0, MAX_RULE_PART_SIZE);
    memset(time_period, 0, MAX_RULE_PART_SIZE);
    memset(mixt, 0, MAX_RULE_PART_SIZE);
    if (convert_cache_ele_to_html_format(ele, ruleType, pdType, time, src_ip,
                                         prefix, suffix, port, method, scheme,
                                         time_period, mixt) != WEB_HTTP_ERR_OKAY) {
      Debug("config", "[writeCacheRuleList] invalid Ele, can't format - SKIP");
      continue;                 // invalid ele, so skip to next one
    }

    memset(rule, 0, MAX_RULE_SIZE);
    snprintf(rule, MAX_RULE_SIZE,
                 "ruleList[%d] = new Rule(\"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\");\n",
                 i, ruleType, pdType, ele->cache_info.pd_val, time, src_ip, prefix, suffix, port, method, scheme,
                 time_period, mixt);

    output->copyFrom(rule, strlen(rule));
  }

  INKCfgContextDestroy(ctx);
  return WEB_HTTP_ERR_OKAY;
}


//-------------------------------------------------------------------------
// writeHostingRuleList
//-------------------------------------------------------------------------
int
writeHostingRuleList(textBuffer * output)
{
  INKHostingEle *ele;
  int count, i;
  const char ruleList[] = "var ruleList = new Object();\n";

  INKCfgContext ctx = INKCfgContextCreate(INK_FNAME_HOSTING);
  INKError err = INKCfgContextGet(ctx);
  if (err != INK_ERR_OKAY) {
    mgmt_log(stderr, "[writeHostingRuleList] Error INKCfgContextGet");
    return WEB_HTTP_ERR_FAIL;
  }

  output->copyFrom(ruleList, strlen(ruleList));

  char rule[MAX_RULE_SIZE];

  char pdType[MAX_RULE_PART_SIZE];
  char partitions[MAX_RULE_PART_SIZE];

  count = INKCfgContextGetCount(ctx);
  for (i = 0; i < count; i++) {
    ele = (INKHostingEle *) INKCfgContextGetEleAt(ctx, i);

    memset(pdType, 0, MAX_RULE_PART_SIZE);
    memset(partitions, 0, MAX_RULE_PART_SIZE);

    if (convert_hosting_ele_to_html_format(ele, pdType, partitions) != WEB_HTTP_ERR_OKAY) {
      Debug("config", "[writeHostingRuleList] invalid Ele, can't format - SKIP");
      continue;                 // invalid ele, so skip to next one
    }

    memset(rule, 0, MAX_RULE_SIZE);
    snprintf(rule, MAX_RULE_SIZE, "ruleList[%d] = new Rule(\"%s\", \"%s\", \"%s\");\n",
                 i, pdType, ele->pd_val, partitions);

    output->copyFrom(rule, strlen(rule));
  }

  INKCfgContextDestroy(ctx);
  return WEB_HTTP_ERR_OKAY;
}



//-------------------------------------------------------------------------
// writeIcpRuleList
//-------------------------------------------------------------------------
int
writeIcpRuleList(textBuffer * output)
{
  INKIcpEle *ele;
  int count, i;
  const char ruleList[] = "var ruleList = new Object();\n";

  INKCfgContext ctx = INKCfgContextCreate(INK_FNAME_ICP_PEER);
  INKError err = INKCfgContextGet(ctx);
  if (err != INK_ERR_OKAY) {
    mgmt_log(stderr, "[writeIcpRuleList] Error INKCfgContextGet");
    return WEB_HTTP_ERR_FAIL;   //goto Lerror;
  }

  output->copyFrom(ruleList, strlen(ruleList));

  char rule[MAX_RULE_SIZE];
  char name[MAX_RULE_PART_SIZE];
  char host_ip[MAX_RULE_PART_SIZE];
  char peer_type[MAX_RULE_PART_SIZE];
  char proxy_port[MAX_RULE_PART_SIZE];
  char icp_port[MAX_RULE_PART_SIZE];
  char mc_state[MAX_RULE_PART_SIZE];
  char mc_ip[MAX_RULE_PART_SIZE];
  char mc_ttl[MAX_RULE_PART_SIZE];

  count = INKCfgContextGetCount(ctx);
  for (i = 0; i < count; i++) {
    ele = (INKIcpEle *) INKCfgContextGetEleAt(ctx, i);

    memset(name, 0, MAX_RULE_PART_SIZE);
    memset(host_ip, 0, MAX_RULE_PART_SIZE);
    memset(peer_type, 0, MAX_RULE_PART_SIZE);
    memset(proxy_port, 0, MAX_RULE_PART_SIZE);
    memset(icp_port, 0, MAX_RULE_PART_SIZE);
    memset(mc_state, 0, MAX_RULE_PART_SIZE);
    memset(mc_ip, 0, MAX_RULE_PART_SIZE);
    memset(mc_ttl, 0, MAX_RULE_PART_SIZE);
    if (convert_icp_ele_to_html_format(ele, name, host_ip, peer_type, proxy_port, icp_port, mc_state, mc_ip, mc_ttl) !=
        WEB_HTTP_ERR_OKAY) {
      Debug("config", "[writeIcpRuleList] invalid Ele, can't format - SKIP");
      continue;                 // invalid ele, so skip to next one
    }

    memset(rule, 0, MAX_RULE_SIZE);
    snprintf(rule, MAX_RULE_SIZE,
                 "ruleList[%d] = new Rule(\"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\");\n", i, name,
                 host_ip, peer_type, proxy_port, icp_port, mc_state, mc_ip, mc_ttl);

    output->copyFrom(rule, strlen(rule));
  }

  INKCfgContextDestroy(ctx);
  return WEB_HTTP_ERR_OKAY;
}



//-------------------------------------------------------------------------
// writeIpAllowRuleList
//-------------------------------------------------------------------------
int
writeIpAllowRuleList(textBuffer * output)
{
  INKIpAllowEle *ele;
  int count, i;
  const char ruleList[] = "var ruleList = new Object();\n";

  INKCfgContext ctx = INKCfgContextCreate(INK_FNAME_IP_ALLOW);
  INKError err = INKCfgContextGet(ctx);
  if (err != INK_ERR_OKAY) {
    mgmt_log(stderr, "[writeIpAllowRuleList] Error INKCfgContextGet");
    return WEB_HTTP_ERR_FAIL;
  }

  output->copyFrom(ruleList, strlen(ruleList));

  char rule[MAX_RULE_SIZE];
  char src_ip[MAX_RULE_PART_SIZE];
  char action[MAX_RULE_PART_SIZE];

  count = INKCfgContextGetCount(ctx);
  for (i = 0; i < count; i++) {
    ele = (INKIpAllowEle *) INKCfgContextGetEleAt(ctx, i);

    memset(src_ip, 0, MAX_RULE_PART_SIZE);
    memset(action, 0, MAX_RULE_PART_SIZE);

    if (convert_ip_allow_ele_to_html_format(ele, src_ip, action) != WEB_HTTP_ERR_OKAY) {
      Debug("config", "[writeIpAllowRuleList] invalid Ele, can't format - SKIP");
      continue;                 // invalid ele, so skip to next one
    }

    memset(rule, 0, MAX_RULE_SIZE);
    snprintf(rule, MAX_RULE_SIZE, "ruleList[%d] = new Rule(\"%s\", \"%s\");\n", i, src_ip, action);

    output->copyFrom(rule, strlen(rule));
  }

  INKCfgContextDestroy(ctx);
  return WEB_HTTP_ERR_OKAY;
}



//-------------------------------------------------------------------------
// writeMgmtAllowRuleList
//-------------------------------------------------------------------------
int
writeMgmtAllowRuleList(textBuffer * output)
{
  INKMgmtAllowEle *ele;
  int count, i;
  const char ruleList[] = "var ruleList = new Object();\n";

  INKCfgContext ctx = INKCfgContextCreate(INK_FNAME_MGMT_ALLOW);
  INKError err = INKCfgContextGet(ctx);
  if (err != INK_ERR_OKAY) {
    mgmt_log(stderr, "[writeMgmtAllowRuleList] Error INKCfgContextGet");
    return WEB_HTTP_ERR_FAIL;
  }

  output->copyFrom(ruleList, strlen(ruleList));

  char rule[MAX_RULE_SIZE];
  char src_ip[MAX_RULE_PART_SIZE];
  char action[MAX_RULE_PART_SIZE];

  count = INKCfgContextGetCount(ctx);
  for (i = 0; i < count; i++) {
    ele = (INKMgmtAllowEle *) INKCfgContextGetEleAt(ctx, i);

    memset(src_ip, 0, MAX_RULE_PART_SIZE);
    memset(action, 0, MAX_RULE_PART_SIZE);

    if (convert_mgmt_allow_ele_to_html_format(ele, src_ip, action) != WEB_HTTP_ERR_OKAY) {
      Debug("config", "[writeMgmtAllowRuleList] invalid Ele, can't format - SKIP");
      continue;                 // invalid ele, so skip to next one
    }

    memset(rule, 0, MAX_RULE_SIZE);
    snprintf(rule, MAX_RULE_SIZE, "ruleList[%d] = new Rule(\"%s\", \"%s\");\n", i, src_ip, action);

    output->copyFrom(rule, strlen(rule));
  }

  INKCfgContextDestroy(ctx);
  return WEB_HTTP_ERR_OKAY;
}


//-------------------------------------------------------------------------
// writeParentRuleList
//-------------------------------------------------------------------------
int
writeParentRuleList(textBuffer * output)
{
  INKParentProxyEle *ele;
  int count, i;
  const char ruleList[] = "var ruleList = new Object();\n";

  INKCfgContext ctx = INKCfgContextCreate(INK_FNAME_PARENT_PROXY);
  INKError err = INKCfgContextGet(ctx);
  if (err != INK_ERR_OKAY) {
    mgmt_log(stderr, "[writeParentRuleList] Error INKCfgContextGet");
    return WEB_HTTP_ERR_FAIL;
  }

  output->copyFrom(ruleList, strlen(ruleList));
  char rule[MAX_RULE_SIZE];
  char pdType[MAX_RULE_PART_SIZE];
  char time[MAX_RULE_PART_SIZE];
  char src_ip[MAX_RULE_PART_SIZE];
  char prefix[MAX_RULE_PART_SIZE];
  char suffix[MAX_RULE_PART_SIZE];
  char port[MAX_RULE_PART_SIZE];
  char method[MAX_RULE_PART_SIZE];
  char scheme[MAX_RULE_PART_SIZE];
  char mixt[MAX_RULE_PART_SIZE];
  char parents[MAX_RULE_PART_SIZE];
  char round_robin[MAX_RULE_PART_SIZE];
  char direct[MAX_RULE_PART_SIZE];

  count = INKCfgContextGetCount(ctx);
  for (i = 0; i < count; i++) {
    ele = (INKParentProxyEle *) INKCfgContextGetEleAt(ctx, i);

    memset(pdType, 0, MAX_RULE_PART_SIZE);
    memset(time, 0, MAX_RULE_PART_SIZE);
    memset(src_ip, 0, MAX_RULE_PART_SIZE);
    memset(prefix, 0, MAX_RULE_PART_SIZE);
    memset(suffix, 0, MAX_RULE_PART_SIZE);
    memset(port, 0, MAX_RULE_PART_SIZE);
    memset(method, 0, MAX_RULE_PART_SIZE);
    memset(scheme, 0, MAX_RULE_PART_SIZE);
    memset(mixt, 0, MAX_RULE_PART_SIZE);
    memset(parents, 0, MAX_RULE_PART_SIZE);
    memset(round_robin, 0, MAX_RULE_PART_SIZE);
    memset(direct, 0, MAX_RULE_PART_SIZE);
    if (convert_parent_ele_to_html_format
        (ele, pdType, time, src_ip, prefix, suffix, port, method, scheme, mixt, parents, round_robin,
         direct) != WEB_HTTP_ERR_OKAY) {
      Debug("config", "[writeParentConfigTable] invalid Ele, can't format - SKIP");
      continue;                 // invalid ele, so skip to next one
    }

    memset(rule, 0, MAX_RULE_SIZE);
    snprintf(rule, MAX_RULE_SIZE,
                 "ruleList[%d] = new Rule(\"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\");\n",
                 i, pdType, ele->parent_info.pd_val, time, src_ip, prefix, suffix, port, method, scheme, mixt, parents,
                 round_robin, direct);

    output->copyFrom(rule, strlen(rule));
  }

  INKCfgContextDestroy(ctx);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// writePartitionRuleList
//-------------------------------------------------------------------------
int
writePartitionRuleList(textBuffer * output)
{
  INKPartitionEle *ele;
  int count, i;
  const char ruleList[] = "var ruleList = new Object();\n";

  INKCfgContext ctx = INKCfgContextCreate(INK_FNAME_PARTITION);
  INKError err = INKCfgContextGet(ctx);
  if (err != INK_ERR_OKAY) {
    mgmt_log(stderr, "[writePartitionRuleList] Error INKCfgContextGet");
    return WEB_HTTP_ERR_FAIL;
  }

  output->copyFrom(ruleList, strlen(ruleList));

  char rule[MAX_RULE_SIZE];

  char part_num[MAX_RULE_PART_SIZE];
  char scheme[MAX_RULE_PART_SIZE];
  char size[MAX_RULE_PART_SIZE];
  char size_fmt[MAX_RULE_PART_SIZE];

  count = INKCfgContextGetCount(ctx);
  for (i = 0; i < count; i++) {
    ele = (INKPartitionEle *) INKCfgContextGetEleAt(ctx, i);

    memset(part_num, 0, MAX_RULE_PART_SIZE);
    memset(scheme, 0, MAX_RULE_PART_SIZE);
    memset(size, 0, MAX_RULE_PART_SIZE);
    memset(size_fmt, 0, MAX_RULE_PART_SIZE);
    if (convert_partition_ele_to_html_format(ele, part_num, scheme, size, size_fmt) != WEB_HTTP_ERR_OKAY) {
      Debug("config", "[writePartitionRuleList] invalid Ele, can't format - SKIP");
      continue;                 // invalid ele, so skip to next one
    }

    memset(rule, 0, MAX_RULE_SIZE);
    snprintf(rule, MAX_RULE_SIZE, "ruleList[%d] = new Rule(\"%s\", \"%s\", \"%s\", \"%s\");\n",
                 i, part_num, scheme, size, size_fmt);

    output->copyFrom(rule, strlen(rule));
  }

  INKCfgContextDestroy(ctx);
  return WEB_HTTP_ERR_OKAY;
}


//-------------------------------------------------------------------------
// writeRemapRuleList
//-------------------------------------------------------------------------
int
writeRemapRuleList(textBuffer * output)
{
  INKRemapEle *ele;
  int count, i;
  const char ruleList[] = "var ruleList = new Object();\n";

  INKCfgContext ctx = INKCfgContextCreate(INK_FNAME_REMAP);
  INKError err = INKCfgContextGet(ctx);
  if (err != INK_ERR_OKAY) {
    mgmt_log(stderr, "[writeRemapRuleList] Error INKCfgContextGet");
    return WEB_HTTP_ERR_FAIL;
  }

  output->copyFrom(ruleList, strlen(ruleList));

  char rule[MAX_RULE_SIZE];
  char rule_type[MAX_RULE_PART_SIZE];
  char from_scheme[MAX_RULE_PART_SIZE];
  char from_port[MAX_RULE_PART_SIZE];
  char from_path[MAX_RULE_PART_SIZE];
  char to_scheme[MAX_RULE_PART_SIZE];
  char to_port[MAX_RULE_PART_SIZE];
  char to_path[MAX_RULE_PART_SIZE];
  char mixt[MAX_RULE_PART_SIZE];

  count = INKCfgContextGetCount(ctx);
  for (i = 0; i < count; i++) {
    ele = (INKRemapEle *) INKCfgContextGetEleAt(ctx, i);

    memset(rule_type, 0, MAX_RULE_PART_SIZE);
    memset(from_scheme, 0, MAX_RULE_PART_SIZE);
    memset(from_port, 0, MAX_RULE_PART_SIZE);
    memset(from_path, 0, MAX_RULE_PART_SIZE);
    memset(to_scheme, 0, MAX_RULE_PART_SIZE);
    memset(to_port, 0, MAX_RULE_PART_SIZE);
    memset(to_path, 0, MAX_RULE_PART_SIZE);
    memset(mixt, 0, MAX_RULE_PART_SIZE);
    if (convert_remap_ele_to_html_format
        (ele, rule_type, from_scheme, from_port, from_path, to_scheme, to_port, to_path, mixt) != WEB_HTTP_ERR_OKAY) {
      Debug("config", "[writeRemapRuleList] invalid Ele, can't format - SKIP");
      continue;                 // invalid ele, so skip to next one
    }

    memset(rule, 0, MAX_RULE_SIZE);
    snprintf(rule, MAX_RULE_SIZE,
                 "ruleList[%d] = new Rule(\"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\",\"%s\",\"%s\" );\n",
                 i, rule_type, from_scheme, ele->from_host, from_port, from_path, to_scheme, ele->to_host, to_port,
                 to_path, mixt);

    output->copyFrom(rule, strlen(rule));
  }

  INKCfgContextDestroy(ctx);
  return WEB_HTTP_ERR_OKAY;
}


//-------------------------------------------------------------------------
// writeSocksRuleList
//-------------------------------------------------------------------------
int
writeSocksRuleList(textBuffer * output)
{
  INKSocksEle *ele;
  int count, i;
  const char ruleList[] = "var ruleList = new Object();\n";

  INKCfgContext ctx = INKCfgContextCreate(INK_FNAME_SOCKS);
  INKError err = INKCfgContextGet(ctx);
  if (err != INK_ERR_OKAY) {
    mgmt_log(stderr, "[writeSocksRuleList] Error INKCfgContextGet");
    return WEB_HTTP_ERR_FAIL;
  }

  output->copyFrom(ruleList, strlen(ruleList));

  char rule[MAX_RULE_SIZE];
  char rule_type[MAX_RULE_PART_SIZE];
  char user[MAX_RULE_PART_SIZE];
  char passwd[MAX_RULE_PART_SIZE];
  char servers[MAX_RULE_PART_SIZE];
  char dest_ip[MAX_RULE_PART_SIZE];
  char rr[MAX_RULE_PART_SIZE];

  count = INKCfgContextGetCount(ctx);
  for (i = 0; i < count; i++) {
    ele = (INKSocksEle *) INKCfgContextGetEleAt(ctx, i);

    memset(rule_type, 0, MAX_RULE_PART_SIZE);
    memset(user, 0, MAX_RULE_PART_SIZE);
    memset(passwd, 0, MAX_RULE_PART_SIZE);
    memset(dest_ip, 0, MAX_RULE_PART_SIZE);
    memset(servers, 0, MAX_RULE_PART_SIZE);
    memset(rr, 0, MAX_RULE_PART_SIZE);
    if (convert_socks_ele_to_html_format(ele, rule_type, dest_ip, user, passwd, servers, rr) != WEB_HTTP_ERR_OKAY) {
      Debug("config", "[writeSocksRuleList] invalid Ele, can't format - SKIP");
      continue;                 // invalid ele, so skip to next one
    }

    memset(rule, 0, MAX_RULE_SIZE);
    snprintf(rule, MAX_RULE_SIZE, "ruleList[%d] = new Rule(\"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\");\n",
                 i, rule_type, dest_ip, user, passwd, servers, rr);

    output->copyFrom(rule, strlen(rule));
  }

  INKCfgContextDestroy(ctx);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// writeSplitDnsRuleList
//-------------------------------------------------------------------------
int
writeSplitDnsRuleList(textBuffer * output)
{
  INKSplitDnsEle *ele;
  int count, i;
  const char ruleList[] = "var ruleList = new Object();\n";

  INKCfgContext ctx = INKCfgContextCreate(INK_FNAME_SPLIT_DNS);
  INKError err = INKCfgContextGet(ctx);
  if (err != INK_ERR_OKAY) {
    mgmt_log(stderr, "[writeSplitDnsRuleList] Error INKCfgContextGet");
    return WEB_HTTP_ERR_FAIL;
  }

  output->copyFrom(ruleList, strlen(ruleList));

  char rule[MAX_RULE_SIZE];

  char pdType[MAX_RULE_PART_SIZE];
  char dns_server[MAX_RULE_PART_SIZE];
  char def_domain[MAX_RULE_PART_SIZE];
  char search_list[MAX_RULE_PART_SIZE];

  count = INKCfgContextGetCount(ctx);
  for (i = 0; i < count; i++) {
    ele = (INKSplitDnsEle *) INKCfgContextGetEleAt(ctx, i);

    memset(pdType, 0, MAX_RULE_PART_SIZE);
    memset(dns_server, 0, MAX_RULE_PART_SIZE);
    memset(def_domain, 0, MAX_RULE_PART_SIZE);
    memset(search_list, 0, MAX_RULE_PART_SIZE);

    if (convert_split_dns_ele_to_html_format(ele, pdType, dns_server, def_domain, search_list) != WEB_HTTP_ERR_OKAY) {
      Debug("config", "[writeSplitDnsRuleList] invalid Ele, can't format - SKIP");
      continue;                 // invalid ele, so skip to next one
    }

    memset(rule, 0, MAX_RULE_SIZE);
    snprintf(rule, MAX_RULE_SIZE, "ruleList[%d] = new Rule(\"%s\", \"%s\", \"%s\", \"%s\", \"%s\");\n",
                 i, pdType, ele->pd_val, dns_server, def_domain, search_list);

    output->copyFrom(rule, strlen(rule));
  }

  INKCfgContextDestroy(ctx);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// writeUpdateRuleList
//-------------------------------------------------------------------------
int
writeUpdateRuleList(textBuffer * output)
{
  INKUpdateEle *ele;
  int count, i;
  const char ruleList[] = "var ruleList = new Object();\n";

  INKCfgContext ctx = INKCfgContextCreate(INK_FNAME_UPDATE_URL);
  INKError err = INKCfgContextGet(ctx);
  if (err != INK_ERR_OKAY) {
    mgmt_log(stderr, "[writeUpdateRuleList] Error INKCfgContextGet");
    return WEB_HTTP_ERR_FAIL;
  }

  output->copyFrom(ruleList, strlen(ruleList));

  char rule[MAX_RULE_SIZE];

  char hdrs[MAX_RULE_PART_SIZE];
  char offset[MAX_RULE_PART_SIZE];
  char interval[MAX_RULE_PART_SIZE];
  char depth[MAX_RULE_PART_SIZE];

  count = INKCfgContextGetCount(ctx);
  for (i = 0; i < count; i++) {
    ele = (INKUpdateEle *) INKCfgContextGetEleAt(ctx, i);

    memset(hdrs, 0, MAX_RULE_PART_SIZE);
    memset(offset, 0, MAX_RULE_PART_SIZE);
    memset(interval, 0, MAX_RULE_PART_SIZE);
    memset(depth, 0, MAX_RULE_PART_SIZE);
    if (convert_update_ele_to_html_format(ele, hdrs, offset, interval, depth) != WEB_HTTP_ERR_OKAY) {
      Debug("config", "[writeUpdateList] invalid Ele, can't format - SKIP");
      continue;                 // invalid ele, so skip to next one
    }

    memset(rule, 0, MAX_RULE_SIZE);
    snprintf(rule, MAX_RULE_SIZE, "ruleList[%d] = new Rule(\"%s\", \"%s\", \"%s\", \"%s\", \"%s\");\n",
                 i, ele->url, hdrs, offset, interval, depth);

    output->copyFrom(rule, strlen(rule));
  }

  INKCfgContextDestroy(ctx);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// writeVaddrsRuleList
//-------------------------------------------------------------------------
int
writeVaddrsRuleList(textBuffer * output)
{
  INKVirtIpAddrEle *ele;
  int count, i;
  const char ruleList[] = "var ruleList = new Object();\n";

  INKCfgContext ctx = INKCfgContextCreate(INK_FNAME_VADDRS);
  INKError err = INKCfgContextGet(ctx);
  if (err != INK_ERR_OKAY) {
    mgmt_log(stderr, "[writeVaddrsRuleList] Error INKCfgContextGet");
    return WEB_HTTP_ERR_FAIL;
  }

  output->copyFrom(ruleList, strlen(ruleList));

  char rule[MAX_RULE_SIZE];
  char ip[MAX_RULE_PART_SIZE];
  char sub_intr[MAX_RULE_PART_SIZE];

  count = INKCfgContextGetCount(ctx);
  for (i = 0; i < count; i++) {
    ele = (INKVirtIpAddrEle *) INKCfgContextGetEleAt(ctx, i);

    memset(ip, 0, MAX_RULE_PART_SIZE);
    memset(sub_intr, 0, MAX_RULE_PART_SIZE);
    if (convert_virt_ip_addr_ele_to_html_format(ele, ip, sub_intr) != WEB_HTTP_ERR_OKAY) {
      Debug("config", "[writeVaddrsRuleList] invalid Ele, can't format - SKIP");
      continue;                 // invalid ele, so skip to next one
    }

    memset(rule, 0, MAX_RULE_SIZE);
    snprintf(rule, MAX_RULE_SIZE, "ruleList[%d] = new Rule(\"%s\", \"%s\", \"%s\");\n", i, ip, ele->intr, sub_intr);

    output->copyFrom(rule, strlen(rule));
  }

  INKCfgContextDestroy(ctx);
  return WEB_HTTP_ERR_OKAY;
}


// -------------------- FORM FUNCTIONS ------------------------------------

//-------------------------------------------------------------------------
// writeArmSecurityConfigForm
//-------------------------------------------------------------------------
// Form contains:
//    Rule Type (open, deny, allow)
//    Connection Type (udp, tcp)
//    Source IP Address (single or range)
//    Destination IP Address
//    Open Ports
//    Destination Ports
//    Source Ports
int
writeArmSecurityConfigForm(WebHttpContext * whc)
{
  textBuffer *output = whc->response_bdy;

  // rule type
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_RULE_TYPE);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_RULE_TYPE_HELP_3);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // connection type
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_CONN_TYPE);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  writeConnTypeSelect(output, "conn_type");
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_CONN_TYPE_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // source ip
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SOURCE_IP);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "src_ip", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SOURCE_IP_HELP_4);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // source port
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SOURCE_PORT);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "s_ports", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SOURCE_PORT_HELP_2);
  HtmlRndrBr(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PORT_LIST_EG);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // destination ip
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_DEST_IP);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "dest_ip", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_DEST_IP_HELP_2);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // destination port
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_DEST_PORT);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "d_ports", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_DEST_PORT_HELP_2);
  HtmlRndrBr(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PORT_LIST_EG);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // open port
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_OPEN_PORT);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "o_ports", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_OPEN_PORT_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// writeBypassConfigForm
//-------------------------------------------------------------------------
// Form contains:
//    Rule Type                                       rule_type
//    Source IP Address (list of ip addresses)        src_ip
//    Destination IP Address (list of ip addresses)   dest_ip
int
writeBypassConfigForm(WebHttpContext * whc)
{
  textBuffer *output = whc->response_bdy;

  // write the hidden "filename" tag
  HtmlRndrInput(output, HTML_CSS_NONE, HTML_TYPE_HIDDEN, HTML_CONFIG_FILE_TAG, HTML_FILE_BYPASS_CONFIG, NULL, NULL);

  // rule type
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_RULE_TYPE);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  writeRuleTypeSelect_bypass(output, "rule_type");
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_RULE_TYPE_HELP_4);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // source ip
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SOURCE_IP);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "src_ip", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SOURCE_IP_HELP_5);
  HtmlRndrBr(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SOURCE_IP_EG_5);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // destination ip
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_DEST_IP);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "dest_ip", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_DEST_IP_HELP_3);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_DEST_IP_EG_3);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// writeCacheConfigForm
//-------------------------------------------------------------------------
// Form contains:
//    Rule Type                 rule_type
//    Primary Dest Type         pd_type
//    Primary Dest Value        pd_value
//    Time                      time
//    Source IP                 src_ip
//    Prefix                    prefix
//    Suffix                    suffix
//    Port                      port
//    Method                    method
//    Scheme                    scheme
//    Time Period               time_period
//    Media-IXT tag             mixt
int
writeCacheConfigForm(WebHttpContext * whc)
{
  textBuffer *output = whc->response_bdy;

  // first write the hidden "filename" tag
  HtmlRndrInput(output, HTML_CSS_NONE, HTML_TYPE_HIDDEN, HTML_CONFIG_FILE_TAG, HTML_FILE_CACHE_CONFIG, NULL, NULL);

  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_RULE_TYPE);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  writeRuleTypeSelect_cache(output, "rule_type");
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_RULE_TYPE_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_TYPE);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  writePdTypeSelect(output, "pd_type");
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_TYPE_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_VALUE);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "pd_val", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_VALUE_HELP);
  HtmlRndrBr(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_VALUE_EG);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // additional specifiers
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 3);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_ADDITIONAL_SPEC);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // time period
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_TIME_PERIOD);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "time_period", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_TIME_PERIOD_HELP);
  HtmlRndrBr(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_TIME_PERIOD_EG);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // secondary specifiers
  writeSecondarySpecsForm(whc, INK_FNAME_CACHE_OBJ);

  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// writeHostingConfigForm
//-------------------------------------------------------------------------
// Form Contains following:
//    Primary Dest Type (only domain or hostname)    pd_type
//    Primary Dest Value                             pd_val
//    partitions (comma separated list of #'s)       partitions
int
writeHostingConfigForm(WebHttpContext * whc)
{
  textBuffer *output = whc->response_bdy;

  // write "filename" hidden tag
  HtmlRndrInput(output, HTML_CSS_NONE, HTML_TYPE_HIDDEN, HTML_CONFIG_FILE_TAG, HTML_FILE_HOSTING_CONFIG, NULL, NULL);

  // Primary Dest Type
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_TYPE);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  writePdTypeSelect_hosting(output, "pd_type");
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_TYPE_HELP_2);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // Primary Dest Value (name = "pd_val")
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_VALUE);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "pd_val", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_VALUE_HELP_2);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // Partitions
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PARTITIONS);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "partitions", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PARTITIONS_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// writeIcpConfigForm
//-------------------------------------------------------------------------
// Form contains:
//    ICP Peer Hostname            hostname
//    ICP Peer IP                  host_ip
//    Peer Type (select)           peer_type
//    TCP Proxy Port               proxy_port
//    UDP ICP Port                 icp_port
//    Multicast on/off             mc_state
//    Multicast IP                 mc_ip
//    Multicast TTL                mc_ttl
int
writeIcpConfigForm(WebHttpContext * whc)
{
  textBuffer *output = whc->response_bdy;

  // write the hidden "filename" tag
  HtmlRndrInput(output, HTML_CSS_NONE, HTML_TYPE_HIDDEN, HTML_CONFIG_FILE_TAG, HTML_FILE_ICP_CONFIG, NULL, NULL);

  // peer hostname
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PEER_HOST);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "hostname", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PEER_HOST_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // peer IP
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PEER_IP);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "host_ip", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PEER_IP_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // peer type
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PEER_TYPE);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  writeCacheTypeSelect(output, "peer_type");
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PEER_TYPE_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // peer proxy port
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PEER_PORT);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "proxy_port", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PEER_PORT_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // peer icp port
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_ICP_PORT);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "icp_port", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_ICP_PORT_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // multicast enable/disabled?
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_MCAST_STATE);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  writeOnOffSelect(output, "mc_state");
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_MCAST_STATE_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // multicast IP
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_MCAST_IP);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "mc_ip", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_MCAST_IP_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_MCAST_TTL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  writeMcTtlSelect(output, "mc_ttl");
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_MCAST_TTL_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  return WEB_HTTP_ERR_OKAY;
}


//-------------------------------------------------------------------------
// writeIpAllowConfigForm
//-------------------------------------------------------------------------
// Form contains:
//    Source IP Address (single or range)        src_ip
//    IP action type                             ip_action
//
int
writeIpAllowConfigForm(WebHttpContext * whc)
{
  textBuffer *output = whc->response_bdy;

  // write the hidden "filename" tag
  HtmlRndrInput(output, HTML_CSS_NONE, HTML_TYPE_HIDDEN, HTML_CONFIG_FILE_TAG, HTML_FILE_IP_ALLOW_CONFIG, NULL, NULL);

  // ip action
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_IP_ACTION);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  writeIpActionSelect(output, "ip_action");
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_IP_ACTION_HELP_2);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // source ip
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SOURCE_IP);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "src_ip", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SOURCE_IP_HELP_6);
  HtmlRndrBr(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SOURCE_IP_EG_6);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  return WEB_HTTP_ERR_OKAY;
}


//-------------------------------------------------------------------------
// writeMgmtAllowConfigForm
//-------------------------------------------------------------------------
// Form contains:
//    Source IP Address (single or range)        src_ip
//    IP action type                             ip_action
//
int
writeMgmtAllowConfigForm(WebHttpContext * whc)
{
  textBuffer *output = whc->response_bdy;

  // write the hidden "filename" tag
  HtmlRndrInput(output, HTML_CSS_NONE, HTML_TYPE_HIDDEN, HTML_CONFIG_FILE_TAG, HTML_FILE_MGMT_ALLOW_CONFIG, NULL, NULL);

  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_IP_ACTION);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  writeIpActionSelect(output, "ip_action");
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_IP_ACTION_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SOURCE_IP);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "src_ip", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SOURCE_IP_HELP);
  HtmlRndrBr(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SOURCE_IP_EG);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// writeParentConfigForm
//-------------------------------------------------------------------------
// Form contains:
//    Primary Dest Type         pd_type
//    Primary Dest Value        pd_value
//    Time                      time
//    Source IP                 src_ip
//    Prefix                    prefix
//    Suffix                    suffix
//    Port                      port
//    Method                    method
//    Scheme                    scheme
//    Media-IXT tag             mixt
//    Parent List               parents
//    Round Robin Type          round_robin
//    Go Direct?                direct
int
writeParentConfigForm(WebHttpContext * whc)
{
  textBuffer *output = whc->response_bdy;

  // write "filename" hidden tag
  HtmlRndrInput(output, HTML_CSS_NONE, HTML_TYPE_HIDDEN, HTML_CONFIG_FILE_TAG, HTML_FILE_PARENT_CONFIG, NULL, NULL);
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_TYPE);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  writePdTypeSelect(output, "pd_type");
  HtmlRndrTdClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_TYPE_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_VALUE);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "pd_val", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_VALUE_HELP);
  HtmlRndrBr(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_VALUE_EG);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // parent proxy list
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PARENTS);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "parents", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PARENTS_HELP);
  HtmlRndrBr(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PARENTS_EG);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // round robin
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_ROUND_ROBIN);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  writeRoundRobinTypeSelect(output, "round_robin");
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_ROUND_ROBIN_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // go direct
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_GO_DIRECT);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  writeTrueFalseSelect(output, "direct");
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_GO_DIRECT_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// writePartitionConfigForm
//-------------------------------------------------------------------------
// Form Contains following:
//    Partition #                   part_num
//    Scheme Type (http/mixt)       scheme
//    partition size                size
//    Size Format (absolute/%)      size_format
int
writePartitionConfigForm(WebHttpContext * whc)
{
  textBuffer *output = whc->response_bdy;

  // write "filename" hidden tag
  HtmlRndrInput(output, HTML_CSS_NONE, HTML_TYPE_HIDDEN, HTML_CONFIG_FILE_TAG, HTML_FILE_PARTITION_CONFIG, NULL, NULL);

  // Partition Number
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PARTITION_NUM);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "part_num", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PARTITION_NUM_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // Scheme Type
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SCHEME);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  writeSchemeSelect_partition(output, "scheme");
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SCHEME_HELP_3);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // partition size
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PSIZE);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "size", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PSIZE_HELP);
  HtmlRndrBr(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PSIZE_EG);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // partition size format
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PSIZE_FMT);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  writeSizeFormatSelect(output, "size_format");
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PSIZE_FMT_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// writeRemapConfigForm
//-------------------------------------------------------------------------
// Form contains:
//    Rule Type                 rule_type
//    Scheme TYpe               scheme
//    Target:
//           host               from_host
//           port               from_port
//           path_prefix        from_path
//    Replacement:
//           host               to_host
//           port               to_port
//           path_prefix        to_path
//    Media IXT tag             mixt
int
writeRemapConfigForm(WebHttpContext * whc)
{
  textBuffer *output = whc->response_bdy;

  // write the hidden "filename" tag
  HtmlRndrInput(output, HTML_CSS_NONE, HTML_TYPE_HIDDEN, HTML_CONFIG_FILE_TAG, HTML_FILE_REMAP_CONFIG, NULL, NULL);

  // rule type
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_RULE_TYPE);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  writeRuleTypeSelect_remap(output, "rule_type");
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_RULE_TYPE_HELP_2);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // scheme
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_FROM_SCHEME);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  writeSchemeSelect_remap(output, "from_scheme");
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SCHEME_HELP_2);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // from host
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_FROM_HOST);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "from_host", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_FROM_HOST_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // from port
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_FROM_PORT);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "from_port", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_FROM_PORT_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // from path prefix
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_FROM_PATH);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "from_path", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_FROM_PATH_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // to scheme
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_TO_SCHEME);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  writeSchemeSelect_remap(output, "to_scheme");
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SCHEME_HELP_2);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // to host
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_TO_HOST);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "to_host", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_TO_HOST_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // to port
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_TO_PORT);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "to_port", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_TO_PORT_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // to path prefix
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_TO_PATH);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "to_path", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_TO_PATH_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// writeSocksConfigForm
//-------------------------------------------------------------------------
// Form contains:
//    Rule Type                 rule_type
//    (INK_SOCKS_BYPASS rule type)
//    IP address list           dest_ip
//    (INK_SOCKS_AUTH rule type)
//    Username                  user
//    Password                  password
//    (INK_SOCKS_MULTIPLE rule type)
//    SOck Servers List         socks_servers
//    Round Robin               round_robin
int
writeSocksConfigForm(WebHttpContext * whc)
{
  textBuffer *output = whc->response_bdy;

  // write the hidden "filename" tag
  HtmlRndrInput(output, HTML_CSS_NONE, HTML_TYPE_HIDDEN, HTML_CONFIG_FILE_TAG, HTML_FILE_SOCKS_CONFIG, NULL, NULL);

  // rule type
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_RULE_TYPE);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  writeRuleTypeSelect_socks(output, "rule_type");
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_RULE_TYPE_HELP_6);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);


  // username
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_USER);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "user", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_USER_HELP_2);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // password
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SOCKS_PASSWORD);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "password", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SOCKS_PASSWORD_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // Destination IP
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_DEST_IP);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "dest_ip", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_ORIGIN_SERVER_HELP);
  HtmlRndrBr(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_ORIGIN_SERVER_EG);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // Parent list of socks servers
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SOCKS_SERVER);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "socks_servers", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SOCKS_SERVER_HELP);
  HtmlRndrBr(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SOCKS_SERVER_EG);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // round robin
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_ROUND_ROBIN);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  writeRoundRobinTypeSelect_notrue(output, "round_robin");
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_ROUND_ROBIN_HELP_2);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  return WEB_HTTP_ERR_OKAY;
}


//-------------------------------------------------------------------------
// writeSplitDnsConfigForm
//-------------------------------------------------------------------------
// Form Contains following:
//    Primary Dest Type
//    Primary Dest Value
//    DNS server names (can have multiple values separated by spaces or ';')
//    Domain Name (optional)
//    Domain Search List (optional - separated by spaces or ';')
//
int
writeSplitDnsConfigForm(WebHttpContext * whc)
{
  textBuffer *output = whc->response_bdy;

  // write "filename" hidden tag
  HtmlRndrInput(output, HTML_CSS_NONE, HTML_TYPE_HIDDEN, HTML_CONFIG_FILE_TAG, HTML_FILE_SPLIT_DNS_CONFIG, NULL, NULL);

  // Primary Dest Type
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_TYPE);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  writePdTypeSelect_splitdns(output, "pd_type");
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_TYPE_HELP_3);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // Primary Dest Value (name = "pd_val")
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_VALUE);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "pd_val", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PDEST_VALUE_HELP_3);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // DNS server ip(s)
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_DNS_SERVER_IP);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "dns_server", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_DNS_SERVER_IP_HELP);
  HtmlRndrBr(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_DNS_SERVER_IP_EG);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // Default Domain Name
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_DOMAIN_NAME);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "def_domain", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_DOMAIN_NAME_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // Domain Search List
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SEARCH_LIST);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "search_list", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SEARCH_LIST_HELP);
  HtmlRndrBr(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SEARCH_LIST_EG);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// writeUpdateConfigForm
//-------------------------------------------------------------------------
// Form Contains following:
//    URL                   url
//    Request Headers       headers
//    Offset Hour           offset_hr
//    Interval              interval
//    Recursion depth       rec_depth
int
writeUpdateConfigForm(WebHttpContext * whc)
{
  textBuffer *output = whc->response_bdy;

  // write "filename" hidden tag
  HtmlRndrInput(output, HTML_CSS_NONE, HTML_TYPE_HIDDEN, HTML_CONFIG_FILE_TAG, HTML_FILE_UPDATE_CONFIG, NULL, NULL);

  // URL
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_URL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "url", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_URL_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // Request Headers
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_REQUEST_HDR);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "headers", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_REQUEST_HDR_HELP);
  HtmlRndrBr(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_REQUEST_HDR_EG);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // Offset Hour
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_OFFSET_HOUR);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "offset_hr", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_OFFSET_HOUR_HELP);
  HtmlRndrBr(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_OFFSET_HOUR_EG);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // Interval
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_INTERVAL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "interval", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_INTERVAL_HELP);
  HtmlRndrBr(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_INTERVAL_EG);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // Recursion Depth
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_RECUR_DEPTH);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "rec_depth", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_RECUR_DEPTH_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// writeVaddrsConfigForm
//-------------------------------------------------------------------------
// Form Contains following:
//    virtual IP            ip
//    Interface             intr
//    Sub-Interface         sub_intr
int
writeVaddrsConfigForm(WebHttpContext * whc)
{
  textBuffer *output = whc->response_bdy;

  // write "filename" hidden tag
  HtmlRndrInput(output, HTML_CSS_NONE, HTML_TYPE_HIDDEN, HTML_CONFIG_FILE_TAG, HTML_FILE_VADDRS_CONFIG, NULL, NULL);

  // IP address
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_VIRTUAL_IP);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "ip", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_VIRTUAL_IP_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // Interface
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_ETH_INTERFACE);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "intr", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_ETH_INTERFACE_HELP_3);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  // Sub interface
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SUB_INTERFACE);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "sub_intr", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SUB_INTERFACE_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// writeSecondarySpecsForm
//-------------------------------------------------------------------------
// Helper function - writes the specific editable data fields for secondary
// specifiers. Assumes that the html for a table are already created. This
// function will only write the a sec spec field per row, and the
// header for Secondary Specifiers
// INKFileNameT parameter is needed because there might be some special
// handling of sec. specs for different files
int
writeSecondarySpecsForm(WebHttpContext * whc, INKFileNameT file)
{
  textBuffer *output = whc->response_bdy;

  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 3);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SECONDARY_SPEC);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_TIME);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "time", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_TIME_HELP);
  HtmlRndrBr(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_TIME_EG);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PREFIX);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "prefix", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PREFIX_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SUFFIX);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "suffix", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SUFFIX_HELP);
  HtmlRndrBr(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SUFFIX_EG);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SOURCE_IP_2);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "src_ip", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SOURCE_IP_2_HELP);
  HtmlRndrBr(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SOURCE_IP_2_EG);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PORT);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrInput(output, HTML_CSS_BODY_TEXT, "text", "port", NULL, NULL, NULL);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PORT_HELP);
  //HtmlRndrBr(output);
  //HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_PORT_EG);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_METHOD);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  writeMethodSelect(output, "method");
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_METHOD_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SCHEME);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  writeSchemeSelect(output, "scheme");
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrUlOpen(output);
  HtmlRndrLi(output);
  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CFG_EDIT_SCHEME_HELP);
  HtmlRndrUlClose(output);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// writeSecondarySpecsTableElem
//-------------------------------------------------------------------------
// helper function that writes the following HTML: lists all the secondary
// specifiers in a table data element, one sec spec per line.
int
writeSecondarySpecsTableElem(textBuffer * output, char *time, char *src_ip, char *prefix, char *suffix, char *port,
                             char *method, char *scheme, char *mixt)
{
  char line[30];
  bool hasSspecs = false;

  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  if (strlen(time) > 0) {
    HtmlRndrSpace(output, 2);
    memset(line, 0, 30);
    snprintf(line, 30, "time=%s", time);
    output->copyFrom(line, strlen(line));
    HtmlRndrBr(output);
    hasSspecs = true;
  }
  if (strlen(prefix) > 0) {
    HtmlRndrSpace(output, 2);
    memset(line, 0, 30);
    snprintf(line, 30, "prefix=%s", prefix);
    output->copyFrom(line, strlen(line));
    HtmlRndrBr(output);
    hasSspecs = true;
  }
  if (strlen(suffix) > 0) {
    HtmlRndrSpace(output, 2);
    memset(line, 0, 30);
    snprintf(line, 30, "suffix=%s", suffix);
    output->copyFrom(line, strlen(line));
    HtmlRndrBr(output);
    hasSspecs = true;
  }
  if (strlen(src_ip) > 0) {
    HtmlRndrSpace(output, 2);
    memset(line, 0, 30);
    snprintf(line, 30, "src_ip=%s", src_ip);
    output->copyFrom(line, strlen(line));
    HtmlRndrBr(output);
    hasSspecs = true;
  }
  if (strlen(port) > 0) {
    HtmlRndrSpace(output, 2);
    memset(line, 0, 30);
    snprintf(line, 30, "port=%s", port);
    output->copyFrom(line, strlen(line));
    HtmlRndrBr(output);
    hasSspecs = true;
  }
  if (strlen(method) > 0) {
    HtmlRndrSpace(output, 2);
    memset(line, 0, 30);
    snprintf(line, 30, "method=%s", method);
    output->copyFrom(line, strlen(line));
    HtmlRndrBr(output);
    hasSspecs = true;
  }
  if (strlen(scheme) > 0) {
    HtmlRndrSpace(output, 2);
    memset(line, 0, 30);
    snprintf(line, 30, "scheme=%s", scheme);
    output->copyFrom(line, strlen(line));
    HtmlRndrBr(output);
    hasSspecs = true;
  }
  if (strlen(mixt) > 0) {
    HtmlRndrSpace(output, 2);
    memset(line, 0, 30);
    snprintf(line, 30, "mixt tag=%s", mixt);
    output->copyFrom(line, strlen(line));
    HtmlRndrBr(output);
    hasSspecs = true;
  }

  if (!hasSspecs) {
    HtmlRndrSpace(output, 2);
  }

  HtmlRndrTdClose(output);

  return WEB_HTTP_ERR_OKAY;
}

// -------------------- CONVERSION FUNCTIONS ------------------------------


//-------------------------------------------------------------------------
// convert_cache_ele_to_html_format
//-------------------------------------------------------------------------
int
convert_cache_ele_to_html_format(INKCacheEle * ele,
                                 char *ruleType,
                                 char *pdType,
                                 char *time,
                                 char *src_ip,
                                 char *prefix,
                                 char *suffix, char *port, char *method, char *scheme, char *time_period, char *mixt)
{
  char *hms_time;

  // rule type
  switch (ele->cfg_ele.type) {
  case INK_CACHE_NEVER:
    snprintf(ruleType, MAX_RULE_PART_SIZE, "never-cache");
    break;
  case INK_CACHE_IGNORE_NO_CACHE:
    snprintf(ruleType, MAX_RULE_PART_SIZE, "ignore-no-cache");
    break;
  case INK_CACHE_IGNORE_CLIENT_NO_CACHE:
    snprintf(ruleType, MAX_RULE_PART_SIZE, "ignore-client-no-cache");
    break;
  case INK_CACHE_IGNORE_SERVER_NO_CACHE:
    snprintf(ruleType, MAX_RULE_PART_SIZE, "ignore-server-no-cache");
    break;
  case INK_CACHE_PIN_IN_CACHE:
    snprintf(ruleType, MAX_RULE_PART_SIZE, "pin-in-cache");
    break;
  case INK_CACHE_REVALIDATE:
    snprintf(ruleType, MAX_RULE_PART_SIZE, "revalidate");
    break;
  case INK_CACHE_TTL_IN_CACHE:
    snprintf(ruleType, MAX_RULE_PART_SIZE, "ttl-in-cache");
    break;
  case INK_CACHE_AUTH_CONTENT:
    snprintf(ruleType, MAX_RULE_PART_SIZE, "cache-auth-content");
    break;
  default:
    goto Lerror;
  }

  if (convert_pdss_to_html_format(ele->cache_info, pdType, time, src_ip, prefix, suffix, port, method, scheme, mixt) !=
      WEB_HTTP_ERR_OKAY)
    goto Lerror;

  // time period (for pin_in_cache, ttl_in_cache, and revalidate only)
  hms_time = hms_time_to_string(ele->time_period);
  if (hms_time) {
    snprintf(time_period, MAX_RULE_PART_SIZE, "%s", hms_time);
    xfree(hms_time);
  }
  return WEB_HTTP_ERR_OKAY;

Lerror:
  Debug("config", "[convert_cache_ele_to_html_format] ERROR - invalid Ele");
  return WEB_HTTP_ERR_FAIL;
}


//-------------------------------------------------------------------------
// convert_hosting_ele_to_html_format
//-------------------------------------------------------------------------
int
convert_hosting_ele_to_html_format(INKHostingEle * ele, char *pdType, char *partitions)
{
  char *list;

  // pd type
  switch (ele->pd_type) {
  case INK_PD_DOMAIN:
    snprintf(pdType, MAX_RULE_PART_SIZE, "domain");
    break;
  case INK_PD_HOST:
    snprintf(pdType, MAX_RULE_PART_SIZE, "hostname");
    break;
  default:
    goto Lerror;
  }

  // pd value
  if (!ele->pd_val)
    goto Lerror;

  // partitions list
  if (ele->partitions) {
    list = int_list_to_string(ele->partitions, ",");
    ink_strncpy(partitions, list, MAX_RULE_PART_SIZE);
    xfree(list);
  } else {
    goto Lerror;
  }

  return WEB_HTTP_ERR_OKAY;

Lerror:
  Debug("config", "[convert_hosting_ele_to_html_format] ERROR - invalid Ele");
  return WEB_HTTP_ERR_FAIL;
}

//-------------------------------------------------------------------------
// convert_icp_ele_to_html_format
//-------------------------------------------------------------------------
int
convert_icp_ele_to_html_format(INKIcpEle * ele,
                               char *name,
                               char *host_ip,
                               char *peer_type,
                               char *proxy_port, char *icp_port, char *mc_state, char *mc_ip, char *mc_ttl)
{
  char *tmpStr;

  if (!ele->peer_hostname && !ele->peer_host_ip_addr)
    goto Lerror;

  // check hostname
  if (ele->peer_hostname)
    snprintf(name, MAX_RULE_PART_SIZE, "%s", ele->peer_hostname);

  // host_ip
  if (ele->peer_host_ip_addr) {
    tmpStr = ip_addr_to_string(ele->peer_host_ip_addr);
    ink_strncpy(host_ip, tmpStr, MAX_RULE_PART_SIZE);
    xfree(tmpStr);
  }
  // cache type
  switch (ele->peer_type) {
  case INK_ICP_PARENT:
    snprintf(peer_type, MAX_RULE_PART_SIZE, "parent");
    break;
  case INK_ICP_SIBLING:
    snprintf(peer_type, MAX_RULE_PART_SIZE, "sibling");
    break;
  default:
    goto Lerror;
  }

  // proxy_port
  snprintf(proxy_port, MAX_RULE_PART_SIZE, "%d", ele->peer_proxy_port);

  // icp_port
  snprintf(icp_port, MAX_RULE_PART_SIZE, "%d", ele->peer_icp_port);

  // mc on/off?
  if (ele->is_multicast) {
    snprintf(mc_state, MAX_RULE_PART_SIZE, "on");
  } else {
    snprintf(mc_state, MAX_RULE_PART_SIZE, "off");
  }

  // mc ip
  if (ele->mc_ip_addr != INK_INVALID_IP_ADDR) {
    tmpStr = ip_addr_to_string(ele->mc_ip_addr);
    ink_strncpy(mc_ip, tmpStr, MAX_RULE_PART_SIZE);
    xfree(tmpStr);
  }
  // mc ttl
  switch (ele->mc_ttl) {
  case INK_MC_TTL_SINGLE_SUBNET:
    snprintf(mc_ttl, MAX_RULE_PART_SIZE, "single subnet");
    break;
  case INK_MC_TTL_MULT_SUBNET:
    snprintf(mc_ttl, MAX_RULE_PART_SIZE, "multiple subnets");
    break;
  default:
    // Handled here:
    //INK_MC_TTL_UNDEFINED
    break;
  }

  return WEB_HTTP_ERR_OKAY;

Lerror:
  Debug("config", "[convert_icp_ele_to_html_format] ERROR - invalid Ele");
  return WEB_HTTP_ERR_FAIL;
}

//-------------------------------------------------------------------------
// convert_ip_allow_ele_to_html_format
//-------------------------------------------------------------------------
int
convert_ip_allow_ele_to_html_format(INKIpAllowEle * ele, char *src_ip, char *action)
{
  char *ip = NULL;

  // src_ip
  if (ele->src_ip_addr) {
    ip = ip_addr_ele_to_string(ele->src_ip_addr);
    snprintf(src_ip, MAX_RULE_PART_SIZE, "%s", ip);
    xfree(ip);
  }
  // action
  switch (ele->action) {
  case INK_IP_ALLOW_ALLOW:
    snprintf(action, MAX_RULE_PART_SIZE, "ip_allow");
    break;
  case INK_IP_ALLOW_DENY:
    snprintf(action, MAX_RULE_PART_SIZE, "ip_deny");
    break;
  default:
    goto Lerror;
  }

  return WEB_HTTP_ERR_OKAY;

Lerror:
  Debug("config", "[convert_ip_allow_ele_to_html_format] ERROR - invalid Ele");
  return WEB_HTTP_ERR_FAIL;
}

//-------------------------------------------------------------------------
// convert_mgmt_allow_ele_to_html_format
//-------------------------------------------------------------------------
int
convert_mgmt_allow_ele_to_html_format(INKMgmtAllowEle * ele, char *src_ip, char *action)
{
  char *ip = NULL;

  // src_ip
  if (ele->src_ip_addr) {
    ip = ip_addr_ele_to_string(ele->src_ip_addr);
    snprintf(src_ip, MAX_RULE_PART_SIZE, "%s", ip);
    xfree(ip);
  }
  // action
  switch (ele->action) {
  case INK_MGMT_ALLOW_ALLOW:
    snprintf(action, MAX_RULE_PART_SIZE, "ip_allow");
    break;
  case INK_MGMT_ALLOW_DENY:
    snprintf(action, MAX_RULE_PART_SIZE, "ip_deny");
    break;
  default:
    goto Lerror;
  }

  return WEB_HTTP_ERR_OKAY;

Lerror:
  Debug("config", "[convert_mgmt_allow_ele_to_html_format] ERROR - invalid Ele");
  return WEB_HTTP_ERR_FAIL;
}

//-------------------------------------------------------------------------
// convert_parent_ele_to_html_format
//-------------------------------------------------------------------------
int
convert_parent_ele_to_html_format(INKParentProxyEle * ele,
                                  char *pdType,
                                  char *time,
                                  char *src_ip,
                                  char *prefix,
                                  char *suffix,
                                  char *port,
                                  char *method,
                                  char *scheme, char *mixt, char *parents, char *round_robin, char *direct)
{
  char *plist;

  if (convert_pdss_to_html_format(ele->parent_info, pdType, time, src_ip, prefix, suffix, port, method, scheme, mixt) !=
      WEB_HTTP_ERR_OKAY)
    goto Lerror;

  // parents
  if (ele->proxy_list) {
    plist = domain_list_to_string((DomainList *) ele->proxy_list, ";");
    snprintf(parents, MAX_RULE_PART_SIZE, "%s", plist);
    xfree(plist);
  }
  // round_robin
  switch (ele->rr) {
  case INK_RR_TRUE:
    snprintf(round_robin, MAX_RULE_PART_SIZE, "true");
    break;
  case INK_RR_STRICT:
    snprintf(round_robin, MAX_RULE_PART_SIZE, "strict");
    break;
  case INK_RR_FALSE:
    snprintf(round_robin, MAX_RULE_PART_SIZE, "false");
    break;
  default:
    // Handled here:
    // INK_RR_NONE, INK_RR_UNDEFINED
    break;
  }

  // go direct
  if (ele->direct)
    snprintf(direct, MAX_RULE_PART_SIZE, "true");
  else
    snprintf(direct, MAX_RULE_PART_SIZE, "false");

  return WEB_HTTP_ERR_OKAY;

Lerror:
  Debug("config", "[convert_parent_ele_to_html_format] ERROR - invalid Ele");
  return WEB_HTTP_ERR_FAIL;
}

//-------------------------------------------------------------------------
// convert_partition_ele_to_html_format
//-------------------------------------------------------------------------
int
convert_partition_ele_to_html_format(INKPartitionEle * ele, char *part_num, char *scheme, char *size, char *size_fmt)
{
  // partition number
  snprintf(part_num, MAX_RULE_PART_SIZE, "%d", ele->partition_num);

  // scheme
  switch (ele->scheme) {
  case INK_PARTITION_HTTP:
    snprintf(scheme, MAX_RULE_PART_SIZE, "http");
    break;
  default:
    goto Lerror;
  }

  // size
  snprintf(size, MAX_RULE_PART_SIZE, "%d", ele->partition_size);

  // size format
  switch (ele->size_format) {
  case INK_SIZE_FMT_PERCENT:
    snprintf(size_fmt, MAX_RULE_PART_SIZE, "percent");
    break;
  case INK_SIZE_FMT_ABSOLUTE:
    snprintf(size_fmt, MAX_RULE_PART_SIZE, "absolute");
    break;
  default:
    goto Lerror;
  }

  return WEB_HTTP_ERR_OKAY;

Lerror:
  Debug("config", "[convert_parent_ele_to_html_format] ERROR - invalid Ele");
  return WEB_HTTP_ERR_FAIL;
}

//-------------------------------------------------------------------------
// convert_remap_ele_to_html_format
//-------------------------------------------------------------------------
int
convert_remap_ele_to_html_format(INKRemapEle * ele,
                                 char *rule_type,
                                 char *from_scheme, char *from_port, char *from_path,
                                 char *to_scheme, char *to_port, char *to_path, char *mixt)
{
  NOWARN_UNUSED(mixt);
  // rule type
  switch (ele->cfg_ele.type) {
  case INK_REMAP_MAP:
    snprintf(rule_type, MAX_RULE_PART_SIZE, "map");
    break;
  case INK_REMAP_REVERSE_MAP:
    snprintf(rule_type, MAX_RULE_PART_SIZE, "reverse_map");
    break;
  case INK_REMAP_REDIRECT:
    snprintf(rule_type, MAX_RULE_PART_SIZE, "redirect");
    break;
  case INK_REMAP_REDIRECT_TEMP:
    snprintf(rule_type, MAX_RULE_PART_SIZE, "redirect_temporary");
    break;
  default:
    goto Lerror;
  }

  // scheme
  switch (ele->from_scheme) {
  case INK_SCHEME_HTTP:
    snprintf(from_scheme, MAX_RULE_PART_SIZE, "http");
    break;
  case INK_SCHEME_HTTPS:
    snprintf(from_scheme, MAX_RULE_PART_SIZE, "https");
    break;
  case INK_SCHEME_RTSP:
    snprintf(from_scheme, MAX_RULE_PART_SIZE, "rtsp");
    break;
  case INK_SCHEME_MMS:
    snprintf(from_scheme, MAX_RULE_PART_SIZE, "mms");
    break;
  default:
    goto Lerror;
  }

  if (!ele->from_host)
    goto Lerror;

  // from port
  if (ele->from_port > 0) {
    snprintf(from_port, MAX_RULE_PART_SIZE, "%d", ele->from_port);
  }
  // from path
  if (ele->from_path_prefix) {
    snprintf(from_path, MAX_RULE_PART_SIZE, "%s", ele->from_path_prefix);
  }

  switch (ele->to_scheme) {
  case INK_SCHEME_HTTP:
    snprintf(to_scheme, MAX_RULE_PART_SIZE, "http");
    break;
  case INK_SCHEME_HTTPS:
    snprintf(to_scheme, MAX_RULE_PART_SIZE, "https");
    break;
  case INK_SCHEME_RTSP:
    snprintf(to_scheme, MAX_RULE_PART_SIZE, "rtsp");
    break;
  case INK_SCHEME_MMS:
    snprintf(to_scheme, MAX_RULE_PART_SIZE, "mms");
    break;
  default:
    goto Lerror;
  }

  if (!ele->to_host)
    goto Lerror;

  // to port
  if (ele->to_port > 0) {
    snprintf(to_port, MAX_RULE_PART_SIZE, "%d", ele->to_port);
  }
  // to path
  if (ele->to_path_prefix) {
    snprintf(to_path, MAX_RULE_PART_SIZE, "%s", ele->to_path_prefix);
  }

  return WEB_HTTP_ERR_OKAY;

Lerror:
  Debug("config", "[convert_remap_ele_to_html_format] ERROR - invalid Ele");
  return WEB_HTTP_ERR_FAIL;
}


//-------------------------------------------------------------------------
// convert_socks_ele_to_html_format
//-------------------------------------------------------------------------
int
convert_socks_ele_to_html_format(INKSocksEle * ele,
                                 char *rule_type, char *dest_ip, char *user, char *passwd, char *servers, char *rr)
{
  char *list, *ip;

  // rule type
  switch (ele->cfg_ele.type) {
  case INK_SOCKS_BYPASS:
    snprintf(rule_type, MAX_RULE_PART_SIZE, "no_socks");
    break;
  case INK_SOCKS_AUTH:
    snprintf(rule_type, MAX_RULE_PART_SIZE, "auth");
    break;
  case INK_SOCKS_MULTIPLE:
    snprintf(rule_type, MAX_RULE_PART_SIZE, "multiple_socks");
    break;
  default:
    goto Lerror;
  }

  // ip list to bypass
  if (ele->ip_addrs) {
    list = ip_addr_list_to_string((IpAddrList *) ele->ip_addrs, ",");
    if (list) {
      snprintf(dest_ip, MAX_RULE_PART_SIZE, "%s", list);
      xfree(list);
    }
  }
  // username
  if (ele->username) {
    snprintf(user, MAX_RULE_PART_SIZE, "%s", ele->username);
  }
  // password
  if (ele->password) {
    snprintf(passwd, MAX_RULE_PART_SIZE, "%s", ele->password);
  }
  // dest ip
  if (ele->dest_ip_addr) {
    if (ele->ip_addrs)
      goto Lerror;
    ip = ip_addr_ele_to_string(ele->dest_ip_addr);
    if (ip) {
      snprintf(dest_ip, MAX_RULE_PART_SIZE, "%s", ip);
      xfree(ip);
    }
  }
  // socks servers
  if (ele->socks_servers) {
    list = domain_list_to_string((DomainList *) ele->socks_servers, ";");
    if (list) {
      snprintf(servers, MAX_RULE_PART_SIZE, "%s", list);
      xfree(list);
    }
  }
  // round_robin
  switch (ele->rr) {
  case INK_RR_TRUE:
    snprintf(rr, MAX_RULE_PART_SIZE, "true");
    break;
  case INK_RR_STRICT:
    snprintf(rr, MAX_RULE_PART_SIZE, "strict");
    break;
  case INK_RR_FALSE:
    snprintf(rr, MAX_RULE_PART_SIZE, "false");
    break;
  default:
    // Handled here:
    // INK_RR_NONE, INK_RR_UNDEFINED
    break;
  }

  return WEB_HTTP_ERR_OKAY;

Lerror:
  Debug("config", "[convert_socks_ele_to_html_format] ERROR - invalid Ele");
  return WEB_HTTP_ERR_FAIL;
}


//-------------------------------------------------------------------------
// convert_split_dns_ele_to_html_format
//-------------------------------------------------------------------------
int
convert_split_dns_ele_to_html_format(INKSplitDnsEle * ele,
                                     char *pdType, char *dns_server, char *def_domain, char *search_list)
{
  char *domain_list;

  // pd type
  switch (ele->pd_type) {
  case INK_PD_DOMAIN:
    snprintf(pdType, MAX_RULE_PART_SIZE, "dest_domain");
    break;
  case INK_PD_HOST:
    snprintf(pdType, MAX_RULE_PART_SIZE, "dest_host");
    break;
  case INK_PD_URL_REGEX:
    snprintf(pdType, MAX_RULE_PART_SIZE, "url_regex");
    break;
  default:
    goto Lerror;
  }

  // pd value
  if (!ele->pd_val)
    goto Lerror;

  // dns servers ip's
  if (ele->dns_servers_addrs) {
    domain_list = domain_list_to_string((DomainList *) (ele->dns_servers_addrs), ";");
    ink_strncpy(dns_server, domain_list, MAX_RULE_PART_SIZE);
    xfree(domain_list);
  } else {
    goto Lerror;
  }

  // default domain is optional
  if (ele->def_domain) {
    ink_strncpy(def_domain, ele->def_domain, MAX_RULE_PART_SIZE);
  }
  // search list is optional
  if (ele->search_list) {
    domain_list = domain_list_to_string((DomainList *) (ele->search_list), ";");
    if (domain_list) {
      ink_strncpy(search_list, domain_list, MAX_RULE_PART_SIZE);
      xfree(domain_list);
    } else {
      goto Lerror;
    }
  }

  return WEB_HTTP_ERR_OKAY;

Lerror:
  Debug("config", "[convert_split_dns_ele_to_html_format] ERROR - invalid Ele");
  return WEB_HTTP_ERR_FAIL;
}

//-------------------------------------------------------------------------
// convert_update_ele_to_html_format
//-------------------------------------------------------------------------
int
convert_update_ele_to_html_format(INKUpdateEle * ele, char *hdrs, char *offset, char *interval, char *depth)
{
  char *list;

  // url
  if (!ele->url)
    goto Lerror;

  // hdrs
  if (ele->headers) {
    list = string_list_to_string(ele->headers, ";");
    if (list) {
      snprintf(hdrs, MAX_RULE_PART_SIZE, "%s", list);
      xfree(list);
    }
  } else {
    goto Lerror;
  }

  // offset hour
  snprintf(offset, MAX_RULE_PART_SIZE, "%d", ele->offset_hour);

  // interval
  snprintf(interval, MAX_RULE_PART_SIZE, "%d", ele->interval);

  // recursion depth
  if (ele->recursion_depth > 0) {
    snprintf(depth, MAX_RULE_PART_SIZE, "%d", ele->recursion_depth);
  }

  return WEB_HTTP_ERR_OKAY;

Lerror:
  Debug("config", "[convert_update_ele_to_html_format] ERROR - invalid Ele");
  return WEB_HTTP_ERR_FAIL;
}


//-------------------------------------------------------------------------
// convert_virt_ip_addr_ele_to_html_format
//-------------------------------------------------------------------------
int
convert_virt_ip_addr_ele_to_html_format(INKVirtIpAddrEle * ele, char *ip, char *sub_intr)
{
  char *ipt;

  // virtual IP
  if (ele->ip_addr) {
    ipt = ip_addr_to_string(ele->ip_addr);
    snprintf(ip, MAX_RULE_PART_SIZE, "%s", ipt);
    xfree(ipt);
  } else {
    goto Lerror;
  }

  // interface
  if (!ele->intr)
    goto Lerror;

  // sub interface
  snprintf(sub_intr, MAX_RULE_PART_SIZE, "%d", ele->sub_intr);

  return WEB_HTTP_ERR_OKAY;

Lerror:
  Debug("config", "[convert_virt_ip_addr_ele_to_html_format] ERROR - invalid Ele");
  return WEB_HTTP_ERR_FAIL;
}


//-------------------------------------------------------------------------
// convert_pdss_to_html_format
//-------------------------------------------------------------------------
// Helper function that can be used by Ele's with embeded INKPdSsFormat
// structures.
int
convert_pdss_to_html_format(INKPdSsFormat info,
                            char *pdType,
                            char *time,
                            char *src_ip,
                            char *prefix, char *suffix, char *port, char *method, char *scheme, char *mixt)
{
  NOWARN_UNUSED(mixt);
  char minA[3];
  char minB[3];

  // pd type
  switch (info.pd_type) {
  case INK_PD_DOMAIN:
    snprintf(pdType, MAX_RULE_PART_SIZE, "dest_domain");
    break;
  case INK_PD_HOST:
    snprintf(pdType, MAX_RULE_PART_SIZE, "dest_host");
    break;
  case INK_PD_IP:
    snprintf(pdType, MAX_RULE_PART_SIZE, "dest_ip");
    break;
  case INK_PD_URL_REGEX:
    snprintf(pdType, MAX_RULE_PART_SIZE, "url_regex");
    break;
  default:
    goto Lerror;
  }

  // pd value
  if (!info.pd_val)
    goto Lerror;

  // secondary specifiers
  // time
  if (info.sec_spec.time.hour_a > 0 ||
      info.sec_spec.time.min_a > 0 || info.sec_spec.time.hour_b > 0 || info.sec_spec.time.min_b > 0) {

    if (info.sec_spec.time.min_a <= 0)
      snprintf(minA, 3, "00");
    else if (info.sec_spec.time.min_a < 10)
      snprintf(minA, 3, "0%d", info.sec_spec.time.min_a);
    else
      snprintf(minA, 3, "%d", info.sec_spec.time.min_a);

    if (info.sec_spec.time.min_b <= 0)
      snprintf(minB, 3, "00");
    else if (info.sec_spec.time.min_b < 10)
      snprintf(minB, 3, "0%d", info.sec_spec.time.min_b);
    else
      snprintf(minB, 3, "%d", info.sec_spec.time.min_b);

    snprintf(time, MAX_RULE_PART_SIZE, "%d:%s-%d:%s", info.sec_spec.time.hour_a,
                 minA, info.sec_spec.time.hour_b, minB);
  }
  // src_ip
  if (info.sec_spec.src_ip)
    snprintf(src_ip, MAX_RULE_PART_SIZE, "%s", info.sec_spec.src_ip);

  // prefix
  if (info.sec_spec.prefix)
    snprintf(prefix, MAX_RULE_PART_SIZE, "%s", info.sec_spec.prefix);

  // suffix
  if (info.sec_spec.suffix)
    snprintf(suffix, MAX_RULE_PART_SIZE, "%s", info.sec_spec.suffix);

  // port
  if (info.sec_spec.port) {
    if (info.sec_spec.port->port_a != 0 && info.sec_spec.port->port_b != 0) {
      snprintf(port, MAX_RULE_PART_SIZE, "%d-%d", info.sec_spec.port->port_a, info.sec_spec.port->port_b);
    } else {
      snprintf(port, MAX_RULE_PART_SIZE, "%d", info.sec_spec.port->port_a);
    }
  }
  //method
  switch (info.sec_spec.method) {
  case INK_METHOD_GET:
    snprintf(method, MAX_RULE_PART_SIZE, "get");
    break;
  case INK_METHOD_POST:
    snprintf(method, MAX_RULE_PART_SIZE, "post");
    break;
  case INK_METHOD_PUT:
    snprintf(method, MAX_RULE_PART_SIZE, "put");
    break;
  case INK_METHOD_TRACE:
    snprintf(method, MAX_RULE_PART_SIZE, "trace");
    break;
  case INK_METHOD_PUSH:
    snprintf(method, MAX_RULE_PART_SIZE, "PUSH");
    break;
  case INK_METHOD_NONE:
    snprintf(method, MAX_RULE_PART_SIZE, "none");
    break;
  default:
    break;
  }

  // scheme
  switch (info.sec_spec.scheme) {
  case INK_SCHEME_HTTP:
    snprintf(scheme, MAX_RULE_PART_SIZE, "http");
    break;
  case INK_SCHEME_HTTPS:
    snprintf(scheme, MAX_RULE_PART_SIZE, "https");
    break;
  case INK_SCHEME_RTSP:
    snprintf(scheme, MAX_RULE_PART_SIZE, "rtsp");
    break;
  case INK_SCHEME_MMS:
    snprintf(scheme, MAX_RULE_PART_SIZE, "mms");
    break;
  case INK_SCHEME_NONE:
    snprintf(scheme, MAX_RULE_PART_SIZE, "none");
    break;
  default:
    break;
  }

  return WEB_HTTP_ERR_OKAY;

Lerror:
  Debug("config", "[convert_pdss_to_html_format] ERROR - invalid Ele");
  return WEB_HTTP_ERR_FAIL;
}

//------------------------- SELECT FUNCTIONS ------------------------------

//-------------------------------------------------------------------------
// writeRuleTypeSelect
//-------------------------------------------------------------------------
void
writeRuleTypeSelect_cache(textBuffer * html, const char *listName)
{
  const char *options[7];
  options[0] = "never-cache";
  options[1] = "ignore-no-cache";
  options[2] = "ignore-client-no-cache";
  options[3] = "ignore-server-no-cache";
  options[4] = "pin-in-cache";
  options[5] = "revalidate";
  options[6] = "ttl-in-cache";

  HtmlRndrSelectList(html, listName, options, 7);
}
void
writeRuleTypeSelect_filter(textBuffer * html, const char *listName)
{
  const char *options[6];
  options[0] = "allow";
  options[1] = "deny";
  options[2] = "ldap";
  options[3] = "ntlm";
  options[4] = "radius";
  options[5] = "strip_hdr";

  HtmlRndrSelectList(html, listName, options, 6);
}

void
writeRuleTypeSelect_remap(textBuffer * html, const char *listName)
{
  const char *options[4];
  options[0] = "map";
  options[1] = "reverse_map";
  options[2] = "redirect";
  options[3] = "redirect_temporary";

  HtmlRndrSelectList(html, listName, options, 4);
}

void
writeRuleTypeSelect_socks(textBuffer * html, const char *listName)
{
  const char *options[3];
  options[0] = "no_socks";
  options[1] = "auth";
  options[2] = "multiple_socks";

  HtmlRndrSelectList(html, listName, options, 3);
}

void
writeRuleTypeSelect_bypass(textBuffer * html, const char *listName)
{
  const char *options[2];
  options[0] = "bypass";
  options[1] = "deny_dyn_bypass";

  HtmlRndrSelectList(html, listName, options, 2);
}

//-------------------------------------------------------------------------
// writeConnTypeSelect
//-------------------------------------------------------------------------
void
writeConnTypeSelect(textBuffer * html, const char *listName)
{
  const char *options[2];
  options[0] = "tcp";
  options[1] = "udp";

  HtmlRndrSelectList(html, listName, options, 2);
}

//-------------------------------------------------------------------------
// writeIpActionSelect
//-------------------------------------------------------------------------
void
writeIpActionSelect(textBuffer * html, const char *listName)
{
  const char *options[2];
  options[0] = "ip_allow";
  options[1] = "ip_deny";

  HtmlRndrSelectList(html, listName, options, 2);
}


//-------------------------------------------------------------------------
// writePdTypeSelect
//-------------------------------------------------------------------------
void
writePdTypeSelect(textBuffer * html, const char *listName)
{
  const char *options[4];
  options[0] = "dest_domain";
  options[1] = "dest_host";
  options[2] = "dest_ip";
  options[3] = "url_regex";

  HtmlRndrSelectList(html, listName, options, 4);
}

void
writePdTypeSelect_splitdns(textBuffer * html, const char *listName)
{
  const char *options[3];
  options[0] = "dest_domain";
  options[1] = "dest_host";
  options[2] = "url_regex";

  HtmlRndrSelectList(html, listName, options, 3);
}

void
writePdTypeSelect_hosting(textBuffer * html, const char *listName)
{
  const char *options[2];
  options[0] = "domain";
  options[1] = "hostname";

  HtmlRndrSelectList(html, listName, options, 2);
}

//-------------------------------------------------------------------------
// writeMethodSelect
//-------------------------------------------------------------------------
// some files may/may not include the PUSH option in their list.
void
writeMethodSelect_push(textBuffer * html, const char *listName)
{
  // PUSH option is enabledwith proxy.config.http.push_method_enabled
  bool found;
  RecInt rec_int;
  found = (RecGetRecordInt("proxy.config.http.push_method_enabled", &rec_int)
           == REC_ERR_OKAY);
  int push_enabled = (int) rec_int;
  if (found && push_enabled) {  // PUSH enabled
    const char *options[6];
    options[0] = "";
    options[1] = "get";
    options[2] = "post";
    options[3] = "put";
    options[4] = "trace";
    options[5] = "PUSH";
    HtmlRndrSelectList(html, listName, options, 6);
  } else {
    writeMethodSelect(html, listName);  // PUSH disabled
  }
}

void
writeMethodSelect(textBuffer * html, const char *listName)
{
  const char *options[5];
  options[0] = "";
  options[1] = "get";
  options[2] = "post";
  options[3] = "put";
  options[4] = "trace";
  HtmlRndrSelectList(html, listName, options, 5);
}


//-------------------------------------------------------------------------
// writeSchemeSelect
//-------------------------------------------------------------------------
void
writeSchemeSelect(textBuffer * html, const char *listName)
{
  const char *options[5];
  options[0] = "";
  options[1] = "http";
  options[2] = "https";
  options[3] = "rtsp";
  options[4] = "mms";

  HtmlRndrSelectList(html, listName, options, 6);
}

void
writeSchemeSelect_partition(textBuffer * html, const char *listName)
{
  const char *options[2];
  options[0] = "http";
  options[1] = "mixt";

  HtmlRndrSelectList(html, listName, options, 2);
}

void
writeSchemeSelect_remap(textBuffer * html, const char *listName)
{
  const char *options[4];
  options[0] = "http";
  options[1] = "https";
  options[2] = "rtsp";
  options[3] = "mms";

  HtmlRndrSelectList(html, listName, options, 5);
}

//-------------------------------------------------------------------------
// writeHeaderTypeSelect
//-------------------------------------------------------------------------
void
writeHeaderTypeSelect(textBuffer * html, const char *listName)
{
  const char *options[5];
  options[0] = "";
  options[1] = "date";
  options[2] = "host";
  options[3] = "cookie";
  options[4] = "client_ip";

  HtmlRndrSelectList(html, listName, options, 5);
}

//-------------------------------------------------------------------------
// writeCacheTypeSelect
//-------------------------------------------------------------------------
void
writeCacheTypeSelect(textBuffer * html, const char *listName)
{
  const char *options[2];
  options[0] = "parent";
  options[1] = "sibling";

  HtmlRndrSelectList(html, listName, options, 2);
}

//-------------------------------------------------------------------------
// writeMcTtlSelect
//-------------------------------------------------------------------------
void
writeMcTtlSelect(textBuffer * html, const char *listName)
{
  const char *options[2];
  options[0] = "single subnet";
  options[1] = "multiple subnets";

  HtmlRndrSelectList(html, listName, options, 2);
}

//-------------------------------------------------------------------------
// writeOnOffSelect
//-------------------------------------------------------------------------
void
writeOnOffSelect(textBuffer * html, const char *listName)
{
  const char *options[2];
  options[0] = "off";
  options[1] = "on";

  HtmlRndrSelectList(html, listName, options, 2);
}

//-------------------------------------------------------------------------
// writeDenySelect
//-------------------------------------------------------------------------
void
writeDenySelect(textBuffer * html, const char *listName)
{
  const char *options[2];
  options[0] = "";
  options[1] = "deny";

  HtmlRndrSelectList(html, listName, options, 2);
}

//-------------------------------------------------------------------------
// writeClientGroupTypeSelect
//-------------------------------------------------------------------------
void
writeClientGroupTypeSelect(textBuffer * html, const char *listName)
{
  const char *options[3];
  options[0] = "ip";
  options[1] = "domain";
  options[2] = "hostname";

  HtmlRndrSelectList(html, listName, options, 3);
}

//-------------------------------------------------------------------------
// writeAccessTypeSelect
//-------------------------------------------------------------------------
void
writeAccessTypeSelect(textBuffer * html, const char *listName)
{
  const char *options[5];
  options[0] = "allow";
  options[1] = "deny";
  options[2] = "basic";
  options[3] = "generic";
  options[4] = "custom";

  HtmlRndrSelectList(html, listName, options, 5);
}

//-------------------------------------------------------------------------
// writeTreatmentTypeSelect
//-------------------------------------------------------------------------
void
writeTreatmentTypeSelect(textBuffer * html, const char *listName)
{
  const char *options[7];
  options[0] = "";
  options[1] = "feed";
  options[2] = "push";
  options[3] = "pull";
  options[4] = "pullover";
  options[5] = "dynamic";
  options[6] = "post";

  HtmlRndrSelectList(html, listName, options, 7);
}

//-------------------------------------------------------------------------
// writeRoundRobinTypeSelect
//-------------------------------------------------------------------------
void
writeRoundRobinTypeSelect(textBuffer * html, const char *listName)
{
  const char *options[4];
  options[0] = "";
  options[1] = "true";
  options[2] = "strict";
  options[3] = "false";

  HtmlRndrSelectList(html, listName, options, 4);
}

void
writeRoundRobinTypeSelect_notrue(textBuffer * html, const char *listName)
{
  const char *options[3];
  options[0] = "";
  options[1] = "strict";
  options[2] = "false";

  HtmlRndrSelectList(html, listName, options, 3);
}


//-------------------------------------------------------------------------
// writeTrueFalseSelect
//-------------------------------------------------------------------------
void
writeTrueFalseSelect(textBuffer * html, const char *listName)
{
  const char *options[2];
  options[0] = "false";
  options[1] = "true";

  HtmlRndrSelectList(html, listName, options, 2);
}

//-------------------------------------------------------------------------
// writeSizeFormatSelect
//-------------------------------------------------------------------------
void
writeSizeFormatSelect(textBuffer * html, const char *listName)
{
  const char *options[2];
  options[0] = "absolute";
  options[1] = "percent";

  HtmlRndrSelectList(html, listName, options, 2);
}

//-------------------------------------------------------------------------
// writeProtocolSelect
//-------------------------------------------------------------------------
void
writeProtocolSelect(textBuffer * html, const char *listName)
{
  const char *options[2];
  options[0] = "";
  options[1] = "dns";

  HtmlRndrSelectList(html, listName, options, 2);
}
