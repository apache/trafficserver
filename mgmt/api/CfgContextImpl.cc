/** @file

  Implementation of CfgContext class and all the CfgEleObj subclasses

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

#include "ts/ink_platform.h"
#include "ts/ink_string.h"

#include "CfgContextImpl.h"
#include "CfgContextUtils.h"
#include "mgmtapi.h"

//--------------------------------------------------------------------------
// Defines
//--------------------------------------------------------------------------

#define TIGHT_RULE_CHECK true

//--------------------------------------------------------------------------
// CommentObj
//--------------------------------------------------------------------------
CommentObj::CommentObj(char *comment)
{
  m_ele   = comment_ele_create(comment);
  m_valid = (comment ? true : false);
}

CommentObj::~CommentObj()
{
  comment_ele_destroy(m_ele);
}

char *
CommentObj::formatEleToRule()
{
  return ats_strdup(m_ele->comment);
}

bool
CommentObj::isValid()
{
  return m_valid;
}

TSCfgEle *
CommentObj::getCfgEleCopy()
{
  return (TSCfgEle *)copy_comment_ele(m_ele);
}

//--------------------------------------------------------------------------
// CacheObj
//--------------------------------------------------------------------------
CacheObj::CacheObj(TSCacheEle *ele)
{
  m_ele   = ele;
  m_valid = true;
  m_valid = isValid();
}

// assumes the specifiers are specified in specific order!!
CacheObj::CacheObj(TokenList *tokens)
{
  Token *tok;
  m_ele                = TSCacheEleCreate(TS_TYPE_UNDEFINED);
  m_ele->cfg_ele.error = TS_ERR_OKAY;
  m_valid              = true;

  if (!tokens) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, TS_FNAME_CACHE_OBJ);

  // if any invalid values, set m_valid=false
  // convert token name and value into ele field
  tok = tokens->first();
  tok = tokens_to_pdss_format(tokens, tok, &(m_ele->cache_info));

  if (!tok) { // INVALID FORMAT
    goto FORMAT_ERR;
  }

  tok = tokens->next(tok);
  if (m_ele->cfg_ele.type == TS_CACHE_REVALIDATE || m_ele->cfg_ele.type == TS_CACHE_PIN_IN_CACHE ||
      m_ele->cfg_ele.type == TS_CACHE_TTL_IN_CACHE) {
    // must have a time specified
    if (strcmp(tok->name, "pin-in-cache") != 0 && strcmp(tok->name, "revalidate") != 0 && strcmp(tok->name, "ttl-in-cache") != 0) {
      goto FORMAT_ERR; // wrong token!!
    }
    if (string_to_hms_time(tok->value, &(m_ele->time_period)) != TS_ERR_OKAY) {
      goto FORMAT_ERR;
    }
  }

  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
  m_valid              = false;
}

CacheObj::~CacheObj()
{
  TSCacheEleDestroy(m_ele);
}

char *
CacheObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  char *pd_str, *time_str;
  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  pd_str = pdest_sspec_to_string(m_ele->cache_info.pd_type, m_ele->cache_info.pd_val, &(m_ele->cache_info.sec_spec));
  if (!pd_str) {
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }
  ink_strlcat(buf, pd_str, sizeof(buf));
  ats_free(pd_str);

  switch (m_ele->cfg_ele.type) {
  case TS_CACHE_NEVER:
    ink_strlcat(buf, "action=never-cache ", sizeof(buf));
    break;
  case TS_CACHE_IGNORE_NO_CACHE:
    ink_strlcat(buf, "action=ignore-no-cache ", sizeof(buf));
    break;
  case TS_CACHE_CLUSTER_CACHE_LOCAL:
    ink_strlcat(buf, "action=cluster-cache-local ", sizeof(buf));
    break;
  case TS_CACHE_IGNORE_CLIENT_NO_CACHE:
    ink_strlcat(buf, "action=ignore-client-no-cache ", sizeof(buf));
    break;
  case TS_CACHE_IGNORE_SERVER_NO_CACHE:
    ink_strlcat(buf, "action=ignore-server-no-cache ", sizeof(buf));
    break;
  case TS_CACHE_AUTH_CONTENT:
    ink_strlcat(buf, "action=cache-auth-content ", sizeof(buf));
    break;
  case TS_CACHE_PIN_IN_CACHE:
    ink_strlcat(buf, "pin-in-cache=", sizeof(buf));
    time_str = hms_time_to_string(m_ele->time_period);
    if (time_str) {
      ink_strlcat(buf, time_str, sizeof(buf));
      ats_free(time_str);
    }
    ink_strlcat(buf, " ", sizeof(buf));
    break;
  case TS_CACHE_REVALIDATE:
    ink_strlcat(buf, "revalidate=", sizeof(buf));
    time_str = hms_time_to_string(m_ele->time_period);
    if (time_str) {
      ink_strlcat(buf, time_str, sizeof(buf));
      ats_free(time_str);
    }
    ink_strlcat(buf, " ", sizeof(buf));
    break;
  case TS_CACHE_TTL_IN_CACHE:
    ink_strlcat(buf, "ttl-in-cache=", sizeof(buf));
    time_str = hms_time_to_string(m_ele->time_period);
    if (time_str) {
      ink_strlcat(buf, time_str, sizeof(buf));
      ats_free(time_str);
    }
    ink_strlcat(buf, " ", sizeof(buf));
    break;
  default:
    // Handled here:
    // Lots of cases...
    break;
  }

  return ats_strdup(buf);
}

bool
CacheObj::isValid()
{
  char *timeStr;

  if (m_ele->cfg_ele.error != TS_ERR_OKAY) {
    m_valid = false;
  }
  // all Cache Ele's should have a prim dest, sec specs are optional
  if (!ccu_checkPdSspec(m_ele->cache_info)) {
    m_valid = false;
  }
  // only pin-in-cache, ttl, and revalidate rules have time period
  switch (m_ele->cfg_ele.type) {
  case TS_CACHE_NEVER:
  case TS_CACHE_IGNORE_NO_CACHE:
  case TS_CACHE_CLUSTER_CACHE_LOCAL:
  case TS_CACHE_IGNORE_CLIENT_NO_CACHE:
  case TS_CACHE_IGNORE_SERVER_NO_CACHE:
  case TS_CACHE_AUTH_CONTENT:
    break;
  case TS_CACHE_PIN_IN_CACHE:
  case TS_CACHE_REVALIDATE:
  case TS_CACHE_TTL_IN_CACHE:
    timeStr = hms_time_to_string(m_ele->time_period);
    if (!timeStr) {
      m_valid = false;
    }
    ats_free(timeStr);
  default:
    // Handled here:
    // Lots of cases ...
    break;
  }

  if (!m_valid)
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;

  return m_valid;
}

TSCfgEle *
CacheObj::getCfgEleCopy()
{
  return (TSCfgEle *)copy_cache_ele(m_ele);
}

//--------------------------------------------------------------------------
// CongestionObj
//--------------------------------------------------------------------------
CongestionObj::CongestionObj(TSCongestionEle *ele)
{
  m_ele   = ele;
  m_valid = true;
  m_valid = isValid();
}

CongestionObj::CongestionObj(TokenList *tokens)
{
  Token *tok;
  m_ele                = TSCongestionEleCreate();
  m_ele->cfg_ele.error = TS_ERR_OKAY;
  m_valid              = true;

  if (!tokens) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, TS_FNAME_CONGESTION);

  // if any invalid values, set m_valid=false
  // convert token name and value into ele field
  tok = tokens->first();
  // tok = tokens_to_pdss_format(tokens, tok, &(m_ele->congestion_info));

  if (!tok) { // INVALID FORMAT
    goto FORMAT_ERR;
  }

  if (strcmp(tok->name, "dest_domain") == 0) {
    m_ele->pd_type = TS_PD_DOMAIN;
  } else if (strcmp(tok->name, "dest_host") == 0) {
    m_ele->pd_type = TS_PD_HOST;
  } else if (strcmp(tok->name, "dest_ip") == 0) {
    m_ele->pd_type = TS_PD_IP;
  } else if (strcmp(tok->name, "host_regex") == 0) {
    m_ele->pd_type = TS_PD_URL_REGEX;
  }
  m_ele->pd_val = ats_strdup(tok->value);

  // check for remaining tags
  tok = tokens->next(tok);
  while (tok) {
    if (!tok->name || !tok->value) {
      goto FORMAT_ERR;
    }
    if (strcmp(tok->name, "prefix") == 0) {
      m_ele->prefix = ats_strdup(tok->value);
    } else if (strcmp(tok->name, "port") == 0) {
      m_ele->port = ink_atoi(tok->value);
    } else if (strcmp(tok->name, "congestion_scheme") == 0) {
      if (strcmp(tok->value, "per_ip") == 0) {
        m_ele->scheme = TS_HTTP_CONGEST_PER_IP;
      } else if (strcmp(tok->value, "per_host") == 0) {
        m_ele->scheme = TS_HTTP_CONGEST_PER_HOST;
      } else {
        goto FORMAT_ERR;
      }
    } else if (strcmp(tok->name, "max_connection_failures") == 0) {
      m_ele->max_connection_failures = ink_atoi(tok->value);
    } else if (strcmp(tok->name, "fail_window") == 0) {
      m_ele->fail_window = ink_atoi(tok->value);
    } else if (strcmp(tok->name, "proxy_retry_interval") == 0) {
      m_ele->proxy_retry_interval = ink_atoi(tok->value);
    } else if (strcmp(tok->name, "client_wait_interval") == 0) {
      m_ele->client_wait_interval = ink_atoi(tok->value);
    } else if (strcmp(tok->name, "wait_interval_alpha") == 0) {
      m_ele->wait_interval_alpha = ink_atoi(tok->value);
    } else if (strcmp(tok->name, "live_os_conn_timeout") == 0) {
      m_ele->live_os_conn_timeout = ink_atoi(tok->value);
    } else if (strcmp(tok->name, "live_os_conn_retries") == 0) {
      m_ele->live_os_conn_retries = ink_atoi(tok->value);
    } else if (strcmp(tok->name, "dead_os_conn_timeout") == 0) {
      m_ele->dead_os_conn_timeout = ink_atoi(tok->value);
    } else if (strcmp(tok->name, "dead_os_conn_retries") == 0) {
      m_ele->dead_os_conn_retries = ink_atoi(tok->value);
    } else if (strcmp(tok->name, "max_connection") == 0) {
      m_ele->max_connection = ink_atoi(tok->value);
    } else if (strcmp(tok->name, "error_page_uri") == 0) {
      m_ele->error_page_uri = ats_strdup(tok->value);
    } else {
      goto FORMAT_ERR;
    }
    tok = tokens->next(tok);
  }
  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
  m_valid              = false;
}

CongestionObj::~CongestionObj()
{
  TSCongestionEleDestroy(m_ele);
}

//
// will always print defaults in the rule
//
char *
CongestionObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  char buf[MAX_BUF_SIZE];
  size_t pos = 0;
  int psize;
  memset(buf, 0, MAX_BUF_SIZE);

  // push in primary destination
  if (pos < sizeof(buf)) {
    switch (m_ele->pd_type) {
    case TS_PD_DOMAIN:
      psize = snprintf(buf + pos, sizeof(buf) - pos, "dest_domain=%s ", m_ele->pd_val);
      break;
    case TS_PD_HOST:
      psize = snprintf(buf + pos, sizeof(buf) - pos, "dest_host=%s ", m_ele->pd_val);
      break;
    case TS_PD_IP:
      psize = snprintf(buf + pos, sizeof(buf) - pos, "dest_ip=%s ", m_ele->pd_val);
      break;
    case TS_PD_URL_REGEX:
      psize = snprintf(buf + pos, sizeof(buf) - pos, "host_regex=%s ", m_ele->pd_val);
      break;
    default:
      psize = 0;
      // Handled here:
      // TS_PD_UNDEFINED
      break;
    }
    if (psize > 0)
      pos += psize;
  }
  // secondary specifiers
  if (m_ele->prefix) {
    if (pos < sizeof(buf) && (psize = snprintf(buf + pos, sizeof(buf) - pos, "prefix=%s ", m_ele->prefix)) > 0)
      pos += psize;
  }
  if (m_ele->port > 0) {
    if (pos < sizeof(buf) && (psize = snprintf(buf + pos, sizeof(buf) - pos, "port=%d ", m_ele->port)) > 0)
      pos += psize;
  }

  if (pos < sizeof(buf) &&
      (psize = snprintf(buf + pos, sizeof(buf) - pos, "max_connection_failures=%d ", m_ele->max_connection_failures)) > 0)
    pos += psize;
  if (pos < sizeof(buf) && (psize = snprintf(buf + pos, sizeof(buf) - pos, "fail_window=%d ", m_ele->fail_window)) > 0)
    pos += psize;
  if (pos < sizeof(buf) &&
      (psize = snprintf(buf + pos, sizeof(buf) - pos, "proxy_retry_interval=%d ", m_ele->proxy_retry_interval)) > 0)
    pos += psize;
  if (pos < sizeof(buf) &&
      (psize = snprintf(buf + pos, sizeof(buf) - pos, "client_wait_interval=%d ", m_ele->client_wait_interval)) > 0)
    pos += psize;
  if (pos < sizeof(buf) &&
      (psize = snprintf(buf + pos, sizeof(buf) - pos, "wait_interval_alpha=%d ", m_ele->wait_interval_alpha)) > 0)
    pos += psize;
  if (pos < sizeof(buf) &&
      (psize = snprintf(buf + pos, sizeof(buf) - pos, "live_os_conn_timeout=%d ", m_ele->live_os_conn_timeout)) > 0)
    pos += psize;
  if (pos < sizeof(buf) &&
      (psize = snprintf(buf + pos, sizeof(buf) - pos, "live_os_conn_retries=%d ", m_ele->live_os_conn_retries)) > 0)
    pos += psize;
  if (pos < sizeof(buf) &&
      (psize = snprintf(buf + pos, sizeof(buf) - pos, "dead_os_conn_timeout=%d ", m_ele->dead_os_conn_timeout)) > 0)
    pos += psize;
  if (pos < sizeof(buf) &&
      (psize = snprintf(buf + pos, sizeof(buf) - pos, "dead_os_conn_retries=%d ", m_ele->dead_os_conn_retries)) > 0)
    pos += psize;
  if (pos < sizeof(buf) && (psize = snprintf(buf + pos, sizeof(buf) - pos, "max_connection=%d ", m_ele->max_connection)) > 0)
    pos += psize;
  if (m_ele->error_page_uri) {
    if (pos < sizeof(buf) && (psize = snprintf(buf + pos, sizeof(buf) - pos, "error_page=%s ", m_ele->error_page_uri)) > 0)
      pos += psize;
  }
  switch (m_ele->scheme) {
  case TS_HTTP_CONGEST_PER_IP:
    snprintf(buf + pos, sizeof(buf) - pos, "congestion_scheme=per_ip ");
    break;
  case TS_HTTP_CONGEST_PER_HOST:
    snprintf(buf + pos, sizeof(buf) - pos, "congestion_scheme=per_host ");
    break;
  default:
    break;
  }

  return ats_strdup(buf);
}

bool
CongestionObj::isValid()
{
  if (m_ele->cfg_ele.error != TS_ERR_OKAY) {
    m_valid = false;
  }
  // all Congestion Ele's should have a prim dest, sec specs are optional
  if (!m_ele->pd_val)
    m_valid = false;

  if (!m_valid)
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
  return m_valid;
}

TSCfgEle *
CongestionObj::getCfgEleCopy()
{
  return (TSCfgEle *)copy_congestion_ele(m_ele);
}

//--------------------------------------------------------------------------
// HostingObj
//--------------------------------------------------------------------------
HostingObj::HostingObj(TSHostingEle *ele)
{
  m_ele   = ele;
  m_valid = true;
  m_valid = isValid();
}

HostingObj::HostingObj(TokenList *tokens)
{
  Token *token;

  m_ele                = TSHostingEleCreate();
  m_ele->cfg_ele.error = TS_ERR_OKAY;
  m_valid              = true;

  if (!tokens || tokens->length != 2) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, TS_FNAME_HOSTING);
  if (m_ele->cfg_ele.type == TS_TYPE_UNDEFINED) {
    goto FORMAT_ERR;
  }
  // First Token
  token = tokens->first();
  if (!token->value) {
    goto FORMAT_ERR;
  }
  if (strcmp(token->name, "hostname") == 0) {
    m_ele->pd_type = TS_PD_HOST;
  } else if (strcmp(token->name, "domain") == 0) {
    m_ele->pd_type = TS_PD_DOMAIN;
  } else {
    goto FORMAT_ERR;
  }
  m_ele->pd_val = ats_strdup(token->value);

  // Second Token
  token = tokens->next(token);
  if (!token->value) {
    goto FORMAT_ERR;
  }
  if (strcmp(token->name, "volume") != 0) {
    goto FORMAT_ERR;
  }
  m_ele->volumes = string_to_int_list(token->value, LIST_DELIMITER);
  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
  m_valid              = false;
}

HostingObj::~HostingObj()
{
  TSHostingEleDestroy(m_ele);
}

char *
HostingObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  char *list_str;
  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  switch (m_ele->pd_type) {
  case TS_PD_HOST:
    ink_strlcat(buf, "hostname=", sizeof(buf));
    break;
  case TS_PD_DOMAIN:
    ink_strlcat(buf, "domain=", sizeof(buf));
    break;
  default:
    // Handled here:
    // Lots of cases...
    break;
  }

  list_str = int_list_to_string(m_ele->volumes, ",");
  ink_strlcat(buf, m_ele->pd_val, sizeof(buf));
  ink_strlcat(buf, " volume=", sizeof(buf));
  ink_strlcat(buf, list_str, sizeof(buf));
  ats_free(list_str);

  return ats_strdup(buf);
}

bool
HostingObj::isValid()
{
  int *part;
  int len, i;

  if (m_ele->pd_type == TS_PD_UNDEFINED) {
    m_valid = false;
    goto Lend;
  }

  if (!m_ele->pd_val) {
    m_valid = false;
    goto Lend;
  }

  if (!m_ele->volumes || !TSIntListIsValid(m_ele->volumes, 0, 50000)) {
    m_valid = false;
    goto Lend;
  }
  // check that each volume is between 1-255
  len = TSIntListLen(m_ele->volumes);
  for (i = 0; i < len; i++) {
    part = TSIntListDequeue(m_ele->volumes);
    if (*part < 1 || *part > 255) {
      TSIntListEnqueue(m_ele->volumes, part);
      m_valid = false;
      goto Lend;
    }
    TSIntListEnqueue(m_ele->volumes, part);
  }

Lend:
  if (!m_valid) {
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
  }
  return m_valid;
}

TSCfgEle *
HostingObj::getCfgEleCopy()
{
  return (TSCfgEle *)copy_hosting_ele(m_ele);
}

//--------------------------------------------------------------------------
// IcpObj
//--------------------------------------------------------------------------
IcpObj::IcpObj(TSIcpEle *ele)
{
  m_ele   = ele;
  m_valid = true;
  m_valid = isValid();
}

IcpObj::IcpObj(TokenList *tokens)
{
  Token *token;
  int i;

  m_ele                = TSIcpEleCreate();
  m_ele->cfg_ele.error = TS_ERR_OKAY;
  m_valid              = true;

  if (!tokens || tokens->length < 8) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, TS_FNAME_ICP_PEER);
  if (m_ele->cfg_ele.type == TS_TYPE_UNDEFINED) {
    goto FORMAT_ERR;
  }

  token = tokens->first();

  for (i = 0; i < 8; i++) {
    if (!token->name || token->value) {
      goto FORMAT_ERR;
    }
    char *alias = token->name;

    unsigned short cache_type;
    int mc_ttl;
    int is_multicast;

    switch (i) {
    case 0:
      if (strlen(alias) > 0)
        m_ele->peer_hostname = ats_strdup(alias);
      break;
    case 1:
      if (strlen(alias) > 0) {
        m_ele->peer_host_ip_addr = string_to_ip_addr(alias);
        if (!m_ele->peer_host_ip_addr)
          goto FORMAT_ERR;
      }
      break;
    case 2:
      cache_type = ink_atoi(alias); // what if failed?
      switch (cache_type) {
      case 1:
        m_ele->peer_type = TS_ICP_PARENT;
        break;
      case 2:
        m_ele->peer_type = TS_ICP_SIBLING;
        break;
      default:
        m_ele->peer_type = TS_ICP_UNDEFINED;
      }
      break;
    case 3:
      m_ele->peer_proxy_port = ink_atoi(alias);
      break;
    case 4:
      m_ele->peer_icp_port = ink_atoi(alias);
      break;
    case 5:
      is_multicast = ink_atoi(alias);
      switch (is_multicast) {
      case 0:
        m_ele->is_multicast = false;
        break;
      case 1:
        m_ele->is_multicast = true;
        break;
      default:
        // ERROR:MC_on can only be either 0 or 1
        goto FORMAT_ERR;
      }
      break;
    case 6:
      m_ele->mc_ip_addr = string_to_ip_addr(alias);
      if (!m_ele->mc_ip_addr)
        goto FORMAT_ERR;
      break;
    case 7:
      mc_ttl = ink_atoi(alias);
      switch (mc_ttl) {
      case 1:
        m_ele->mc_ttl = TS_MC_TTL_SINGLE_SUBNET;
        break;
      case 2:
        m_ele->mc_ttl = TS_MC_TTL_MULT_SUBNET;
        break;
      default:
        m_ele->mc_ttl = TS_MC_TTL_UNDEFINED;
      }
      break;
    default:
      goto FORMAT_ERR;
    }
    token = tokens->next(token);
  }

  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
  m_valid              = false;
}

IcpObj::~IcpObj()
{
  TSIcpEleDestroy(m_ele);
}

char *
IcpObj::formatEleToRule()
{
  char *ip_str1, *ip_str2;
  char buf[MAX_RULE_SIZE];
  int peer_type = 0;

  if (!isValid()) {
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  memset(buf, 0, MAX_RULE_SIZE);

  switch (m_ele->peer_type) {
  case TS_ICP_PARENT:
    peer_type = 1;
    break;
  case TS_ICP_SIBLING:
    peer_type = 2;
    break;
  default:
    // Handled here:
    // TS_ICP_UNDEFINED
    break;
  }

  // optional field
  if (m_ele->peer_host_ip_addr)
    ip_str1 = ip_addr_to_string(m_ele->peer_host_ip_addr);
  else
    ip_str1 = ats_strdup("");

  // optional field
  if (m_ele->mc_ip_addr)
    ip_str2 = ip_addr_to_string(m_ele->mc_ip_addr);
  else
    ip_str2 = ats_strdup("0.0.0.0");

  if (m_ele->peer_hostname) {
    snprintf(buf, sizeof(buf), "%s:%s:%d:%d:%d:%d:%s:", m_ele->peer_hostname, ip_str1, peer_type, m_ele->peer_proxy_port,
             m_ele->peer_icp_port, (m_ele->is_multicast ? 1 : 0), ip_str2);
  } else {
    snprintf(buf, sizeof(buf), ":%s:%d:%d:%d:%d:%s:", ip_str1, peer_type, m_ele->peer_proxy_port, m_ele->peer_icp_port,
             (m_ele->is_multicast ? 1 : 0), ip_str2);
  }

  switch (m_ele->mc_ttl) {
  case TS_MC_TTL_SINGLE_SUBNET:
    ink_strlcat(buf, "1:", sizeof(buf));
    break;
  case TS_MC_TTL_MULT_SUBNET:
    ink_strlcat(buf, "2:", sizeof(buf));
    break;
  case TS_MC_TTL_UNDEFINED:
    ink_strlcat(buf, "0:", sizeof(buf));
    break;
  }

  ats_free(ip_str1);
  ats_free(ip_str2);

  return ats_strdup(buf);
}

bool
IcpObj::isValid()
{
  if (m_ele->cfg_ele.error != TS_ERR_OKAY) {
    m_valid = false;
  }
  // either hostname or IP must be specified
  if (!m_ele->peer_hostname && !m_ele->peer_host_ip_addr) {
    m_valid = false;
  }
  // check valid host IP
  if (m_ele->peer_host_ip_addr && !ccu_checkIpAddr(m_ele->peer_host_ip_addr)) {
    m_valid = false;
  }
  // check valid cache type
  if (m_ele->peer_type == TS_ICP_UNDEFINED) {
    m_valid = false;
  }
  // check valid ports
  if (!ccu_checkPortNum(m_ele->peer_proxy_port)) {
    m_valid = false;
  }

  if (!ccu_checkPortNum(m_ele->peer_icp_port)) {
    m_valid = false;
  }
  // check valid multicast values: mc_ttl, mc_ip, if enabled
  if (m_ele->is_multicast) {
    // a valid multicast address must be between 224.0.0.0-239.255.255.255
    if (!ccu_checkIpAddr(m_ele->mc_ip_addr, "224.0.0.0", "239.255.255.255") || m_ele->mc_ttl == TS_MC_TTL_UNDEFINED)
      m_valid = false;
  } else { // multicast disabled; only valid mc ip is "0.0.0.0"
    if (m_ele->mc_ip_addr && strcmp(m_ele->mc_ip_addr, "0.0.0.0") != 0)
      m_valid = false;
  }

  if (!m_valid) {
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
  }

  return m_valid;
}

TSCfgEle *
IcpObj::getCfgEleCopy()
{
  return (TSCfgEle *)copy_icp_ele(m_ele);
}

//--------------------------------------------------------------------------
// IpAllowObj
//--------------------------------------------------------------------------
IpAllowObj::IpAllowObj(TSIpAllowEle *ele)
{
  m_ele   = ele;
  m_valid = true;
  m_valid = isValid();
}

IpAllowObj::IpAllowObj(TokenList *tokens)
{
  Token *token;

  m_ele                = TSIpAllowEleCreate();
  m_ele->cfg_ele.error = TS_ERR_OKAY;
  m_valid              = true;

  if (!tokens || (tokens->length != 2)) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, TS_FNAME_IP_ALLOW);
  if (m_ele->cfg_ele.type == TS_TYPE_UNDEFINED) {
    goto FORMAT_ERR;
  }

  token = tokens->first();
  if (!token->name || strcmp(token->name, "src_ip")) {
    goto FORMAT_ERR;
  }
  if (!token->value) {
    goto FORMAT_ERR;
  } else {
    m_ele->src_ip_addr = string_to_ip_addr_ele(token->value);
  }

  token = tokens->next(token);
  if (!token->name || strcmp(token->name, "action")) {
    goto FORMAT_ERR;
  }
  if (!token->value) {
    goto FORMAT_ERR;
  } else {
    if (!strcmp(token->value, "ip_allow")) {
      m_ele->action = TS_IP_ALLOW_ALLOW;
    } else if (strcmp(token->value, "ip_deny") == 0) {
      m_ele->action = TS_IP_ALLOW_DENY;
    } else {
      m_ele->action = TS_IP_ALLOW_UNDEFINED;
    }
  }
  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
  m_valid              = false;
}

IpAllowObj::~IpAllowObj()
{
  TSIpAllowEleDestroy(m_ele);
}

char *
IpAllowObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
    return NULL;
    ;
  }

  char *rule;
  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  ink_strlcpy(buf, "src_ip=", sizeof(buf));
  if (m_ele->src_ip_addr) {
    char *ip_str = ip_addr_ele_to_string(m_ele->src_ip_addr);
    if (ip_str) {
      ink_strlcat(buf, ip_str, sizeof(buf));
      ats_free(ip_str);
    }
  }

  ink_strlcat(buf, " action=", sizeof(buf));
  switch (m_ele->action) {
  case TS_IP_ALLOW_ALLOW:
    ink_strlcat(buf, "ip_allow", sizeof(buf));
    break;
  case TS_IP_ALLOW_DENY:
    ink_strlcat(buf, "ip_deny", sizeof(buf));
    break;
  default:
    // Handled here:
    // TS_IP_ALLOW_UNDEFINED
    break;
  }

  rule = ats_strdup(buf);
  return rule;
}

bool
IpAllowObj::isValid()
{
  if (m_ele->cfg_ele.error != TS_ERR_OKAY) {
    m_valid = false;
  }

  if (!m_ele->src_ip_addr) {
    m_valid = false;
  }

  switch (m_ele->action) {
  case TS_IP_ALLOW_ALLOW:
  case TS_IP_ALLOW_DENY:
    break;
  default:
    m_valid = false;
  }

  if (!m_valid) {
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
  }
  return m_valid;
}

TSCfgEle *
IpAllowObj::getCfgEleCopy()
{
  return (TSCfgEle *)copy_ip_allow_ele(m_ele);
}

//--------------------------------------------------------------------------
// ParentProxyObj
//--------------------------------------------------------------------------
ParentProxyObj::ParentProxyObj(TSParentProxyEle *ele)
{
  m_ele   = ele;
  m_valid = true;
  m_valid = isValid();
}

ParentProxyObj::ParentProxyObj(TokenList *tokens)
{
  Token *tok;
  m_ele                = TSParentProxyEleCreate(TS_TYPE_UNDEFINED);
  m_ele->cfg_ele.error = TS_ERR_OKAY;
  m_valid              = true;

  if (!tokens || tokens->length < 1) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, TS_FNAME_PARENT_PROXY);
  if (m_ele->cfg_ele.type == TS_TYPE_UNDEFINED) {
    goto FORMAT_ERR;
  }

  tok = tokens->first();
  tok = tokens_to_pdss_format(tokens, tok, &(m_ele->parent_info));
  if (tok == NULL) {
    goto FORMAT_ERR;
  }
  // search parent and round_robin action tags
  for (tok = tokens->next(tok); tok; tok = tokens->next(tok)) {
    if (strcmp(tok->name, "round_robin") == 0) {
      // sanity check
      if (!tok->value) {
        goto FORMAT_ERR;
      }
      if (strcmp(tok->value, "true") == 0) {
        m_ele->rr = TS_RR_TRUE;
      } else if (strcmp(tok->value, "strict") == 0) {
        m_ele->rr = TS_RR_STRICT;
      } else if (strcmp(tok->value, "false") == 0) {
        m_ele->rr = TS_RR_FALSE;
      } else {
        m_ele->rr = TS_RR_NONE;
        goto FORMAT_ERR;
      }

    } else if (strcmp(tok->name, "parent") == 0) {
      // sanity check
      if (!tok->value) {
        goto FORMAT_ERR;
      }
      m_ele->proxy_list = string_to_domain_list(tok->value, ";");

    } else if (strcmp(tok->name, "go_direct") == 0) {
      // sanity check
      if (!tok->value) {
        goto FORMAT_ERR;
      }
      if (!strcmp(tok->value, "true")) {
        m_ele->direct = true;
      } else if (!strcmp(tok->value, "false")) {
        m_ele->direct = false;
      } else {
        goto FORMAT_ERR;
      }
    } else {
      goto FORMAT_ERR;
    }
  }

  // the rule type should tell us whether go_direct or not
  // the "go_direct" action tag recognization is done in get_rule_type
  switch (m_ele->cfg_ele.type) {
  case TS_PP_GO_DIRECT:
    m_ele->direct = true;
    break;
  case TS_PP_PARENT:
    m_ele->direct = false;
    break;
  default:
    // Handled here:
    // Lots of cases
    break;
  }

  return;

FORMAT_ERR:
  m_valid = false;
}

ParentProxyObj::~ParentProxyObj()
{
  TSParentProxyEleDestroy(m_ele);
}

char *
ParentProxyObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  char *pd_str, *list_str;
  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  pd_str = pdest_sspec_to_string(m_ele->parent_info.pd_type, m_ele->parent_info.pd_val, &(m_ele->parent_info.sec_spec));
  if (!pd_str)
    return NULL;
  ink_strlcat(buf, pd_str, sizeof(buf));
  ats_free(pd_str);

  // round_robin
  if ((m_ele->rr != TS_RR_NONE) && (m_ele->rr != TS_RR_UNDEFINED)) {
    if (!isspace(buf[strlen(buf) - 1])) {
      ink_strlcat(buf, " ", sizeof(buf));
    }
    ink_strlcat(buf, "round_robin=", sizeof(buf));
    switch (m_ele->rr) {
    case TS_RR_TRUE:
      ink_strlcat(buf, "true", sizeof(buf));
      break;
    case TS_RR_STRICT:
      ink_strlcat(buf, "strict", sizeof(buf));
      break;
    case TS_RR_FALSE:
      ink_strlcat(buf, "false", sizeof(buf));
      break;
    default:
      // Handled here:
      // TS_RR_NONE, TS_RR_UNDEFINED
      break;
    }
  }

  if (m_ele->proxy_list != NULL) {
    // include space delimiter if not already exist
    if (!isspace(buf[strlen(buf) - 1])) {
      ink_strlcat(buf, " ", sizeof(buf));
    }
    list_str = domain_list_to_string(m_ele->proxy_list, ";");
    ink_strlcat(buf, "parent=\"", sizeof(buf));
    if (list_str) {
      ink_strlcat(buf, list_str, sizeof(buf));
      ats_free(list_str);
    }
    ink_strlcat(buf, "\"", sizeof(buf));
  }

  if (m_ele->direct) {
    // include space delimiter if not already exist
    if (!isspace(buf[strlen(buf) - 1])) {
      ink_strlcat(buf, " ", sizeof(buf));
    }
    ink_strlcat(buf, "go_direct=true", sizeof(buf));
  } else {
    if (!isspace(buf[strlen(buf) - 1])) {
      ink_strlcat(buf, " ", sizeof(buf));
    }
    ink_strlcat(buf, "go_direct=false", sizeof(buf));
  }

  return ats_strdup(buf);
}

bool
ParentProxyObj::isValid()
{
  if (m_ele->cfg_ele.error != TS_ERR_OKAY) {
    m_valid = false;
  }

  if (!ccu_checkPdSspec(m_ele->parent_info)) {
    m_valid = false;
  }

  if (m_ele->proxy_list && !TSDomainListIsValid(m_ele->proxy_list)) {
    m_valid = false;
  }

  if (!m_valid)
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;

  return m_valid;
}

TSCfgEle *
ParentProxyObj::getCfgEleCopy()
{
  return (TSCfgEle *)copy_parent_proxy_ele(m_ele);
}

//--------------------------------------------------------------------------
// VolumeObj
//--------------------------------------------------------------------------
VolumeObj::VolumeObj(TSVolumeEle *ele)
{
  m_ele   = ele;
  m_valid = true;
  m_valid = isValid();
}

VolumeObj::VolumeObj(TokenList *tokens)
{
  Token *token;

  m_ele                = TSVolumeEleCreate();
  m_ele->cfg_ele.error = TS_ERR_OKAY;
  m_valid              = true;

  if (!tokens || tokens->length != 3) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, TS_FNAME_VOLUME);
  if (m_ele->cfg_ele.type == TS_TYPE_UNDEFINED) {
    goto FORMAT_ERR;
  }

  token = tokens->first();
  if (strcmp(token->name, "volume") || !token->value) {
    goto FORMAT_ERR;
  }
  m_ele->volume_num = ink_atoi(token->value);

  token = tokens->next(token);
  if (strcmp(token->name, "scheme") || !token->value) {
    goto FORMAT_ERR;
  }
  if (!strcmp(token->value, "http")) {
    m_ele->scheme = TS_VOLUME_HTTP;
  } else {
    m_ele->scheme = TS_VOLUME_UNDEFINED;
  }

  token = tokens->next(token);
  if (strcmp(token->name, "size") || !token->value) {
    goto FORMAT_ERR;
  }
  // CAUTION: we may need a tigher error check
  if (strstr(token->value, "%")) {
    m_ele->size_format = TS_SIZE_FMT_PERCENT;
  } else {
    m_ele->size_format = TS_SIZE_FMT_ABSOLUTE;
  }
  m_ele->volume_size = ink_atoi(token->value);

  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
  m_valid              = false;
}

VolumeObj::~VolumeObj()
{
  TSVolumeEleDestroy(m_ele);
}

char *
VolumeObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  snprintf(buf, sizeof(buf), "volume=%d scheme=", m_ele->volume_num);

  switch (m_ele->scheme) {
  case TS_VOLUME_HTTP:
    ink_strlcat(buf, "http", sizeof(buf));
    break;
  default:
    // Handled here:
    // TS_VOLUME_UNDEFINED, TS_SIZE_FMT_ABSOLUTE, TS_SIZE_FMT_UNDEFINED
    break;
  }

  size_t pos = strlen(buf);
  snprintf(buf + pos, sizeof(buf) - pos, " size=%d", m_ele->volume_size);
  switch (m_ele->size_format) {
  case TS_SIZE_FMT_PERCENT:
    ink_strlcat(buf, "%", sizeof(buf));
    break;
  default:
    // Handled here:
    // TS_SIZE_FMT_ABSOLUTE, TS_SIZE_FMT_UNDEFINED
    break;
  }

  return ats_strdup(buf);
}

bool
VolumeObj::isValid()
{
  if (m_ele->cfg_ele.error != TS_ERR_OKAY) {
    m_valid = false;
  }
  // volume nubmer must be between 1-255 inclusive
  if (m_ele->volume_num < 1 || m_ele->volume_num > 255) {
    m_valid = false;
  }

  switch (m_ele->scheme) {
  case TS_VOLUME_HTTP:
    break;
  default:
    m_valid = false;
  }

  // absolute size must be multiple of 128; percentage size <= 100
  if (m_ele->size_format == TS_SIZE_FMT_ABSOLUTE) {
    if ((m_ele->volume_size < 0) || (m_ele->volume_size % 128)) {
      m_valid = false;
    }
  } else if (m_ele->size_format == TS_SIZE_FMT_PERCENT) {
    if ((m_ele->volume_size < 0) || (m_ele->volume_size > 100)) {
      m_valid = false;
    }
  }

  if (!m_valid)
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;

  return m_valid;
}

TSCfgEle *
VolumeObj::getCfgEleCopy()
{
  return (TSCfgEle *)copy_volume_ele(m_ele);
}

//--------------------------------------------------------------------------
// PluginObj
//--------------------------------------------------------------------------
PluginObj::PluginObj(TSPluginEle *ele)
{
  m_ele   = ele;
  m_valid = true;
  m_valid = isValid();
}

PluginObj::PluginObj(TokenList *tokens)
{
  Token *token;

  m_ele                = TSPluginEleCreate();
  m_ele->cfg_ele.error = TS_ERR_OKAY;
  m_valid              = true;

  if (!tokens || tokens->length < 1) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, TS_FNAME_PLUGIN);
  if (m_ele->cfg_ele.type == TS_TYPE_UNDEFINED) {
    goto FORMAT_ERR;
  }
  // plugin name
  token = tokens->first();
  if (strcmp(token->name, "") == 0) {
    goto FORMAT_ERR;
  }
  m_ele->name = ats_strdup(token->name);

  // arguments
  token = tokens->next(token);
  while (token) {
    if (m_ele->args == TS_INVALID_LIST)
      m_ele->args = TSStringListCreate();
    if (token->name)
      TSStringListEnqueue(m_ele->args, ats_strdup(token->name));
    token = tokens->next(token);
  }

  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
  m_valid              = false;
}

PluginObj::~PluginObj()
{
  TSPluginEleDestroy(m_ele);
}

char *
PluginObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  char *list_str;
  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  list_str = string_list_to_string(m_ele->args, " ");
  if (list_str) {
    snprintf(buf, sizeof(buf), "%s %s", m_ele->name, list_str);
    ats_free(list_str);
  } else {
    snprintf(buf, sizeof(buf), "%s", m_ele->name);
  }

  return ats_strdup(buf);
}

bool
PluginObj::isValid()
{
  if (m_ele->cfg_ele.error != TS_ERR_OKAY) {
    m_valid = false;
  }
  // check plugin name
  if (!m_ele->name || strcmp(m_ele->name, "") == 0) {
    m_valid = false;
  }

  return m_valid;
}

TSCfgEle *
PluginObj::getCfgEleCopy()
{
  return (TSCfgEle *)copy_plugin_ele(m_ele);
}

//--------------------------------------------------------------------------
// RemapObj
//--------------------------------------------------------------------------
RemapObj::RemapObj(TSRemapEle *ele)
{
  m_ele   = ele;
  m_valid = true;
  m_valid = isValid();
}

RemapObj::RemapObj(TokenList *tokens)
{
  Token *token;
  short current; // current token index
  Tokenizer fromTok(":/");
  Tokenizer toTok(":/");
  char buf[MAX_RULE_SIZE];

  m_ele                = TSRemapEleCreate(TS_TYPE_UNDEFINED);
  m_ele->cfg_ele.error = TS_ERR_OKAY;
  m_valid              = true;

  if (!tokens || ((tokens->length != 2) && (tokens->length != 3))) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, TS_FNAME_REMAP);
  if (m_ele->cfg_ele.type == TS_TYPE_UNDEFINED) {
    goto FORMAT_ERR;
  }
  // The first token must either be "map, "reverse_map", "redirect", and redirect_temporary
  token = tokens->first();

  // target
  token = tokens->next(token);

  if (!ccu_checkUrl(token->name)) {
    goto FORMAT_ERR;
  }

  // TODO: Should we check the return value (count) here?
  fromTok.Initialize(token->name, ALLOW_EMPTY_TOKS); // allow empty token for parse sanity check

  if (strcmp(fromTok[0], "http") == 0) {
    m_ele->from_scheme = TS_SCHEME_HTTP;
  } else if (strcmp(fromTok[0], "https") == 0) {
    m_ele->from_scheme = TS_SCHEME_HTTPS;
  } else {
    m_ele->from_scheme = TS_SCHEME_UNDEFINED;
    goto FORMAT_ERR;
  }

  // from host
  m_ele->from_host = ats_strdup(fromTok[3]);

  current = 4;
  if (fromTok[4]) {
    // from port
    m_ele->from_port = ink_atoi(fromTok[4]);
    if (m_ele->from_port != 0) { // Does it have a port
      current++;
    } else { // No ports
      m_ele->from_port = TS_INVALID_PORT;
    }

    // from prefix
    if (fromTok[current]) {
      memset(buf, 0, MAX_RULE_SIZE);

      for (int i = current; fromTok[i]; i++) {
        ink_strlcat(buf, fromTok[i], sizeof(buf));
        ink_strlcat(buf, "/", sizeof(buf));
      }

      if ((token->name)[strlen(token->name) - 1] != '/') {
        buf[strlen(buf) - 1] = '\0'; // truncate the last '/'
      }

      m_ele->from_path_prefix = ats_strdup(buf);
    }
  } else {
    if ((token->name)[strlen(token->name) - 1] == '/') {
      memset(buf, 0, MAX_RULE_SIZE);
      ink_strlcpy(buf, m_ele->from_host, sizeof(buf));
      ats_free(m_ele->from_host);
      ink_strlcat(buf, "/", sizeof(buf));
      m_ele->from_host = ats_strdup(buf);
    }
  }

  if (!ccu_checkUrl(token->value)) {
    goto FORMAT_ERR;
  }

  // TODO: Should we check the return value (count) here?
  toTok.Initialize(token->value, ALLOW_EMPTY_TOKS); // allow empty token for parse sanity check

  if (strcmp(toTok[0], "http") == 0) {
    m_ele->to_scheme = TS_SCHEME_HTTP;
  } else if (strcmp(toTok[0], "https") == 0) {
    m_ele->to_scheme = TS_SCHEME_HTTPS;
  } else {
    m_ele->to_scheme = TS_SCHEME_UNDEFINED;
    goto FORMAT_ERR;
  }

  // to host
  m_ele->to_host = ats_strdup(toTok[3]);

  current = 4;
  if (toTok[4]) {
    // to port
    m_ele->to_port = ink_atoi(toTok[4]);
    if (m_ele->to_port != 0) { // Does it have a port
      current++;
    } else { // No ports
      m_ele->to_port = TS_INVALID_PORT;
    }

    // to prefix
    if (toTok[current]) {
      memset(buf, 0, MAX_RULE_SIZE);

      for (int i = current; toTok[i]; i++) {
        ink_strlcat(buf, toTok[i], sizeof(buf));
        ink_strlcat(buf, "/", sizeof(buf));
      }

      if ((token->name)[strlen(token->name) - 1] != '/') {
        buf[strlen(buf) - 1] = '\0'; // truncate the last '/'
      }

      m_ele->to_path_prefix = ats_strdup(buf);
    }
  } else {
    if ((token->value)[strlen(token->value) - 1] == '/') {
      memset(buf, 0, MAX_RULE_SIZE);
      ink_strlcpy(buf, m_ele->to_host, sizeof(buf));
      ats_free(m_ele->to_host);
      ink_strlcat(buf, "/", sizeof(buf));
      m_ele->to_host = ats_strdup(buf);
    }
  }

  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
  m_valid              = false;
}

RemapObj::~RemapObj()
{
  TSRemapEleDestroy(m_ele);
}

char *
RemapObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  switch (m_ele->cfg_ele.type) {
  case TS_REMAP_MAP:
    ink_strlcat(buf, "map", sizeof(buf));
    break;
  case TS_REMAP_REVERSE_MAP:
    ink_strlcat(buf, "reverse_map", sizeof(buf));
    break;
  case TS_REMAP_REDIRECT:
    ink_strlcat(buf, "redirect", sizeof(buf));
    break;
  case TS_REMAP_REDIRECT_TEMP:
    ink_strlcat(buf, "redirect_temporary", sizeof(buf));
    break;
  default:
    // Handled here:
    // Lots of cases...
    break;
  }
  // space delimitor
  ink_strlcat(buf, " ", sizeof(buf));

  // from scheme
  switch (m_ele->from_scheme) {
  case TS_SCHEME_HTTP:
    ink_strlcat(buf, "http", sizeof(buf));
    break;
  case TS_SCHEME_HTTPS:
    ink_strlcat(buf, "https", sizeof(buf));
    break;
  default:
    // Handled here:
    // TS_SCHEME_NONE, TS_SCHEME_UNDEFINED
    break;
  }
  ink_strlcat(buf, "://", sizeof(buf));

  // from host
  if (m_ele->from_host) {
    ink_strlcat(buf, m_ele->from_host, sizeof(buf));
  }
  // from port
  if (m_ele->from_port != TS_INVALID_PORT) {
    snprintf(buf, sizeof(buf), "%s:%d", buf, m_ele->from_port);
  }
  // from host path
  if (m_ele->from_path_prefix) {
    ink_strlcat(buf, "/", sizeof(buf));
    ink_strlcat(buf, m_ele->from_path_prefix, sizeof(buf));
  }
  // space delimitor
  ink_strlcat(buf, " ", sizeof(buf));

  // to scheme
  switch (m_ele->to_scheme) {
  case TS_SCHEME_HTTP:
    ink_strlcat(buf, "http", sizeof(buf));
    break;
  case TS_SCHEME_HTTPS:
    ink_strlcat(buf, "https", sizeof(buf));
    break;
  default:
    // Handled here:
    // TS_SCHEME_NONE, TS_SCHEME_UNDEFINED
    break;
  }
  ink_strlcat(buf, "://", sizeof(buf));

  // to host
  if (m_ele->to_host) {
    ink_strlcat(buf, m_ele->to_host, sizeof(buf));
  }
  // to port
  if (m_ele->to_port != TS_INVALID_PORT) {
    snprintf(buf, sizeof(buf), "%s:%d", buf, m_ele->to_port);
  }
  // to host path
  if (m_ele->to_path_prefix) {
    ink_strlcat(buf, "/", sizeof(buf));
    ink_strlcat(buf, m_ele->to_path_prefix, sizeof(buf));
  }

  return ats_strdup(buf);
}

bool
RemapObj::isValid()
{
  if (m_ele->cfg_ele.error != TS_ERR_OKAY) {
    m_valid = false;
  }
  // rule type
  switch (m_ele->cfg_ele.type) {
  case TS_REMAP_MAP:
  case TS_REMAP_REVERSE_MAP:
  case TS_REMAP_REDIRECT:
  case TS_REMAP_REDIRECT_TEMP:
    break;
  default:
    m_valid = false;
  }

  // from scheme
  switch (m_ele->from_scheme) {
  case TS_SCHEME_HTTP:
  case TS_SCHEME_HTTPS:
    break;
  default:
    m_valid = false;
  }

  switch (m_ele->to_scheme) {
  case TS_SCHEME_HTTP:
  case TS_SCHEME_HTTPS:
    break;
  default:
    m_valid = false;
  }

  // mandatory field
  if (!m_ele->from_host || strstr(m_ele->from_host, ":/")) {
    m_valid = false;
  }
  // mandatory field
  if (!m_ele->to_host || strstr(m_ele->to_host, ":/")) {
    m_valid = false;
  }

  if ((m_ele->from_path_prefix && strstr(m_ele->from_path_prefix, ":")) ||
      (m_ele->to_path_prefix && strstr(m_ele->to_path_prefix, ":"))) {
    m_valid = false;
  }

  if (!m_valid)
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;

  return m_valid;
}

TSCfgEle *
RemapObj::getCfgEleCopy()
{
  return (TSCfgEle *)copy_remap_ele(m_ele);
}

//--------------------------------------------------------------------------
// SocksObj
//--------------------------------------------------------------------------
SocksObj::SocksObj(TSSocksEle *ele)
{
  m_ele   = ele;
  m_valid = true;
  m_valid = isValid();
}

SocksObj::SocksObj(TokenList *tokens)
{
  Token *tok;

  m_ele                = TSSocksEleCreate(TS_TYPE_UNDEFINED);
  m_ele->cfg_ele.error = TS_ERR_OKAY;
  m_valid              = true;

  if (!tokens) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, TS_FNAME_SOCKS);
  if (m_ele->cfg_ele.type == TS_TYPE_UNDEFINED) {
    goto FORMAT_ERR;
  }
  // Determine if it's  a "no-socks" rule or a "parent socks servers" rule
  tok = tokens->first();
  if (strcmp(tok->name, "no_socks") == 0) { // no-socks rule; TS_SOCKS_BYPASS

    if (m_ele->ip_addrs != NULL) {
      goto FORMAT_ERR;
    }
    m_ele->ip_addrs = string_to_ip_addr_list(tok->value, ",");
  } else if (strcmp(tok->name, "auth") == 0) { // TS_SOCKS_AUTH rule
    if (strcmp(tok->value, "u") == 0) {
      tok = tokens->next(tok);
      if (tok && tok->name) {
        m_ele->username = ats_strdup(tok->name);
      } else {
        goto FORMAT_ERR;
      }
      if (tok && tok->name) {
        tok             = tokens->next(tok);
        m_ele->password = ats_strdup(tok->name);
      } else {
        goto FORMAT_ERR;
      }
    } else {
      goto FORMAT_ERR;
    }
  } else { // multiple socks servers rule; TS_SOCKS_MULTIPLE
    // should be dest_ip tag
    if (strcmp(tok->name, "dest_ip") == 0) {
      m_ele->dest_ip_addr = string_to_ip_addr_ele(tok->value);
    } else {
      goto FORMAT_ERR;
    }

    // search dest_ip, parent and round_robin action tags
    for (tok = tokens->next(tok); tok; tok = tokens->next(tok)) {
      if (strcmp(tok->name, "round_robin") == 0) {
        // sanity check
        if (!tok->value) {
          goto FORMAT_ERR;
        }

        if (strcmp(tok->value, "true") == 0) {
          m_ele->rr = TS_RR_TRUE;
        } else if (strcmp(tok->value, "strict") == 0) {
          m_ele->rr = TS_RR_STRICT;
        } else if (strcmp(tok->value, "false") == 0) {
          m_ele->rr = TS_RR_FALSE;
        } else {
          m_ele->rr = TS_RR_NONE;
          goto FORMAT_ERR; // missing value for round_robin tag
        }

      } else if (strcmp(tok->name, "parent") == 0) {
        // sanity check
        if (!tok->value) {
          goto FORMAT_ERR;
        }
        m_ele->socks_servers = string_to_domain_list(tok->value, ";");
      }
    } // end for loop
  }

  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
  m_valid              = false;
}

SocksObj::~SocksObj()
{
  TSSocksEleDestroy(m_ele);
}

char *
SocksObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }
  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  if (m_ele->ip_addrs != NULL) { // TS_SOCKS_BYPASS rule
    char *str_list = ip_addr_list_to_string((LLQ *)m_ele->ip_addrs, ",");
    if (str_list) {
      snprintf(buf, sizeof(buf), "no_socks %s", str_list);
      ats_free(str_list);
    } else {
      return NULL; // invalid ip_addr_list
    }
  } else if (m_ele->username != NULL) { // TS_SOCKS_AUTH rule
    snprintf(buf, sizeof(buf), "auth u %s %s", m_ele->username, m_ele->password);
  } else { // TS_SOCKS_MULTIPLE rule
    // destination ip
    char *ip_str = ip_addr_ele_to_string((TSIpAddrEle *)m_ele->dest_ip_addr);
    if (ip_str) {
      ink_strlcat(buf, "dest_ip=", sizeof(buf));
      ink_strlcat(buf, ip_str, sizeof(buf));
      ats_free(ip_str);
    } else {
      return NULL; // invalid IP
    }

    // parent server list
    if (m_ele->socks_servers != NULL) {
      // include space delimiter if not already exist
      if (!isspace(buf[strlen(buf) - 1])) {
        ink_strlcat(buf, " ", sizeof(buf));
      }
      char *list_str = domain_list_to_string(m_ele->socks_servers, ";");
      if (list_str) {
        ink_strlcat(buf, "parent=\"", sizeof(buf));
        ink_strlcat(buf, list_str, sizeof(buf));
        ink_strlcat(buf, "\"", sizeof(buf));
        ats_free(list_str);
      } else {
        return NULL; // invalid list
      }
    }
    // round-robin, if specified
    if ((m_ele->rr != TS_RR_NONE) && (m_ele->rr != TS_RR_UNDEFINED)) {
      if (!isspace(buf[strlen(buf) - 1])) {
        ink_strlcat(buf, " ", sizeof(buf));
      }
      ink_strlcat(buf, "round_robin=", sizeof(buf));
      switch (m_ele->rr) {
      case TS_RR_TRUE:
        ink_strlcat(buf, "true", sizeof(buf));
        break;
      case TS_RR_STRICT:
        ink_strlcat(buf, "strict", sizeof(buf));
        break;
      case TS_RR_FALSE:
        ink_strlcat(buf, "false", sizeof(buf));
        break;
      default:
        // Handled here:
        // TS_RR_NONE, TS_RR_UNDEFINED
        break;
      }
    }
  }

  return ats_strdup(buf);
}

// the rule must either have an ip addr list (exclusive) OR
// the dest_ip_addr * socks_servers OR
// the username and password
bool
SocksObj::isValid()
{
  if (m_ele->cfg_ele.error != TS_ERR_OKAY) {
    m_valid = false;
  }

  switch (m_ele->cfg_ele.type) {
  case TS_SOCKS_BYPASS:
    if (m_ele->dest_ip_addr || m_ele->username || m_ele->password || !TSIpAddrListIsValid(m_ele->ip_addrs)) {
      m_valid = false;
    } else {
      m_valid = true;
    }
    break;
  case TS_SOCKS_AUTH:
    if (m_ele->username == NULL || m_ele->password == NULL || m_ele->ip_addrs || m_ele->dest_ip_addr) {
      m_valid = false;
    } else {
      m_valid = true;
    }
    break;
  case TS_SOCKS_MULTIPLE:
    if (m_ele->ip_addrs || m_ele->username || !(m_ele->dest_ip_addr && m_ele->socks_servers) ||
        !ccu_checkIpAddrEle(m_ele->dest_ip_addr) || !TSDomainListIsValid(m_ele->socks_servers)) {
      m_valid = false;
    } else {
      m_valid = true;
    }
    break;
  default:
    m_valid = false;
    break;
  }

  if (!m_valid)
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;

  return m_valid;
}

TSCfgEle *
SocksObj::getCfgEleCopy()
{
  return (TSCfgEle *)copy_socks_ele(m_ele);
}

//--------------------------------------------------------------------------
// SplitDnsObj
//--------------------------------------------------------------------------
SplitDnsObj::SplitDnsObj(TSSplitDnsEle *ele)
{
  m_ele   = ele;
  m_valid = true;
  m_valid = isValid();
}

SplitDnsObj::SplitDnsObj(TokenList *tokens)
{
  Token *tok;

  m_ele                = TSSplitDnsEleCreate();
  m_ele->cfg_ele.error = TS_ERR_OKAY;
  m_valid              = true;

  if (!tokens || (tokens->length > 6)) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, TS_FNAME_SPLIT_DNS);
  if (m_ele->cfg_ele.type == TS_TYPE_UNDEFINED) {
    goto FORMAT_ERR;
  }

  tok = tokens->first();
  while (tok) {
    if (!strcmp(tok->name, "dest_domain")) {
      if ((m_ele->pd_type != TS_PD_UNDEFINED) || (m_ele->pd_val != NULL) || (!tok->value)) {
        // fields are already defined!!
        goto FORMAT_ERR;
      }
      m_ele->pd_type = TS_PD_DOMAIN;
      m_ele->pd_val  = ats_strdup(tok->value);
    } else if (strcmp(tok->name, "dest_host") == 0) {
      if ((m_ele->pd_type != TS_PD_UNDEFINED) || (m_ele->pd_val != NULL) || (!tok->value)) {
        // fields are already defined!!
        goto FORMAT_ERR;
      }
      m_ele->pd_type = TS_PD_HOST;
      m_ele->pd_val  = ats_strdup(tok->value);
    } else if (strcmp(tok->name, "url_regex") == 0) {
      if ((m_ele->pd_type != TS_PD_UNDEFINED) || (m_ele->pd_val != NULL) || (!tok->value)) {
        // fields are already defined!!
        goto FORMAT_ERR;
      }
      m_ele->pd_type = TS_PD_URL_REGEX;
      m_ele->pd_val  = ats_strdup(tok->value);
    } else if (strcmp(tok->name, "url") == 0) {
      if ((m_ele->pd_type != TS_PD_UNDEFINED) || (m_ele->pd_val != NULL) || (!tok->value)) {
        // fields are already defined!!
        goto FORMAT_ERR;
      }
      m_ele->pd_type = TS_PD_URL;
      m_ele->pd_val  = ats_strdup(tok->value);
    } else if (strcmp(tok->name, "named") == 0) {
      if ((m_ele->dns_servers_addrs != NULL) || (!tok->value)) {
        // fields are already defined!!
        goto FORMAT_ERR;
      }
      m_ele->dns_servers_addrs = (TSDomainList)string_to_domain_list(tok->value, "; ");
    } else if (strcmp(tok->name, "def_domain") == 0) {
      if ((m_ele->def_domain != NULL) || (!tok->value)) {
        // fields are already defined!!
        goto FORMAT_ERR;
      }
      m_ele->def_domain = ats_strdup(tok->value);
    } else if (strcmp(tok->name, "search_list") == 0) {
      if ((m_ele->search_list != NULL) || (!tok->value)) {
        // fields are already defined!!
        goto FORMAT_ERR;
      }
      m_ele->search_list = (TSDomainList)string_to_domain_list(tok->value, "; ");
    } else {
      // Not able to recongize token name
      goto FORMAT_ERR;
    }

    tok = tokens->next(tok);
  }
  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
  m_valid              = false;
}

SplitDnsObj::~SplitDnsObj()
{
  TSSplitDnsEleDestroy(m_ele);
}

char *
SplitDnsObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  char *pd_name;
  switch (m_ele->pd_type) {
  case TS_PD_DOMAIN:
    pd_name = ats_strdup("dest_domain");
    break;
  case TS_PD_HOST:
    pd_name = ats_strdup("dest_host");
    break;
  case TS_PD_URL_REGEX:
    pd_name = ats_strdup("url_regex");
    break;
  case TS_PD_URL:
    pd_name = ats_strdup("url");
    break;
  default:
    pd_name = ats_strdup(""); // lv: just to make this junk workable
    // Handled here:
    // TS_PD_IP, TS_PD_UNDEFINED
    break;
  }

  if (m_ele->pd_val) {
    ink_strlcat(buf, pd_name, sizeof(buf));
    ink_strlcat(buf, "=", sizeof(buf));
    if (strstr(m_ele->pd_val, " ")) {
      ink_strlcat(buf, "\"", sizeof(buf));
    }
    ink_strlcat(buf, m_ele->pd_val, sizeof(buf));
    if (strstr(m_ele->pd_val, " ")) {
      ink_strlcat(buf, "\"", sizeof(buf));
    }
    ink_strlcat(buf, " ", sizeof(buf));
  }

  if (m_ele->dns_servers_addrs) {
    ink_strlcat(buf, "named=", sizeof(buf));
    char *temp = domain_list_to_string((LLQ *)m_ele->dns_servers_addrs, ";");
    if (temp) {
      if (strstr(temp, " ")) {
        ink_strlcat(buf, "\"", sizeof(buf));
      }
      ink_strlcat(buf, temp, sizeof(buf));
      if (strstr(temp, " ")) {
        ink_strlcat(buf, "\"", sizeof(buf));
      }
      ats_free(temp);
    }
    ink_strlcat(buf, " ", sizeof(buf));
  }

  if (m_ele->def_domain) {
    ink_strlcat(buf, "def_domain=", sizeof(buf));
    if (strstr(m_ele->def_domain, " ")) {
      ink_strlcat(buf, "\"", sizeof(buf));
    }
    ink_strlcat(buf, m_ele->def_domain, sizeof(buf));
    if (strstr(m_ele->def_domain, " ")) {
      ink_strlcat(buf, "\"", sizeof(buf));
    }
    ink_strlcat(buf, " ", sizeof(buf));
  }

  if (m_ele->search_list) {
    ink_strlcat(buf, "search_list=", sizeof(buf));
    char *temp = domain_list_to_string(m_ele->search_list, ";");
    if (temp) {
      if (strstr(temp, " ")) {
        ink_strlcat(buf, "\"", sizeof(buf));
      }
      ink_strlcat(buf, temp, sizeof(buf));
      if (strstr(temp, " ")) {
        ink_strlcat(buf, "\"", sizeof(buf));
      }
      ats_free(temp);
    }
    ink_strlcat(buf, " ", sizeof(buf));
  }
  // chop the last space
  while (isspace(buf[strlen(buf) - 1])) {
    buf[strlen(buf) - 1] = '\0';
  }

  ats_free(pd_name);

  return ats_strdup(buf);
}

bool
SplitDnsObj::isValid()
{
  if (m_ele->cfg_ele.error != TS_ERR_OKAY) {
    m_valid = false;
  }

  switch (m_ele->pd_type) {
  case TS_PD_DOMAIN:
  case TS_PD_HOST:
  case TS_PD_URL_REGEX:
    break;
  default:
    m_valid = false;
  }

  if (!m_ele->pd_val) {
    m_valid = false;
  }

  if (!TSDomainListIsValid(m_ele->dns_servers_addrs)) {
    m_valid = false;
  }
  // search_list is optional
  if (m_ele->search_list && !TSDomainListIsValid(m_ele->search_list)) {
    m_valid = false;
  }

  if (!m_valid)
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;

  return m_valid;
}

TSCfgEle *
SplitDnsObj::getCfgEleCopy()
{
  return (TSCfgEle *)copy_split_dns_ele(m_ele);
}

//--------------------------------------------------------------------------
// StorageObj
//--------------------------------------------------------------------------
StorageObj::StorageObj(TSStorageEle *ele)
{
  m_ele   = ele;
  m_valid = true;
  m_valid = isValid(); // now validate
}

// must have at least 1 token (token-name = pathname, token-value = size (if any) )
StorageObj::StorageObj(TokenList *tokens)
{
  Token *tok;

  m_ele                = TSStorageEleCreate();
  m_ele->cfg_ele.error = TS_ERR_OKAY;
  m_valid              = true;

  if (!tokens || (tokens->length > 6)) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, TS_FNAME_STORAGE);
  if (m_ele->cfg_ele.type == TS_TYPE_UNDEFINED) {
    goto FORMAT_ERR;
  }
  // check first token; must exist
  tok = tokens->first();
  if (!tok->name) {
    goto FORMAT_ERR; // no pathname specified
  } else {
    m_ele->pathname = ats_strdup(tok->name);
  }

  // check if size is specified
  if (tok->value) // size is specified in second token
    m_ele->size = ink_atoi(tok->value);

  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
  m_valid              = false;
}

StorageObj::~StorageObj()
{
  TSStorageEleDestroy(m_ele);
}

char *
StorageObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  if (m_ele->size < 0) { // if size < 0, then raw volume
    snprintf(buf, sizeof(buf), "%s", m_ele->pathname);
  } else {
    snprintf(buf, sizeof(buf), "%s %d", m_ele->pathname, m_ele->size);
  }

  return ats_strdup(buf);
}

bool
StorageObj::isValid()
{
  if (m_ele->cfg_ele.error != TS_ERR_OKAY) {
    m_valid = false;
  }

  if (!(m_ele->pathname))
    m_valid = false;

  if (!m_valid)
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;

  return m_valid;
}

TSCfgEle *
StorageObj::getCfgEleCopy()
{
  return (TSCfgEle *)copy_storage_ele(m_ele);
}

//--------------------------------------------------------------------------
// VirtIpAddrObj
//--------------------------------------------------------------------------
VirtIpAddrObj::VirtIpAddrObj(TSVirtIpAddrEle *ele)
{
  m_ele   = ele;
  m_valid = true;
  m_valid = isValid();
}

VirtIpAddrObj::VirtIpAddrObj(TokenList *tokens)
{
  Token *tok;

  m_ele                = TSVirtIpAddrEleCreate();
  m_ele->cfg_ele.error = TS_ERR_OKAY;
  m_valid              = true;

  if (!tokens || (tokens->length != 3)) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, TS_FNAME_VADDRS);
  if (m_ele->cfg_ele.type == TS_TYPE_UNDEFINED) {
    goto FORMAT_ERR;
  }
  // IP Address
  tok = tokens->first();
  if (tok->value != NULL) {
    goto FORMAT_ERR;
  }
  m_ele->ip_addr = string_to_ip_addr(tok->name);

  // Device
  tok = tokens->next(tok);
  if (tok->value != NULL) {
    goto FORMAT_ERR;
  }
  m_ele->intr = ats_strdup(tok->name);

  // Subinterface
  tok = tokens->next(tok);
  if (tok->value != NULL) {
    goto FORMAT_ERR;
  }
  m_ele->sub_intr = ink_atoi(tok->name); // ERROR: can't convert?

  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
  m_valid              = false;
}

VirtIpAddrObj::~VirtIpAddrObj()
{
  TSVirtIpAddrEleDestroy(m_ele);
}

char *
VirtIpAddrObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  char *ip_str;
  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  ip_str = ip_addr_to_string(m_ele->ip_addr);
  snprintf(buf, sizeof(buf), "%s %s %d", ip_str, m_ele->intr, m_ele->sub_intr);
  ats_free(ip_str);

  return ats_strdup(buf);
}

bool
VirtIpAddrObj::isValid()
{
  if (m_ele->cfg_ele.error != TS_ERR_OKAY) {
    m_valid = false;
  }

  if (!ccu_checkIpAddr(m_ele->ip_addr)) {
    m_valid = false;
  }

  if (!m_ele->intr) {
    m_valid = false;
  }

  if ((m_ele->sub_intr < 1) || (m_ele->sub_intr > 255)) {
    m_valid = false;
  }

  if (!m_valid)
    m_ele->cfg_ele.error = TS_ERR_INVALID_CONFIG_RULE;

  return m_valid;
}

TSCfgEle *
VirtIpAddrObj::getCfgEleCopy()
{
  return (TSCfgEle *)copy_virt_ip_addr_ele(m_ele);
}

/*****************************************************************
 * CfgContext
 *****************************************************************/
CfgContext::CfgContext(TSFileNameT filename)
{
  m_file = filename;
  m_ver  = -1;
}

CfgContext::~CfgContext()
{
  CfgEleObj *ele;
  while ((ele = m_eles.dequeue())) {
    delete ele;
  }
}

TSMgmtError
CfgContext::addEle(CfgEleObj *ele)
{
  ink_assert(ele != NULL);
  m_eles.enqueue(ele); // enqueue CfgEleObj at end of Queue
  return TS_ERR_OKAY;
}

TSMgmtError
CfgContext::removeEle(CfgEleObj *ele)
{
  ink_assert(ele != NULL);
  m_eles.remove(ele);
  delete ele;

  return TS_ERR_OKAY;
}

TSMgmtError
CfgContext::insertEle(CfgEleObj *ele, CfgEleObj *after_ele)
{
  ink_assert(ele != NULL && after_ele != NULL);
  m_eles.insert(ele, after_ele);

  return TS_ERR_OKAY;
}

// insert Ele at front of the Queue
TSMgmtError
CfgContext::pushEle(CfgEleObj *ele)
{
  ink_assert(ele != NULL);
  m_eles.push(ele);

  return TS_ERR_OKAY;
}
