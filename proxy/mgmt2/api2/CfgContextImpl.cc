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

/***********************************************************************
 * CfgContextImpl.cc
 *
 * Implementation of CfgContext class and all the CfgEleObj subclasses
 ***********************************************************************/

#include "inktomi++.h"
#include "ink_platform.h"

#include "CfgContextImpl.h"
#include "CfgContextUtils.h"
#include "INKMgmtAPI.h"

//--------------------------------------------------------------------------
// Defines
//--------------------------------------------------------------------------

#define TIGHT_RULE_CHECK true

//--------------------------------------------------------------------------
// CommentObj
//--------------------------------------------------------------------------
CommentObj::CommentObj(char *comment)
{
  m_ele = comment_ele_create(comment);
  m_valid = (comment ? true : false);
}

CommentObj::~CommentObj()
{
  comment_ele_destroy(m_ele);
}

char *
CommentObj::formatEleToRule()
{
  return xstrdup(m_ele->comment);
}

bool CommentObj::isValid()
{
  return m_valid;
}

INKCfgEle *
CommentObj::getCfgEleCopy()
{
  return (INKCfgEle *) copy_comment_ele(m_ele);
}

//--------------------------------------------------------------------------
// AdminAccessObj
//--------------------------------------------------------------------------
AdminAccessObj::AdminAccessObj(INKAdminAccessEle * ele)
{
  m_ele = ele;
  m_valid = true;
  m_valid = isValid();
}

AdminAccessObj::AdminAccessObj(TokenList * tokens)
{
  Token *tok;
  int accessType;

  m_ele = INKAdminAccessEleCreate();
  m_ele->cfg_ele.error = INK_ERR_OKAY;
  m_valid = true;

  if (!tokens || tokens->length < 3) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, INK_FNAME_ADMIN_ACCESS);
  if (m_ele->cfg_ele.type == INK_TYPE_UNDEFINED) {
    goto FORMAT_ERR;
  }
  // The first token
  tok = tokens->first();
#ifdef TIGHT_RULE_CHECK
  if (tok->value)
    goto FORMAT_ERR;
#endif
  m_ele->user = xstrdup(tok->name);

  // The second token
  tok = tokens->next(tok);
#ifdef TIGHT_RULE_CHECK
  if (tok->value)
    goto FORMAT_ERR;
#endif
  m_ele->password = xstrdup(tok->name);

  // The third (last) token
  tok = tokens->next(tok);
#ifdef TIGHT_RULE_CHECK
  if (tok->value)
    goto FORMAT_ERR;
#endif
  accessType = ink_atoi(tok->name);
  switch (accessType) {
  case 0:
    m_ele->access = INK_ACCESS_NONE;
    break;
  case 1:
    m_ele->access = INK_ACCESS_MONITOR;
    break;
  case 2:
    m_ele->access = INK_ACCESS_MONITOR_VIEW;
    break;
  case 3:
    m_ele->access = INK_ACCESS_MONITOR_CHANGE;
    break;
  default:
    m_ele->access = INK_ACCESS_UNDEFINED;
    goto FORMAT_ERR;
  }
  m_ele->cfg_ele.error = INK_ERR_OKAY;
  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
  m_valid = false;
}

AdminAccessObj::~AdminAccessObj()
{
  INKAdminAccessEleDestroy(m_ele);
}

char *
AdminAccessObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  short accessType;

  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  switch (m_ele->access) {
  case INK_ACCESS_NONE:
    accessType = 0;
    break;
  case INK_ACCESS_MONITOR:
    accessType = 1;
    break;
  case INK_ACCESS_MONITOR_VIEW:
    accessType = 2;
    break;
  case INK_ACCESS_MONITOR_CHANGE:
    accessType = 3;
    break;
  default:
    accessType = 0;             // lv: just zero it
    // Handled here:
    // INK_ACCESS_UNDEFINED
    break;
  }

  snprintf(buf, sizeof(buf), "%s:%s:%d:", m_ele->user, m_ele->password, accessType);

  return xstrdup(buf);
}

bool AdminAccessObj::isValid()
{
  if (m_ele->cfg_ele.error != INK_ERR_OKAY) {
    m_valid = false;
  }
  // Must have a user
  if (!m_ele->user) {
    m_valid = false;
  }
  // Must have a password
  if (!m_ele->password) {
    m_valid = false;
  }
  // validate access type
  switch (m_ele->access) {
  case INK_ACCESS_NONE:
  case INK_ACCESS_MONITOR:
  case INK_ACCESS_MONITOR_VIEW:
  case INK_ACCESS_MONITOR_CHANGE:
    break;
  default:
    m_valid = false;
  }

  if (!m_valid) {
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
  }

  return m_valid;
}

INKCfgEle *
AdminAccessObj::getCfgEleCopy()
{
  return (INKCfgEle *) copy_admin_access_ele(m_ele);
}

//--------------------------------------------------------------------------
// CacheObj
//--------------------------------------------------------------------------
CacheObj::CacheObj(INKCacheEle * ele)
{
  m_ele = ele;
  m_valid = true;
  m_valid = isValid();
}

// assumes the specifiers are specified in specific order!!
CacheObj::CacheObj(TokenList * tokens)
{
  Token *tok;
  m_ele = INKCacheEleCreate(INK_TYPE_UNDEFINED);
  m_ele->cfg_ele.error = INK_ERR_OKAY;
  m_valid = true;

  if (!tokens) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, INK_FNAME_CACHE_OBJ);

  // if any invalid values, set m_valid=false
  // convert token name and value into ele field
  tok = tokens->first();
  tok = tokens_to_pdss_format(tokens, tok, &(m_ele->cache_info));

  if (!tok) {                   // INVALID FORMAT
    goto FORMAT_ERR;
  }

  tok = tokens->next(tok);
  if (m_ele->cfg_ele.type == INK_CACHE_REVALIDATE ||
      m_ele->cfg_ele.type == INK_CACHE_PIN_IN_CACHE || m_ele->cfg_ele.type == INK_CACHE_TTL_IN_CACHE) {
    // must have a time specified
    if (strcmp(tok->name, "pin-in-cache") != 0 && strcmp(tok->name, "revalidate") != 0 &&
        strcmp(tok->name, "ttl-in-cache") != 0) {
      goto FORMAT_ERR;          // wrong token!! 
    }
    if (string_to_hms_time(tok->value, &(m_ele->time_period)) != INK_ERR_OKAY) {
      goto FORMAT_ERR;
    }
  }

  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
  m_valid = false;
}

CacheObj::~CacheObj()
{
  INKCacheEleDestroy(m_ele);
}

char *
CacheObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  char *pd_str, *time_str;
  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  pd_str = pdest_sspec_to_string(m_ele->cache_info.pd_type, m_ele->cache_info.pd_val, &(m_ele->cache_info.sec_spec));
  if (!pd_str) {
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }
  strncat(buf, pd_str, sizeof(buf) - strlen(buf) - 1);
  xfree(pd_str);

  switch (m_ele->cfg_ele.type) {
  case INK_CACHE_NEVER:
    strncat(buf, "action=never-cache ", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_CACHE_IGNORE_NO_CACHE:
    strncat(buf, "action=ignore-no-cache ", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_CACHE_IGNORE_CLIENT_NO_CACHE:
    strncat(buf, "action=ignore-client-no-cache ", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_CACHE_IGNORE_SERVER_NO_CACHE:
    strncat(buf, "action=ignore-server-no-cache ", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_CACHE_AUTH_CONTENT:
    strncat(buf, "action=cache-auth-content ", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_CACHE_PIN_IN_CACHE:
    strncat(buf, "pin-in-cache=", sizeof(buf) - strlen(buf) - 1);
    time_str = hms_time_to_string(m_ele->time_period);
    if (time_str) {
      strncat(buf, time_str, sizeof(buf) - strlen(buf) - 1);
      xfree(time_str);
    }
    strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_CACHE_REVALIDATE:
    strncat(buf, "revalidate=", sizeof(buf) - strlen(buf) - 1);
    time_str = hms_time_to_string(m_ele->time_period);
    if (time_str) {
      strncat(buf, time_str, sizeof(buf) - strlen(buf) - 1);
      xfree(time_str);
    }
    strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_CACHE_TTL_IN_CACHE:
    strncat(buf, "ttl-in-cache=", sizeof(buf) - strlen(buf) - 1);
    time_str = hms_time_to_string(m_ele->time_period);
    if (time_str) {
      strncat(buf, time_str, sizeof(buf) - strlen(buf) - 1);
      xfree(time_str);
    }
    strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
    break;
  default:
    // Handled here:
    // Lots of cases...
    break;
  }

  return xstrdup(buf);
}

bool CacheObj::isValid()
{
  char *
    timeStr;

  if (m_ele->cfg_ele.error != INK_ERR_OKAY) {
    m_valid = false;
  }
  // all Cache Ele's should have a prim dest, sec specs are optional
  if (!ccu_checkPdSspec(m_ele->cache_info)) {
    m_valid = false;
  }
  // only pin-in-cache, ttl, and revalidate rules have time period
  switch (m_ele->cfg_ele.type) {
  case INK_CACHE_NEVER:
  case INK_CACHE_IGNORE_NO_CACHE:
  case INK_CACHE_IGNORE_CLIENT_NO_CACHE:
  case INK_CACHE_IGNORE_SERVER_NO_CACHE:
  case INK_CACHE_AUTH_CONTENT:
    break;
  case INK_CACHE_PIN_IN_CACHE:
  case INK_CACHE_REVALIDATE:
  case INK_CACHE_TTL_IN_CACHE:
    timeStr = hms_time_to_string(m_ele->time_period);
    if (!timeStr) {
      m_valid = false;
    }
    if (timeStr)
      xfree(timeStr);
  default:
    // Handled here:
    // Lots of cases ...
    break;
  }

  if (!m_valid)
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;

  return m_valid;
}

INKCfgEle *
CacheObj::getCfgEleCopy()
{
  return (INKCfgEle *) copy_cache_ele(m_ele);
}


//--------------------------------------------------------------------------
// CongestionObj
//--------------------------------------------------------------------------
CongestionObj::CongestionObj(INKCongestionEle * ele)
{
  m_ele = ele;
  m_valid = true;
  m_valid = isValid();
}

CongestionObj::CongestionObj(TokenList * tokens)
{
  Token *tok;
  m_ele = INKCongestionEleCreate();
  m_ele->cfg_ele.error = INK_ERR_OKAY;
  m_valid = true;

  if (!tokens) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, INK_FNAME_CONGESTION);

  // if any invalid values, set m_valid=false
  // convert token name and value into ele field
  tok = tokens->first();
  //tok = tokens_to_pdss_format(tokens, tok, &(m_ele->congestion_info));

  if (!tok) {                   // INVALID FORMAT
    goto FORMAT_ERR;
  }

  if (strcmp(tok->name, "dest_domain") == 0) {
    m_ele->pd_type = INK_PD_DOMAIN;
  } else if (strcmp(tok->name, "dest_host") == 0) {
    m_ele->pd_type = INK_PD_HOST;
  } else if (strcmp(tok->name, "dest_ip") == 0) {
    m_ele->pd_type = INK_PD_IP;
  } else if (strcmp(tok->name, "host_regex") == 0) {
    m_ele->pd_type = INK_PD_URL_REGEX;
  }
  m_ele->pd_val = xstrdup(tok->value);

  // check for remaining tags
  tok = tokens->next(tok);
  while (tok) {
    if (!tok->name || !tok->value) {
      goto FORMAT_ERR;
    }
    if (strcmp(tok->name, "prefix") == 0) {
      m_ele->prefix = xstrdup(tok->value);
    } else if (strcmp(tok->name, "port") == 0) {
      m_ele->port = ink_atoi(tok->value);
    } else if (strcmp(tok->name, "congestion_scheme") == 0) {
      if (strcmp(tok->value, "per_ip") == 0) {
        m_ele->scheme = INK_HTTP_CONGEST_PER_IP;
      } else if (strcmp(tok->value, "per_host") == 0) {
        m_ele->scheme = INK_HTTP_CONGEST_PER_HOST;
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
      m_ele->error_page_uri = xstrdup(tok->value);
    } else {
      goto FORMAT_ERR;
    }
    tok = tokens->next(tok);
  }
  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
  m_valid = false;
}

CongestionObj::~CongestionObj()
{
  INKCongestionEleDestroy(m_ele);
}

//
// will always print defaults in the rule
// 
char *
CongestionObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  char buf[MAX_BUF_SIZE];
  size_t pos = 0;
  int psize;
  memset(buf, 0, MAX_BUF_SIZE);

  // push in primary destination
  if (pos < sizeof(buf)) {
    switch (m_ele->pd_type) {
    case INK_PD_DOMAIN:
      psize = snprintf(buf + pos, sizeof(buf) - pos, "dest_domain=%s ", m_ele->pd_val);
      break;
    case INK_PD_HOST:
      psize = snprintf(buf + pos, sizeof(buf) - pos, "dest_host=%s ", m_ele->pd_val);
      break;
    case INK_PD_IP:
      psize = snprintf(buf + pos, sizeof(buf) - pos, "dest_ip=%s ", m_ele->pd_val);
      break;
    case INK_PD_URL_REGEX:
      psize = snprintf(buf + pos, sizeof(buf) - pos, "host_regex=%s ", m_ele->pd_val);
      break;
    default:
      psize = 0;
      // Handled here:
      // INK_PD_UNDEFINED
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
      (psize =
       snprintf(buf + pos, sizeof(buf) - pos, "max_connection_failures=%d ", m_ele->max_connection_failures)) > 0)
    pos += psize;
  if (pos < sizeof(buf) &&
      (psize = snprintf(buf + pos, sizeof(buf) - pos, "fail_window=%d ", m_ele->fail_window)) > 0)
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
  if (pos < sizeof(buf) &&
      (psize = snprintf(buf + pos, sizeof(buf) - pos, "max_connection=%d ", m_ele->max_connection)) > 0)
    pos += psize;
  if (m_ele->error_page_uri) {
    if (pos < sizeof(buf) &&
        (psize = snprintf(buf + pos, sizeof(buf) - pos, "error_page=%s ", m_ele->error_page_uri)) > 0)
      pos += psize;
  }
  switch (m_ele->scheme) {
  case INK_HTTP_CONGEST_PER_IP:
    if (pos<sizeof(buf) && (psize = snprintf(buf + pos, sizeof(buf) - pos, "congestion_scheme=per_ip "))> 0)
      pos += psize;
    break;
  case INK_HTTP_CONGEST_PER_HOST:
    if (pos<sizeof(buf) && (psize = snprintf(buf + pos, sizeof(buf) - pos, "congestion_scheme=per_host "))> 0)
      pos += psize;
    break;
  default:
    ;
  }

  return xstrdup(buf);
}

bool CongestionObj::isValid()
{
  if (m_ele->cfg_ele.error != INK_ERR_OKAY) {
    m_valid = false;
  }
  // all Congestion Ele's should have a prim dest, sec specs are optional
  if (!m_ele->pd_val)
    m_valid = false;

  if (!m_valid)
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
  return m_valid;
}

INKCfgEle *
CongestionObj::getCfgEleCopy()
{
  return (INKCfgEle *) copy_congestion_ele(m_ele);
}

//--------------------------------------------------------------------------
// FilterObj
//--------------------------------------------------------------------------
FilterObj::FilterObj(INKFilterEle * ele)
{
  m_ele = ele;
  m_valid = true;
  m_valid = isValid();
}

FilterObj::FilterObj(TokenList * tokens)
{
  Token *tok;

  m_ele = INKFilterEleCreate(INK_TYPE_UNDEFINED);
  m_ele->cfg_ele.error = INK_ERR_OKAY;
  m_valid = true;

  if (!tokens)
    goto FORMAT_ERR;

  // set ele type
  m_ele->cfg_ele.type = get_rule_type(tokens, INK_FNAME_FILTER);
  if (m_ele->cfg_ele.type == INK_TYPE_UNDEFINED)
    goto FORMAT_ERR;

  tok = tokens->first();
  tok = tokens_to_pdss_format(tokens, tok, &(m_ele->filter_info));
  if (!tok)
    goto FORMAT_ERR;


  // must have another token 
  tok = tokens->next(tok);
  if (!tok || !tok->value)
    goto FORMAT_ERR;

  // Some sanity check
  if ((strcmp(tok->name, "action") != 0) &&
      (strcmp(tok->name, "keep_hdr") != 0) && (strcmp(tok->name, "strip_hdr") != 0))
    goto FORMAT_ERR;

  if (strcmp(tok->name, "action") == 0) {
    if ((strcmp(tok->value, "allow") != 0) &&
        (strcmp(tok->value, "deny") != 0) &&
        (strcmp(tok->value, "ldap") != 0) && (strcmp(tok->value, "ntlm") != 0) && (strcmp(tok->value, "radius") != 0))
      goto FORMAT_ERR;

  } else {                      // tok->name must either be keep_hdr or strip_hdr.
    if (strcmp(tok->value, "date") == 0) {
      m_ele->hdr = INK_HDR_DATE;
    } else if (strcmp(tok->value, "host") == 0) {
      m_ele->hdr = INK_HDR_HOST;
    } else if (strcmp(tok->value, "cookie") == 0) {
      m_ele->hdr = INK_HDR_COOKIE;
    } else if (strcmp(tok->value, "client_ip") == 0) {
      m_ele->hdr = INK_HDR_CLIENT_IP;
    } else {
      m_ele->hdr = INK_HDR_UNDEFINED;
      goto FORMAT_ERR;
    }
  }

  if (m_ele->cfg_ele.type == INK_FILTER_LDAP ||
      m_ele->cfg_ele.type == INK_FILTER_NTLM || m_ele->cfg_ele.type == INK_FILTER_RADIUS) {
    tok = tokens->next(tok);
    while (tok) {
      if (!strcmp(tok->name, "server")) {
        if ((!tok->value) || (m_ele->server)) {
          goto FORMAT_ERR;
        }
        m_ele->server = xstrdup(tok->value);
      } else if (!strcmp(tok->name, "dn")) {
        if ((!tok->value) || (m_ele->dn)) {
          goto FORMAT_ERR;
        }
        m_ele->dn = xstrdup(tok->value);
      } else if (!strcmp(tok->name, "realm")) {
        if ((!tok->value) || (m_ele->realm)) {
          goto FORMAT_ERR;
        }
        m_ele->realm = xstrdup(tok->value);
      } else if (!strcmp(tok->name, "uid_filter")) {
        if ((!tok->value) || (m_ele->uid_filter)) {
          goto FORMAT_ERR;
        }
        m_ele->uid_filter = xstrdup(tok->value);
      } else if (!strcmp(tok->name, "attr")) {
        if ((!tok->value) || (m_ele->attr)) {
          goto FORMAT_ERR;
        }
        m_ele->attr = xstrdup(tok->value);
      } else if (!strcmp(tok->name, "attr_val")) {
        if ((!tok->value) || (m_ele->attr_val)) {
          goto FORMAT_ERR;
        }
        m_ele->attr_val = xstrdup(tok->value);
      } else if (!strcmp(tok->name, "redirect_url")) {
        if ((!tok->value) || (m_ele->redirect_url)) {
          goto FORMAT_ERR;
        }
        m_ele->redirect_url = xstrdup(tok->value);
      } else if (!strcmp(tok->name, "bind_dn")) {
        if ((!tok->value) || (m_ele->bind_dn)) {
          goto FORMAT_ERR;
        }
        m_ele->bind_dn = xstrdup(tok->value);
      } else if (!strcmp(tok->name, "bind_pwd_file")) {
        if ((!tok->value) || (m_ele->bind_pwd_file)) {
          goto FORMAT_ERR;
        }
        m_ele->bind_pwd_file = xstrdup(tok->value);
      } else {
        goto FORMAT_ERR;
      }
      tok = tokens->next(tok);
    }
  } else {
    // Sanity check -- should have no more token 
    if (tokens->next(tok)) {
      goto FORMAT_ERR;
    }
  }

  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
  m_valid = false;
}

FilterObj::~FilterObj()
{
  INKFilterEleDestroy(m_ele);
}

char *
FilterObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  char *pd_str;
  char buf[MAX_RULE_SIZE];
  size_t buf_pos = 0;
  int psize;

  memset(buf, 0, MAX_RULE_SIZE);

  pd_str = pdest_sspec_to_string(m_ele->filter_info.pd_type, m_ele->filter_info.pd_val, &(m_ele->filter_info.sec_spec));
  if (!pd_str) {
    return NULL;
  }

  if (buf_pos<sizeof(buf) && (psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%s", pd_str))> 0)
    buf_pos += psize;

  xfree(pd_str);

  if (buf_pos < sizeof(buf)) {
    switch (m_ele->cfg_ele.type) {
    case INK_FILTER_ALLOW:
      psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "action=allow");
      break;
    case INK_FILTER_DENY:
      psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "action=deny");
      break;
    case INK_FILTER_LDAP:
      psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "action=ldap");
      break;
    case INK_FILTER_NTLM:
      psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "action=ntlm");
      break;
    case INK_FILTER_RADIUS:
      psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "action=radius");
      break;
    case INK_FILTER_KEEP_HDR:
      psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "keep_hdr=");
      break;
    case INK_FILTER_STRIP_HDR:
      psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "strip_hdr=");
      break;
    default:
      psize = 0;
      // Handled here:
      // Lots of cases ...
      break;
    }
    if (psize > 0)
      buf_pos += psize;
  }
  // Just for keep_hdr or strip_hdr
  if (buf_pos < sizeof(buf)) {
    switch (m_ele->cfg_ele.type) {
    case INK_FILTER_KEEP_HDR:
    case INK_FILTER_STRIP_HDR:
      switch (m_ele->hdr) {
      case INK_HDR_DATE:
        psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "date");
        break;
      case INK_HDR_HOST:
        psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "host");
        break;
      case INK_HDR_COOKIE:
        psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "cookie");
        break;
      case INK_HDR_CLIENT_IP:
        psize = snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "client_ip");
        break;
      default:
        return NULL;
      }
    default:
      psize = 0;
      break;
    }
    if (psize > 0)
      buf_pos += psize;
  }
  // Any LDAP/NTLM specific tag/name
  if (m_ele->cfg_ele.type == INK_FILTER_LDAP ||
      m_ele->cfg_ele.type == INK_FILTER_NTLM || m_ele->cfg_ele.type == INK_FILTER_RADIUS) {
    if (m_ele->server) {
      char *temp = m_ele->server;
      bool quoteNeeded = (strstr(temp, " ") || strstr(temp, "="));
      strncat(buf, " server=", sizeof(buf) - strlen(buf) - 1);
      if (quoteNeeded) {
        strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
      }
      strncat(buf, m_ele->server, sizeof(buf) - strlen(buf) - 1);
      if (quoteNeeded) {
        strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
      }
    }
    if (m_ele->dn) {
      char *temp = m_ele->dn;
      bool quoteNeeded = (strstr(temp, " ") || strstr(temp, "="));
      strncat(buf, " dn=", sizeof(buf) - strlen(buf) - 1);
      if (quoteNeeded) {
        strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
      }
      strncat(buf, m_ele->dn, sizeof(buf) - strlen(buf) - 1);
      if (quoteNeeded) {
        strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
      }
    }
    if (m_ele->realm) {
      char *temp = m_ele->realm;
      bool quoteNeeded = (strstr(temp, " ") || strstr(temp, "="));
      strncat(buf, " realm=", sizeof(buf) - strlen(buf) - 1);
      if (quoteNeeded) {
        strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
      }
      strncat(buf, m_ele->realm, sizeof(buf) - strlen(buf) - 1);
      if (quoteNeeded) {
        strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
      }
    }
    if (m_ele->uid_filter) {
      char *temp = m_ele->uid_filter;
      bool quoteNeeded = (strstr(temp, " ") || strstr(temp, "="));
      strncat(buf, " uid_filter=", sizeof(buf) - strlen(buf) - 1);
      if (quoteNeeded) {
        strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
      }
      strncat(buf, m_ele->uid_filter, sizeof(buf) - strlen(buf) - 1);
      if (quoteNeeded) {
        strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
      }
    }
    if (m_ele->attr) {
      char *temp = m_ele->attr;
      bool quoteNeeded = (strstr(temp, " ") || strstr(temp, "="));
      strncat(buf, " attr=", sizeof(buf) - strlen(buf) - 1);
      if (quoteNeeded) {
        strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
      }
      strncat(buf, m_ele->attr, sizeof(buf) - strlen(buf) - 1);
      if (quoteNeeded) {
        strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
      }
    }
    if (m_ele->attr_val) {
      char *temp = m_ele->attr_val;
      bool quoteNeeded = (strstr(temp, " ") || strstr(temp, "="));
      strncat(buf, " attr_val=", sizeof(buf) - strlen(buf) - 1);
      if (quoteNeeded) {
        strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
      }
      strncat(buf, m_ele->attr_val, sizeof(buf) - strlen(buf) - 1);
      if (quoteNeeded) {
        strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
      }
    }
    if (m_ele->redirect_url) {
      char *temp = m_ele->redirect_url;
      bool quoteNeeded = (strstr(temp, " ") || strstr(temp, "="));
      strncat(buf, " redirect_url=", sizeof(buf) - strlen(buf) - 1);
      if (quoteNeeded) {
        strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
      }
      strncat(buf, m_ele->redirect_url, sizeof(buf) - strlen(buf) - 1);
      if (quoteNeeded) {
        strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
      }
    }
    if (m_ele->bind_dn) {
      char *temp = m_ele->bind_dn;
      bool quoteNeeded = (strstr(temp, " ") || strstr(temp, "="));
      strncat(buf, " bind_dn=", sizeof(buf) - strlen(buf) - 1);
      if (quoteNeeded) {
        strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
      }
      strncat(buf, m_ele->bind_dn, sizeof(buf) - strlen(buf) - 1);
      if (quoteNeeded) {
        strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
      }
    }
    if (m_ele->bind_pwd_file) {
      char *temp = m_ele->bind_pwd_file;
      bool quoteNeeded = (strstr(temp, " ") || strstr(temp, "="));
      strncat(buf, " bind_pwd_file=", sizeof(buf) - strlen(buf) - 1);
      if (quoteNeeded) {
        strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
      }
      strncat(buf, m_ele->bind_pwd_file, sizeof(buf) - strlen(buf) - 1);
      if (quoteNeeded) {
        strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
      }
    }
  }
  return xstrdup(buf);
}

bool FilterObj::isValid()
{
  if (m_ele->cfg_ele.error != INK_ERR_OKAY) {
    m_valid = false;
  }

  if (!ccu_checkPdSspec(m_ele->filter_info)) {
    m_valid = false;
  }

  switch (m_ele->cfg_ele.type) {
  case INK_FILTER_KEEP_HDR:
  case INK_FILTER_STRIP_HDR:
    switch (m_ele->hdr) {
    case INK_HDR_DATE:
    case INK_HDR_HOST:
    case INK_HDR_COOKIE:
    case INK_HDR_CLIENT_IP:
      break;
    default:
      m_valid = false;
    }
    break;
  case INK_FILTER_ALLOW:
  case INK_FILTER_DENY:
  case INK_FILTER_RADIUS:
    if (m_ele->hdr != INK_HDR_UNDEFINED)
      m_valid = false;
    break;
  case INK_FILTER_NTLM:
  case INK_FILTER_LDAP:
    // if ldap server is specified, uid-filter, base-dn must be too
    if (m_ele->server || m_ele->dn || m_ele->uid_filter) {
      if (!m_ele->server || !m_ele->dn || !m_ele->uid_filter) {
        m_valid = false;
      }
    }
    break;
  default:
    m_valid = false;
  }


  // if one or more of LDAP action optional parameters is specified:
  // (1) must be LDAP or NTLM rule
  // (1) if LDAP rule, server=, dn=, uid_filter= must be present
  if (m_ele->attr || m_ele->attr_val || m_ele->bind_dn || m_ele->bind_pwd_file) {
    if (m_ele->cfg_ele.type != INK_FILTER_LDAP && m_ele->cfg_ele.type != INK_FILTER_NTLM) {
      m_valid = false;
    }
    if (m_ele->cfg_ele.type == INK_FILTER_LDAP) {
      if ((!m_ele->dn) || (!m_ele->server) || (!m_ele->uid_filter)) {
        m_valid = false;
      }
    }
  }
  // realm and redirect_url can only be specified for LDAP, radius or NTLM rules
  if (m_ele->realm || m_ele->redirect_url) {
    if (m_ele->cfg_ele.type != INK_FILTER_LDAP &&
        m_ele->cfg_ele.type != INK_FILTER_NTLM && m_ele->cfg_ele.type != INK_FILTER_RADIUS) {
      m_valid = false;
    }
    if (m_ele->cfg_ele.type == INK_FILTER_LDAP && (!m_ele->dn || !m_ele->server || !m_ele->uid_filter)) {
      m_valid = false;
    }
  }
  // bind_dn and bind_pwd_file must both be specified
  if (m_ele->bind_dn || m_ele->bind_pwd_file) {
    if (!m_ele->bind_dn || !m_ele->bind_pwd_file) {
      m_valid = false;
    }
  }

  if (!m_valid)
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;

  return m_valid;
}

INKCfgEle *
FilterObj::getCfgEleCopy()
{
  return (INKCfgEle *) copy_filter_ele(m_ele);
}

//--------------------------------------------------------------------------
// HostingObj
//--------------------------------------------------------------------------
HostingObj::HostingObj(INKHostingEle * ele)
{
  m_ele = ele;
  m_valid = true;
  m_valid = isValid();
}

HostingObj::HostingObj(TokenList * tokens)
{
  Token *token;

  m_ele = INKHostingEleCreate();
  m_ele->cfg_ele.error = INK_ERR_OKAY;
  m_valid = true;

  if (!tokens || tokens->length != 2) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, INK_FNAME_HOSTING);
  if (m_ele->cfg_ele.type == INK_TYPE_UNDEFINED) {
    goto FORMAT_ERR;
  }
  // First Token
  token = tokens->first();
  if (!token->value) {
    goto FORMAT_ERR;
  }
  if (strcmp(token->name, "hostname") == 0) {
    m_ele->pd_type = INK_PD_HOST;
  } else if (strcmp(token->name, "domain") == 0) {
    m_ele->pd_type = INK_PD_DOMAIN;
  } else {
    goto FORMAT_ERR;
  }
  m_ele->pd_val = xstrdup(token->value);

  // Second Token
  token = tokens->next(token);
  if (!token->value) {
    goto FORMAT_ERR;
  }
  if (strcmp(token->name, "partition") != 0) {
    goto FORMAT_ERR;
  }
  m_ele->partitions = string_to_int_list(token->value, LIST_DELIMITER);
  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
  m_valid = false;

}

HostingObj::~HostingObj()
{
  INKHostingEleDestroy(m_ele);
}

char *
HostingObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  char *list_str;
  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  switch (m_ele->pd_type) {
  case INK_PD_HOST:
    strncat(buf, "hostname=", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_PD_DOMAIN:
    strncat(buf, "domain=", sizeof(buf) - strlen(buf) - 1);
    break;
  default:
    // Handled here:
    // Lots of cases...
    break;
  }

  list_str = int_list_to_string(m_ele->partitions, ",");
  strncat(buf, m_ele->pd_val, sizeof(buf) - strlen(buf) - 1);
  strncat(buf, " partition=", sizeof(buf) - strlen(buf) - 1);
  strncat(buf, list_str, sizeof(buf) - strlen(buf) - 1);
  xfree(list_str);

  return xstrdup(buf);
}

bool HostingObj::isValid()
{
  int *
    part;
  int
    len,
    i;

  if (m_ele->pd_type == INK_PD_UNDEFINED) {
    m_valid = false;
    goto Lend;
  }

  if (!m_ele->pd_val) {
    m_valid = false;
    goto Lend;
  }

  if (!m_ele->partitions || !INKIntListIsValid(m_ele->partitions, 0, 50000)) {
    m_valid = false;
    goto Lend;
  }
  // check that each partition is between 1-255 
  len = INKIntListLen(m_ele->partitions);
  for (i = 0; i < len; i++) {
    part = INKIntListDequeue(m_ele->partitions);
    if (*part<1 || *part> 255) {
      INKIntListEnqueue(m_ele->partitions, part);
      m_valid = false;
      goto Lend;
    }
    INKIntListEnqueue(m_ele->partitions, part);
  }

Lend:
  if (!m_valid) {
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
  }
  return m_valid;
}

INKCfgEle *
HostingObj::getCfgEleCopy()
{
  return (INKCfgEle *) copy_hosting_ele(m_ele);
}

//--------------------------------------------------------------------------
// IcpObj
//--------------------------------------------------------------------------
IcpObj::IcpObj(INKIcpEle * ele)
{
  m_ele = ele;
  m_valid = true;
  m_valid = isValid();
}

IcpObj::IcpObj(TokenList * tokens)
{
  Token *token;
  int i;

  m_ele = INKIcpEleCreate();
  m_ele->cfg_ele.error = INK_ERR_OKAY;
  m_valid = true;

  if (!tokens || tokens->length < 8) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, INK_FNAME_ICP_PEER);
  if (m_ele->cfg_ele.type == INK_TYPE_UNDEFINED) {
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
        m_ele->peer_hostname = xstrdup(alias);
      break;
    case 1:
      if (strlen(alias) > 0) {
        m_ele->peer_host_ip_addr = string_to_ip_addr(alias);
        if (!m_ele->peer_host_ip_addr)
          goto FORMAT_ERR;
      }
      break;
    case 2:
      cache_type = ink_atoi(alias);     // what if failed?
      switch (cache_type) {
      case 1:
        m_ele->peer_type = INK_ICP_PARENT;
        break;
      case 2:
        m_ele->peer_type = INK_ICP_SIBLING;
        break;
      default:
        m_ele->peer_type = INK_ICP_UNDEFINED;
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
        m_ele->mc_ttl = INK_MC_TTL_SINGLE_SUBNET;
        break;
      case 2:
        m_ele->mc_ttl = INK_MC_TTL_MULT_SUBNET;
        break;
      default:
        m_ele->mc_ttl = INK_MC_TTL_UNDEFINED;
      }
      break;
    default:
      goto FORMAT_ERR;
    }
    token = tokens->next(token);
  }

  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
  m_valid = false;
}

IcpObj::~IcpObj()
{
  INKIcpEleDestroy(m_ele);
}

char *
IcpObj::formatEleToRule()
{
  char *ip_str1, *ip_str2;
  char buf[MAX_RULE_SIZE];
  int peer_type = 0;

  if (!isValid()) {
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  memset(buf, 0, MAX_RULE_SIZE);

  switch (m_ele->peer_type) {
  case INK_ICP_PARENT:
    peer_type = 1;
    break;
  case INK_ICP_SIBLING:
    peer_type = 2;
    break;
  default:
    // Handled here:
    // INK_ICP_UNDEFINED
    break;
  }

  // optional field
  if (m_ele->peer_host_ip_addr)
    ip_str1 = ip_addr_to_string(m_ele->peer_host_ip_addr);
  else
    ip_str1 = xstrdup("");

  // optional field
  if (m_ele->mc_ip_addr)
    ip_str2 = ip_addr_to_string(m_ele->mc_ip_addr);
  else
    ip_str2 = xstrdup("0.0.0.0");

  if (m_ele->peer_hostname) {
    snprintf(buf, sizeof(buf), "%s:%s:%d:%d:%d:%d:%s:",
             m_ele->peer_hostname,
             ip_str1, peer_type, m_ele->peer_proxy_port, m_ele->peer_icp_port, (m_ele->is_multicast ? 1 : 0), ip_str2);
  } else {
    snprintf(buf, sizeof(buf), ":%s:%d:%d:%d:%d:%s:",
             ip_str1, peer_type, m_ele->peer_proxy_port, m_ele->peer_icp_port, (m_ele->is_multicast ? 1 : 0), ip_str2);
  }

  switch (m_ele->mc_ttl) {
  case INK_MC_TTL_SINGLE_SUBNET:
    strncat(buf, "1:", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_MC_TTL_MULT_SUBNET:
    strncat(buf, "2:", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_MC_TTL_UNDEFINED:
    strncat(buf, "0:", sizeof(buf) - strlen(buf) - 1);
    break;
  }

  if (ip_str1)
    xfree(ip_str1);
  if (ip_str2)
    xfree(ip_str2);
  return xstrdup(buf);
}

bool IcpObj::isValid()
{
  if (m_ele->cfg_ele.error != INK_ERR_OKAY) {
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
  if (m_ele->peer_type == INK_ICP_UNDEFINED) {
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
    if (!ccu_checkIpAddr(m_ele->mc_ip_addr, "224.0.0.0", "239.255.255.255") || m_ele->mc_ttl == INK_MC_TTL_UNDEFINED)
      m_valid = false;
  } else {                      // multicast disabled; only valid mc ip is "0.0.0.0"
    if (m_ele->mc_ip_addr && strcmp(m_ele->mc_ip_addr, "0.0.0.0") != 0)
      m_valid = false;
  }

  if (!m_valid) {
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
  }

  return m_valid;
}

INKCfgEle *
IcpObj::getCfgEleCopy()
{
  return (INKCfgEle *) copy_icp_ele(m_ele);
}

//--------------------------------------------------------------------------
// IpAllowObj
//--------------------------------------------------------------------------
IpAllowObj::IpAllowObj(INKIpAllowEle * ele)
{
  m_ele = ele;
  m_valid = true;
  m_valid = isValid();
}

IpAllowObj::IpAllowObj(TokenList * tokens)
{
  Token *token;

  m_ele = INKIpAllowEleCreate();
  m_ele->cfg_ele.error = INK_ERR_OKAY;
  m_valid = true;

  if (!tokens || (tokens->length != 2)) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, INK_FNAME_IP_ALLOW);
  if (m_ele->cfg_ele.type == INK_TYPE_UNDEFINED) {
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
      m_ele->action = INK_IP_ALLOW_ALLOW;
    } else if (strcmp(token->value, "ip_deny") == 0) {
      m_ele->action = INK_IP_ALLOW_DENY;
    } else {
      m_ele->action = INK_IP_ALLOW_UNDEFINED;
    }
  }
  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
  m_valid = false;
}

IpAllowObj::~IpAllowObj()
{
  INKIpAllowEleDestroy(m_ele);
}

char *
IpAllowObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
    return NULL;;
  }

  char *rule;
  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  ink_strncpy(buf, "src_ip=", sizeof(buf));
  if (m_ele->src_ip_addr) {
    char *ip_str = ip_addr_ele_to_string(m_ele->src_ip_addr);
    if (ip_str) {
      strncat(buf, ip_str, sizeof(buf) - strlen(buf) - 1);
      xfree(ip_str);
    }
  }

  strncat(buf, " action=", sizeof(buf) - strlen(buf) - 1);
  switch (m_ele->action) {
  case INK_IP_ALLOW_ALLOW:
    strncat(buf, "ip_allow", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_IP_ALLOW_DENY:
    strncat(buf, "ip_deny", sizeof(buf) - strlen(buf) - 1);
    break;
  default:
    // Handled here:
    // INK_IP_ALLOW_UNDEFINED
    break;
  }

  rule = xstrdup(buf);
  return rule;
}

bool IpAllowObj::isValid()
{
  if (m_ele->cfg_ele.error != INK_ERR_OKAY) {
    m_valid = false;
  }

  if (!m_ele->src_ip_addr) {
    m_valid = false;
  }

  switch (m_ele->action) {
  case INK_IP_ALLOW_ALLOW:
  case INK_IP_ALLOW_DENY:
    break;
  default:
    m_valid = false;
  }

  if (!m_valid) {
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
  }
  return m_valid;
}

INKCfgEle *
IpAllowObj::getCfgEleCopy()
{
  return (INKCfgEle *) copy_ip_allow_ele(m_ele);
}

//--------------------------------------------------------------------------
// MgmtAllowObj
//--------------------------------------------------------------------------
MgmtAllowObj::MgmtAllowObj(INKMgmtAllowEle * ele)
{
  m_ele = ele;
  m_valid = true;
  m_valid = isValid();
}

MgmtAllowObj::MgmtAllowObj(TokenList * tokens)
{
  Token *token;

  m_ele = INKMgmtAllowEleCreate();
  m_ele->cfg_ele.error = INK_ERR_OKAY;
  m_valid = true;

  if (!tokens || (tokens->length != 2)) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, INK_FNAME_IP_ALLOW);
  if (m_ele->cfg_ele.type == INK_TYPE_UNDEFINED) {
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
      m_ele->action = INK_MGMT_ALLOW_ALLOW;
    } else if (strcmp(token->value, "ip_deny") == 0) {
      m_ele->action = INK_MGMT_ALLOW_DENY;
    } else {
      m_ele->action = INK_MGMT_ALLOW_UNDEFINED;
    }
  }
  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
  m_valid = false;
}

MgmtAllowObj::~MgmtAllowObj()
{
  INKMgmtAllowEleDestroy(m_ele);
}

char *
MgmtAllowObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  char *rule;
  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  ink_strncpy(buf, "src_ip=", sizeof(buf));
  if (m_ele->src_ip_addr) {
    char *ip_str = ip_addr_ele_to_string(m_ele->src_ip_addr);
    if (ip_str) {
      strncat(buf, ip_str, sizeof(buf) - strlen(buf) - 1);
      xfree(ip_str);
    } else {
      return NULL;
    }
  }

  strncat(buf, " action=", sizeof(buf) - strlen(buf) - 1);
  switch (m_ele->action) {
  case INK_MGMT_ALLOW_ALLOW:
    strncat(buf, "ip_allow", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_MGMT_ALLOW_DENY:
    strncat(buf, "ip_deny", sizeof(buf) - strlen(buf) - 1);
    break;
  default:
    // Handled here:
    // INK_MGMT_ALLOW_UNDEFINED
    break;
  }

  rule = xstrdup(buf);

  return rule;
}

bool MgmtAllowObj::isValid()
{
  if (m_ele->cfg_ele.error != INK_ERR_OKAY) {
    m_valid = false;
  }
  // must specify source IP addr 
  if (!m_ele->src_ip_addr) {
    m_valid = false;
  }

  switch (m_ele->action) {
  case INK_MGMT_ALLOW_ALLOW:
  case INK_MGMT_ALLOW_DENY:
    break;
  default:
    m_valid = false;
  }

  if (!m_valid) {
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
  }
  return m_valid;
}

INKCfgEle *
MgmtAllowObj::getCfgEleCopy()
{
  return (INKCfgEle *) copy_mgmt_allow_ele(m_ele);
}


//--------------------------------------------------------------------------
// ParentProxyObj
//--------------------------------------------------------------------------
ParentProxyObj::ParentProxyObj(INKParentProxyEle * ele)
{
  m_ele = ele;
  m_valid = true;
  m_valid = isValid();
}

ParentProxyObj::ParentProxyObj(TokenList * tokens)
{
  Token *tok;
  m_ele = INKParentProxyEleCreate(INK_TYPE_UNDEFINED);
  m_ele->cfg_ele.error = INK_ERR_OKAY;
  m_valid = true;

  if (!tokens || tokens->length < 1) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, INK_FNAME_PARENT_PROXY);
  if (m_ele->cfg_ele.type == INK_TYPE_UNDEFINED) {
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
        m_ele->rr = INK_RR_TRUE;
      } else if (strcmp(tok->value, "strict") == 0) {
        m_ele->rr = INK_RR_STRICT;
      } else if (strcmp(tok->value, "false") == 0) {
        m_ele->rr = INK_RR_FALSE;
      } else {
        m_ele->rr = INK_RR_NONE;
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
  case INK_PP_GO_DIRECT:
    m_ele->direct = true;
    break;
  case INK_PP_PARENT:
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
  INKParentProxyEleDestroy(m_ele);
}

char *
ParentProxyObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  char *pd_str, *list_str;
  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  pd_str = pdest_sspec_to_string(m_ele->parent_info.pd_type, m_ele->parent_info.pd_val, &(m_ele->parent_info.sec_spec));
  if (!pd_str)
    return NULL;
  strncat(buf, pd_str, sizeof(buf) - strlen(buf) - 1);
  xfree(pd_str);

  // round_robin
  if ((m_ele->rr != INK_RR_NONE) && (m_ele->rr != INK_RR_UNDEFINED)) {
    if (!isspace(buf[strlen(buf) - 1])) {
      strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
    }
    strncat(buf, "round_robin=", sizeof(buf) - strlen(buf) - 1);
    switch (m_ele->rr) {
    case INK_RR_TRUE:
      strncat(buf, "true", sizeof(buf) - strlen(buf) - 1);
      break;
    case INK_RR_STRICT:
      strncat(buf, "strict", sizeof(buf) - strlen(buf) - 1);
      break;
    case INK_RR_FALSE:
      strncat(buf, "false", sizeof(buf) - strlen(buf) - 1);
      break;
    default:
      // Handled here:
      // INK_RR_NONE, INK_RR_UNDEFINED
      break;
    }
  }

  if (m_ele->proxy_list != NULL) {
    // include space delimiter if not already exist
    if (!isspace(buf[strlen(buf) - 1])) {
      strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
    }
    list_str = domain_list_to_string(m_ele->proxy_list, ";");
    strncat(buf, "parent=\"", sizeof(buf) - strlen(buf) - 1);
    if (list_str) {
      strncat(buf, list_str, sizeof(buf) - strlen(buf) - 1);
      xfree(list_str);
    }
    strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);

  }

  if (m_ele->direct) {
    // include space delimiter if not already exist
    if (!isspace(buf[strlen(buf) - 1])) {
      strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
    }
    strncat(buf, "go_direct=true", sizeof(buf) - strlen(buf) - 1);
  } else {
    if (!isspace(buf[strlen(buf) - 1])) {
      strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
    }
    strncat(buf, "go_direct=false", sizeof(buf) - strlen(buf) - 1);
  }

  return xstrdup(buf);
}

bool ParentProxyObj::isValid()
{
  if (m_ele->cfg_ele.error != INK_ERR_OKAY) {
    m_valid = false;
  }

  if (!ccu_checkPdSspec(m_ele->parent_info)) {
    m_valid = false;
  }

  if (m_ele->proxy_list && !INKDomainListIsValid(m_ele->proxy_list)) {
    m_valid = false;
  }

  if (!m_valid)
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;

  return m_valid;
}

INKCfgEle *
ParentProxyObj::getCfgEleCopy()
{
  return (INKCfgEle *) copy_parent_proxy_ele(m_ele);
}

//--------------------------------------------------------------------------
// PartitionObj
//--------------------------------------------------------------------------
PartitionObj::PartitionObj(INKPartitionEle * ele)
{
  m_ele = ele;
  m_valid = true;
  m_valid = isValid();
}

PartitionObj::PartitionObj(TokenList * tokens)
{
  Token *token;

  m_ele = INKPartitionEleCreate();
  m_ele->cfg_ele.error = INK_ERR_OKAY;
  m_valid = true;

  if (!tokens || tokens->length != 3) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, INK_FNAME_PARTITION);
  if (m_ele->cfg_ele.type == INK_TYPE_UNDEFINED) {
    goto FORMAT_ERR;
  }

  token = tokens->first();
  if (strcmp(token->name, "partition") || !token->value) {
    goto FORMAT_ERR;
  }
  m_ele->partition_num = ink_atoi(token->value);

  token = tokens->next(token);
  if (strcmp(token->name, "scheme") || !token->value) {
    goto FORMAT_ERR;
  }
  if (!strcmp(token->value, "http")) {
    m_ele->scheme = INK_PARTITION_HTTP;
  } else if (!strcmp(token->value, "mixt")) {
    m_ele->scheme = INK_PARTITION_MIXT;
  } else {
    m_ele->scheme = INK_PARTITION_UNDEFINED;
  }

  token = tokens->next(token);
  if (strcmp(token->name, "size") || !token->value) {
    goto FORMAT_ERR;
  }
  // CAUTION: we may need a tigher error check 
  if (strstr(token->value, "%")) {
    m_ele->size_format = INK_SIZE_FMT_PERCENT;
  } else {
    m_ele->size_format = INK_SIZE_FMT_ABSOLUTE;
  }
  m_ele->partition_size = ink_atoi(token->value);

  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
  m_valid = false;
}

PartitionObj::~PartitionObj()
{
  INKPartitionEleDestroy(m_ele);
}

char *
PartitionObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  snprintf(buf, sizeof(buf), "partition=%d scheme=", m_ele->partition_num);

  switch (m_ele->scheme) {
  case INK_PARTITION_HTTP:
    strncat(buf, "http", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_PARTITION_MIXT:
    strncat(buf, "mixt", sizeof(buf) - strlen(buf) - 1);
    break;
  default:
    // Handled here:
    // INK_PARTITION_UNDEFINED, INK_SIZE_FMT_ABSOLUTE, INK_SIZE_FMT_UNDEFINED
    break;
  }

  size_t pos = strlen(buf);
  snprintf(buf + pos, sizeof(buf) - pos, " size=%d", m_ele->partition_size);
  switch (m_ele->size_format) {
  case INK_SIZE_FMT_PERCENT:
    strncat(buf, "%", sizeof(buf) - strlen(buf) - 1);
    break;
  default:
    // Handled here:
    // INK_SIZE_FMT_ABSOLUTE, INK_SIZE_FMT_UNDEFINED
    break;
  }

  return xstrdup(buf);
}

bool PartitionObj::isValid()
{
  if (m_ele->cfg_ele.error != INK_ERR_OKAY) {
    m_valid = false;
  }
  // partition nubmer must be between 1-255 inclusive
  if (m_ele->partition_num < 1 || m_ele->partition_num > 255) {
    m_valid = false;
  }

  switch (m_ele->scheme) {
  case INK_PARTITION_HTTP:
  case INK_PARTITION_MIXT:
    break;
  default:
    m_valid = false;
  }

  // absolute size must be multiple of 128; percentage size <= 100
  if (m_ele->size_format == INK_SIZE_FMT_ABSOLUTE) {
    if ((m_ele->partition_size < 0) || (m_ele->partition_size % 128)) {
      m_valid = false;
    }
  } else if (m_ele->size_format == INK_SIZE_FMT_PERCENT) {
    if ((m_ele->partition_size < 0) || (m_ele->partition_size > 100)) {
      m_valid = false;
    }
  }

  if (!m_valid)
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;

  return m_valid;
}

INKCfgEle *
PartitionObj::getCfgEleCopy()
{
  return (INKCfgEle *) copy_partition_ele(m_ele);
}

//--------------------------------------------------------------------------
// PluginObj
//--------------------------------------------------------------------------
PluginObj::PluginObj(INKPluginEle * ele)
{
  m_ele = ele;
  m_valid = true;
  m_valid = isValid();
}

PluginObj::PluginObj(TokenList * tokens)
{
  Token *token;

  m_ele = INKPluginEleCreate();
  m_ele->cfg_ele.error = INK_ERR_OKAY;
  m_valid = true;

  if (!tokens || tokens->length < 1) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, INK_FNAME_PLUGIN);
  if (m_ele->cfg_ele.type == INK_TYPE_UNDEFINED) {
    goto FORMAT_ERR;
  }
  // plugin name
  token = tokens->first();
  if (strcmp(token->name, "") == 0) {
    goto FORMAT_ERR;
  }
  m_ele->name = xstrdup(token->name);

  // arguments
  token = tokens->next(token);
  while (token) {
    if (m_ele->args == INK_INVALID_LIST)
      m_ele->args = INKStringListCreate();
    if (token->name)
      INKStringListEnqueue(m_ele->args, xstrdup(token->name));
    token = tokens->next(token);
  }

  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
  m_valid = false;
}

PluginObj::~PluginObj()
{
  INKPluginEleDestroy(m_ele);
}

char *
PluginObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  char *list_str;
  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  list_str = string_list_to_string(m_ele->args, " ");
  if (list_str) {
    snprintf(buf, sizeof(buf), "%s %s", m_ele->name, list_str);
    xfree(list_str);
  } else {
    snprintf(buf, sizeof(buf), "%s", m_ele->name);
  }

  return xstrdup(buf);
}

bool PluginObj::isValid()
{
  if (m_ele->cfg_ele.error != INK_ERR_OKAY) {
    m_valid = false;
  }
  // check plugin name 
  if (!m_ele->name || strcmp(m_ele->name, "") == 0) {
    m_valid = false;
  }

  return m_valid;
}

INKCfgEle *
PluginObj::getCfgEleCopy()
{
  return (INKCfgEle *) copy_plugin_ele(m_ele);
}


//--------------------------------------------------------------------------
// RemapObj
//--------------------------------------------------------------------------
RemapObj::RemapObj(INKRemapEle * ele)
{
  m_ele = ele;
  m_valid = true;
  m_valid = isValid();
}

RemapObj::RemapObj(TokenList * tokens)
{
  Token *token;
  int numFromTok;
  int numToTok;
  short current;                // current token index
  Tokenizer fromTok(":/");
  Tokenizer toTok(":/");
  char buf[MAX_RULE_SIZE];

  m_ele = INKRemapEleCreate(INK_TYPE_UNDEFINED);
  m_ele->cfg_ele.error = INK_ERR_OKAY;
  m_valid = true;

  if (!tokens || ((tokens->length != 2) && (tokens->length != 3))) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, INK_FNAME_REMAP);
  if (m_ele->cfg_ele.type == INK_TYPE_UNDEFINED) {
    goto FORMAT_ERR;
  }
  // The first token must either be "map, "reverse_map", "redirect", and redirect_temporary
  token = tokens->first();

  // target
  token = tokens->next(token);

  if (!ccu_checkUrl(token->name)) {
    goto FORMAT_ERR;
  }
  numFromTok = fromTok.Initialize(token->name, ALLOW_EMPTY_TOKS);       // allow empty token for parse sanity check
  if (strcmp(fromTok[0], "http") == 0) {
    m_ele->from_scheme = INK_SCHEME_HTTP;
  } else if (strcmp(fromTok[0], "https") == 0) {
    m_ele->from_scheme = INK_SCHEME_HTTPS;
  } else if (strcmp(fromTok[0], "rtsp") == 0) {
    m_ele->from_scheme = INK_SCHEME_RTSP;
  } else if (strcmp(fromTok[0], "mms") == 0) {
    m_ele->from_scheme = INK_SCHEME_MMS;
  } else {
    m_ele->from_scheme = INK_SCHEME_UNDEFINED;
    goto FORMAT_ERR;
  }

  // from host
  m_ele->from_host = xstrdup(fromTok[3]);

  current = 4;
  if (fromTok[4]) {

    // from port
    m_ele->from_port = ink_atoi(fromTok[4]);
    if (m_ele->from_port != 0) {        // Does it have a port
      current++;
    } else {                    // No ports
      m_ele->from_port = INK_INVALID_PORT;
    }

    // from prefix
    if (fromTok[current]) {

      memset(buf, 0, MAX_RULE_SIZE);

      for (int i = current; fromTok[i]; i++) {
        strncat(buf, fromTok[i], sizeof(buf) - strlen(buf) - 1);
        strncat(buf, "/", sizeof(buf) - strlen(buf) - 1);
      }

      if ((token->name)[strlen(token->name) - 1] != '/') {
        buf[strlen(buf) - 1] = '\0';    // truncate the last '/'
      }

      m_ele->from_path_prefix = xstrdup(buf);
    }
  } else {
    if ((token->name)[strlen(token->name) - 1] == '/') {
      memset(buf, 0, MAX_RULE_SIZE);
      ink_strncpy(buf, m_ele->from_host, sizeof(buf));
      xfree(m_ele->from_host);
      strncat(buf, "/", sizeof(buf) - strlen(buf) - 1);
      m_ele->from_host = xstrdup(buf);
    }
  }

  if (!ccu_checkUrl(token->value)) {
    goto FORMAT_ERR;
  }
  numToTok = toTok.Initialize(token->value, ALLOW_EMPTY_TOKS);  // allow empty token for parse sanity check

  if (strcmp(toTok[0], "http") == 0) {
    m_ele->to_scheme = INK_SCHEME_HTTP;
  } else if (strcmp(toTok[0], "https") == 0) {
    m_ele->to_scheme = INK_SCHEME_HTTPS;
  } else if (strcmp(toTok[0], "rtsp") == 0) {
    m_ele->to_scheme = INK_SCHEME_RTSP;
  } else if (strcmp(toTok[0], "mms") == 0) {
    m_ele->to_scheme = INK_SCHEME_MMS;
  } else {
    m_ele->to_scheme = INK_SCHEME_UNDEFINED;
    goto FORMAT_ERR;
  }

  // to host
  m_ele->to_host = xstrdup(toTok[3]);

  current = 4;
  if (toTok[4]) {

    // to port
    m_ele->to_port = ink_atoi(toTok[4]);
    if (m_ele->to_port != 0) {  // Does it have a port
      current++;
    } else {                    // No ports
      m_ele->to_port = INK_INVALID_PORT;
    }

    // to prefix
    if (toTok[current]) {

      memset(buf, 0, MAX_RULE_SIZE);

      for (int i = current; toTok[i]; i++) {
        strncat(buf, toTok[i], sizeof(buf) - strlen(buf) - 1);
        strncat(buf, "/", sizeof(buf) - strlen(buf) - 1);
      }

      if ((token->name)[strlen(token->name) - 1] != '/') {
        buf[strlen(buf) - 1] = '\0';    // truncate the last '/'
      }

      m_ele->to_path_prefix = xstrdup(buf);
    }
  } else {
    if ((token->value)[strlen(token->value) - 1] == '/') {

      memset(buf, 0, MAX_RULE_SIZE);
      ink_strncpy(buf, m_ele->to_host, sizeof(buf));
      xfree(m_ele->to_host);
      strncat(buf, "/", sizeof(buf) - strlen(buf) - 1);
      m_ele->to_host = xstrdup(buf);

    }
  }

  // Optional MIXT tag 
  token = tokens->next(token);
  if (token) {
    if (!token->name) {
      goto FORMAT_ERR;
    }
    if (!strcmp(token->name, "RNI")) {
      m_ele->mixt = INK_MIXT_RNI;
    } else if (!strcmp(token->name, "QT")) {
      m_ele->mixt = INK_MIXT_QT;
    } else if (!strcmp(token->name, "WMT")) {
      m_ele->mixt = INK_MIXT_WMT;
    } else {
      goto FORMAT_ERR;
    }
  }

  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
  m_valid = false;
}

RemapObj::~RemapObj()
{
  INKRemapEleDestroy(m_ele);
}

char *
RemapObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  switch (m_ele->cfg_ele.type) {
  case INK_REMAP_MAP:
    strncat(buf, "map", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_REMAP_REVERSE_MAP:
    strncat(buf, "reverse_map", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_REMAP_REDIRECT:
    strncat(buf, "redirect", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_REMAP_REDIRECT_TEMP:
    strncat(buf, "redirect_temporary", sizeof(buf) - strlen(buf) - 1);
    break;
  default:
    // Handled here:
    // Lots of cases...
    break;
  }
  // space delimitor
  strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);

  // from scheme
  switch (m_ele->from_scheme) {
  case INK_SCHEME_HTTP:
    strncat(buf, "http", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_SCHEME_HTTPS:
    strncat(buf, "https", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_SCHEME_RTSP:
    strncat(buf, "rtsp", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_SCHEME_MMS:
    strncat(buf, "mms", sizeof(buf) - strlen(buf) - 1);
    break;
  default:
    // Handled here:
    // INK_SCHEME_NONE, INK_SCHEME_UNDEFINED
    break;
  }
  strncat(buf, "://", sizeof(buf) - strlen(buf) - 1);

  // from host
  if (m_ele->from_host) {
    strncat(buf, m_ele->from_host, sizeof(buf) - strlen(buf) - 1);
  }
  // from port
  if (m_ele->from_port != INK_INVALID_PORT) {
    snprintf(buf, sizeof(buf), "%s:%d", buf, m_ele->from_port);
  }
  // from host path
  if (m_ele->from_path_prefix) {
    strncat(buf, "/", sizeof(buf) - strlen(buf) - 1);
    strncat(buf, m_ele->from_path_prefix, sizeof(buf) - strlen(buf) - 1);
  }
  // space delimitor
  strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);

  // to scheme
  switch (m_ele->to_scheme) {
  case INK_SCHEME_HTTP:
    strncat(buf, "http", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_SCHEME_HTTPS:
    strncat(buf, "https", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_SCHEME_RTSP:
    strncat(buf, "rtsp", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_SCHEME_MMS:
    strncat(buf, "mms", sizeof(buf) - strlen(buf) - 1);
    break;
  default:
    // Handled here:
    // INK_SCHEME_NONE, INK_SCHEME_UNDEFINED
    break;
  }
  strncat(buf, "://", sizeof(buf) - strlen(buf) - 1);

  // to host
  if (m_ele->to_host) {
    strncat(buf, m_ele->to_host, sizeof(buf) - strlen(buf) - 1);
  }
  // to port
  if (m_ele->to_port != INK_INVALID_PORT) {
    snprintf(buf, sizeof(buf), "%s:%d", buf, m_ele->to_port);
  }
  // to host path
  if (m_ele->to_path_prefix) {
    strncat(buf, "/", sizeof(buf) - strlen(buf) - 1);
    strncat(buf, m_ele->to_path_prefix, sizeof(buf) - strlen(buf) - 1);
  }

  switch (m_ele->mixt) {
  case INK_MIXT_RNI:
    strncat(buf, " RNI", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_MIXT_QT:
    strncat(buf, " QT", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_MIXT_WMT:
    strncat(buf, " WMT", sizeof(buf) - strlen(buf) - 1);
    break;
  default:
    // Handled here:
    // INK_MIXT_UNDEFINED
    break;
  }

  return xstrdup(buf);
}

bool RemapObj::isValid()
{
  if (m_ele->cfg_ele.error != INK_ERR_OKAY) {
    m_valid = false;
  }
  // rule type
  switch (m_ele->cfg_ele.type) {
  case INK_REMAP_MAP:
  case INK_REMAP_REVERSE_MAP:
  case INK_REMAP_REDIRECT:
  case INK_REMAP_REDIRECT_TEMP:
    break;
  default:
    m_valid = false;
  }

  // from scheme
  switch (m_ele->from_scheme) {
  case INK_SCHEME_HTTP:
  case INK_SCHEME_HTTPS:
  case INK_SCHEME_RTSP:
  case INK_SCHEME_MMS:
    break;
  default:
    m_valid = false;
  }

  switch (m_ele->to_scheme) {
  case INK_SCHEME_HTTP:
  case INK_SCHEME_HTTPS:
  case INK_SCHEME_RTSP:
  case INK_SCHEME_MMS:
    break;
  default:
    m_valid = false;
  }

  // if the mixt tag is specified, the only possible scheme is "rtsp"
  if (m_ele->mixt != INK_MIXT_UNDEFINED && m_ele->from_scheme != INK_SCHEME_RTSP && m_ele->to_scheme != INK_SCHEME_RTSP) {
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
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;

  return m_valid;
}

INKCfgEle *
RemapObj::getCfgEleCopy()
{
  return (INKCfgEle *) copy_remap_ele(m_ele);
}

//--------------------------------------------------------------------------
// SocksObj
//--------------------------------------------------------------------------
SocksObj::SocksObj(INKSocksEle * ele)
{
  m_ele = ele;
  m_valid = true;
  m_valid = isValid();
}

SocksObj::SocksObj(TokenList * tokens)
{
  Token *tok;

  m_ele = INKSocksEleCreate(INK_TYPE_UNDEFINED);
  m_ele->cfg_ele.error = INK_ERR_OKAY;
  m_valid = true;

  if (!tokens) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, INK_FNAME_SOCKS);
  if (m_ele->cfg_ele.type == INK_TYPE_UNDEFINED) {
    goto FORMAT_ERR;
  }
  // Determine if it's  a "no-socks" rule or a "parent socks servers" rule 
  tok = tokens->first();
  if (strcmp(tok->name, "no_socks") == 0) {     // no-socks rule; INK_SOCKS_BYPASS

    if (m_ele->ip_addrs != NULL) {
      goto FORMAT_ERR;
    }
    m_ele->ip_addrs = string_to_ip_addr_list(tok->value, ",");
  } else if (strcmp(tok->name, "auth") == 0) {  // INK_SOCKS_AUTH rule
    if (strcmp(tok->value, "u") == 0) {
      tok = tokens->next(tok);
      if (tok && tok->name) {
        m_ele->username = xstrdup(tok->name);
      } else {
        goto FORMAT_ERR;
      }
      if (tok && tok->name) {
        tok = tokens->next(tok);
        m_ele->password = xstrdup(tok->name);
      } else {
        goto FORMAT_ERR;
      }
    } else {
      goto FORMAT_ERR;
    }
  } else {                      // multiple socks servers rule; INK_SOCKS_MULTIPLE 
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
          m_ele->rr = INK_RR_TRUE;
        } else if (strcmp(tok->value, "strict") == 0) {
          m_ele->rr = INK_RR_STRICT;
        } else if (strcmp(tok->value, "false") == 0) {
          m_ele->rr = INK_RR_FALSE;
        } else {
          m_ele->rr = INK_RR_NONE;
          goto FORMAT_ERR;      // missing value for round_robin tag
        }

      } else if (strcmp(tok->name, "parent") == 0) {
        // sanity check
        if (!tok->value) {
          goto FORMAT_ERR;
        }
        m_ele->socks_servers = string_to_domain_list(tok->value, ";");
      }
    }                           // end for loop 

  }

  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
  m_valid = false;
}

SocksObj::~SocksObj()
{
  INKSocksEleDestroy(m_ele);
}

char *
SocksObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }
  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  if (m_ele->ip_addrs != NULL) {        // INK_SOCKS_BYPASS rule 
    char *str_list = ip_addr_list_to_string((LLQ *) m_ele->ip_addrs, ",");
    if (str_list) {
      snprintf(buf, sizeof(buf), "no_socks %s", str_list);
      xfree(str_list);
    } else {
      return NULL;              // invalid ip_addr_list
    }
  } else if (m_ele->username != NULL) { // INK_SOCKS_AUTH rule 
    snprintf(buf, sizeof(buf), "auth u %s %s", m_ele->username, m_ele->password);
  } else {                      // INK_SOCKS_MULTIPLE rule 
    // destination ip 
    char *ip_str = ip_addr_ele_to_string((INKIpAddrEle *) m_ele->dest_ip_addr);
    if (ip_str) {
      strncat(buf, "dest_ip=", sizeof(buf) - strlen(buf) - 1);
      strncat(buf, ip_str, sizeof(buf) - strlen(buf) - 1);
      xfree(ip_str);
    } else {
      return NULL;              // invalid IP
    }

    // parent server list
    if (m_ele->socks_servers != NULL) {
      // include space delimiter if not already exist
      if (!isspace(buf[strlen(buf) - 1])) {
        strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
      }
      char *list_str = domain_list_to_string(m_ele->socks_servers, ";");
      if (list_str) {
        strncat(buf, "parent=\"", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, list_str, sizeof(buf) - strlen(buf) - 1);
        strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
        xfree(list_str);
      } else {
        return NULL;            // invalid list
      }
    }
    // round-robin, if specified
    if ((m_ele->rr != INK_RR_NONE) && (m_ele->rr != INK_RR_UNDEFINED)) {
      if (!isspace(buf[strlen(buf) - 1])) {
        strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
      }
      strncat(buf, "round_robin=", sizeof(buf) - strlen(buf) - 1);
      switch (m_ele->rr) {
      case INK_RR_TRUE:
        strncat(buf, "true", sizeof(buf) - strlen(buf) - 1);
        break;
      case INK_RR_STRICT:
        strncat(buf, "strict", sizeof(buf) - strlen(buf) - 1);
        break;
      case INK_RR_FALSE:
        strncat(buf, "false", sizeof(buf) - strlen(buf) - 1);
        break;
      default:
        // Handled here:
        // INK_RR_NONE, INK_RR_UNDEFINED
        break;
      }
    }
  }

  return xstrdup(buf);
}

// the rule must either have an ip addr list (exclusive) OR 
// the dest_ip_addr * socks_servers OR 
// the username and password 
bool SocksObj::isValid()
{
  if (m_ele->cfg_ele.error != INK_ERR_OKAY) {
    m_valid = false;
  }

  switch (m_ele->cfg_ele.type) {
  case INK_SOCKS_BYPASS:
    if (m_ele->dest_ip_addr || m_ele->username || m_ele->password || !INKIpAddrListIsValid(m_ele->ip_addrs)) {
      m_valid = false;
    } else {
      m_valid = true;
    }
    break;
  case INK_SOCKS_AUTH:
    if (m_ele->username == NULL || m_ele->password == NULL || m_ele->ip_addrs || m_ele->dest_ip_addr) {
      m_valid = false;
    } else {
      m_valid = true;
    }
    break;
  case INK_SOCKS_MULTIPLE:
    if (m_ele->ip_addrs || m_ele->username ||
        !(m_ele->dest_ip_addr && m_ele->socks_servers) ||
        !ccu_checkIpAddrEle(m_ele->dest_ip_addr) || !INKDomainListIsValid(m_ele->socks_servers)) {
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
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;

  return m_valid;
}

INKCfgEle *
SocksObj::getCfgEleCopy()
{
  return (INKCfgEle *) copy_socks_ele(m_ele);
}

//--------------------------------------------------------------------------
// SplitDnsObj
//--------------------------------------------------------------------------
SplitDnsObj::SplitDnsObj(INKSplitDnsEle * ele)
{
  m_ele = ele;
  m_valid = true;
  m_valid = isValid();
}

SplitDnsObj::SplitDnsObj(TokenList * tokens)
{
  Token *tok;

  m_ele = INKSplitDnsEleCreate();
  m_ele->cfg_ele.error = INK_ERR_OKAY;
  m_valid = true;

  if (!tokens || (tokens->length > 6)) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, INK_FNAME_SPLIT_DNS);
  if (m_ele->cfg_ele.type == INK_TYPE_UNDEFINED) {
    goto FORMAT_ERR;
  }

  tok = tokens->first();
  while (tok) {
    if (!strcmp(tok->name, "dest_domain")) {
      if ((m_ele->pd_type != INK_PD_UNDEFINED) || (m_ele->pd_val != NULL) || (!tok->value)) {
        // fields are already defined!!
        goto FORMAT_ERR;
      }
      m_ele->pd_type = INK_PD_DOMAIN;
      m_ele->pd_val = xstrdup(tok->value);
    } else if (strcmp(tok->name, "dest_host") == 0) {
      if ((m_ele->pd_type != INK_PD_UNDEFINED) || (m_ele->pd_val != NULL) || (!tok->value)) {
        // fields are already defined!!
        goto FORMAT_ERR;
      }
      m_ele->pd_type = INK_PD_HOST;
      m_ele->pd_val = xstrdup(tok->value);
    } else if (strcmp(tok->name, "url_regex") == 0) {
      if ((m_ele->pd_type != INK_PD_UNDEFINED) || (m_ele->pd_val != NULL) || (!tok->value)) {
        // fields are already defined!!
        goto FORMAT_ERR;
      }
      m_ele->pd_type = INK_PD_URL_REGEX;
      m_ele->pd_val = xstrdup(tok->value);
    } else if (strcmp(tok->name, "named") == 0) {
      if ((m_ele->dns_servers_addrs != NULL) || (!tok->value)) {
        // fields are already defined!!
        goto FORMAT_ERR;
      }
      m_ele->dns_servers_addrs = (INKDomainList) string_to_domain_list(tok->value, "; ");
    } else if (strcmp(tok->name, "def_domain") == 0) {
      if ((m_ele->def_domain != NULL) || (!tok->value)) {
        // fields are already defined!!
        goto FORMAT_ERR;
      }
      m_ele->def_domain = xstrdup(tok->value);
    } else if (strcmp(tok->name, "search_list") == 0) {
      if ((m_ele->search_list != NULL) || (!tok->value)) {
        // fields are already defined!!
        goto FORMAT_ERR;
      }
      m_ele->search_list = (INKDomainList) string_to_domain_list(tok->value, "; ");
    } else {
      // Not able to recongize token name
      goto FORMAT_ERR;
    }

    tok = tokens->next(tok);
  }
  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
  m_valid = false;
}

SplitDnsObj::~SplitDnsObj()
{
  INKSplitDnsEleDestroy(m_ele);
}

char *
SplitDnsObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  char *pd_name;
  switch (m_ele->pd_type) {
  case INK_PD_DOMAIN:
    pd_name = xstrdup("dest_domain");
    break;
  case INK_PD_HOST:
    pd_name = xstrdup("dest_host");
    break;
  case INK_PD_URL_REGEX:
    pd_name = xstrdup("url_regex");
    break;
  default:
    pd_name = xstrdup("");      // lv: just to make this junk workable
    // Handled here:
    // INK_PD_IP, INK_PD_UNDEFINED
    break;
  }

  if (m_ele->pd_val) {
    strncat(buf, pd_name, sizeof(buf) - strlen(buf) - 1);
    strncat(buf, "=", sizeof(buf) - strlen(buf) - 1);
    if (strstr(m_ele->pd_val, " ")) {
      strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
    }
    strncat(buf, m_ele->pd_val, sizeof(buf) - strlen(buf) - 1);
    if (strstr(m_ele->pd_val, " ")) {
      strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
    }
    strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
  }

  if (m_ele->dns_servers_addrs) {
    strncat(buf, "named=", sizeof(buf) - strlen(buf) - 1);
    char *temp = domain_list_to_string((LLQ *) m_ele->dns_servers_addrs, ";");
    if (temp) {
      if (strstr(temp, " ")) {
        strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
      }
      strncat(buf, temp, sizeof(buf) - strlen(buf) - 1);
      if (strstr(temp, " ")) {
        strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
      }
      xfree(temp);
    }
    strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
  }

  if (m_ele->def_domain) {
    strncat(buf, "def_domain=", sizeof(buf) - strlen(buf) - 1);
    if (strstr(m_ele->def_domain, " ")) {
      strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
    }
    strncat(buf, m_ele->def_domain, sizeof(buf) - strlen(buf) - 1);
    if (strstr(m_ele->def_domain, " ")) {
      strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
    }
    strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
  }

  if (m_ele->search_list) {
    strncat(buf, "search_list=", sizeof(buf) - strlen(buf) - 1);
    char *temp = domain_list_to_string(m_ele->search_list, ";");
    if (temp) {
      if (strstr(temp, " ")) {
        strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
      }
      strncat(buf, temp, sizeof(buf) - strlen(buf) - 1);
      if (strstr(temp, " ")) {
        strncat(buf, "\"", sizeof(buf) - strlen(buf) - 1);
      }
      xfree(temp);
    }
    strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
  }
  // chop the last space
  while (isspace(buf[strlen(buf) - 1])) {
    buf[strlen(buf) - 1] = '\0';
  }

  if (pd_name)
    xfree(pd_name);

  return xstrdup(buf);
}

bool SplitDnsObj::isValid()
{
  if (m_ele->cfg_ele.error != INK_ERR_OKAY) {
    m_valid = false;
  }

  switch (m_ele->pd_type) {
  case INK_PD_DOMAIN:
  case INK_PD_HOST:
  case INK_PD_URL_REGEX:
    break;
  default:
    m_valid = false;
  }

  if (!m_ele->pd_val) {
    m_valid = false;
  }

  if (!INKDomainListIsValid(m_ele->dns_servers_addrs)) {
    m_valid = false;
  }
  // search_list is optional 
  if (m_ele->search_list && !INKDomainListIsValid(m_ele->search_list)) {
    m_valid = false;
  }

  if (!m_valid)
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;

  return m_valid;
}

INKCfgEle *
SplitDnsObj::getCfgEleCopy()
{
  return (INKCfgEle *) copy_split_dns_ele(m_ele);
}

//--------------------------------------------------------------------------
// StorageObj
//--------------------------------------------------------------------------
StorageObj::StorageObj(INKStorageEle * ele)
{
  m_ele = ele;
  m_valid = true;
  m_valid = isValid();          // now validate
}

// must have at least 1 token (token-name = pathname, token-value = size (if any) )
StorageObj::StorageObj(TokenList * tokens)
{
  Token *tok;

  m_ele = INKStorageEleCreate();
  m_ele->cfg_ele.error = INK_ERR_OKAY;
  m_valid = true;

  if (!tokens || (tokens->length > 6)) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, INK_FNAME_STORAGE);
  if (m_ele->cfg_ele.type == INK_TYPE_UNDEFINED) {
    goto FORMAT_ERR;
  }
  // check first token; must exist
  tok = tokens->first();
  if (!tok->name) {
    goto FORMAT_ERR;            // no pathname specified
  } else {
    m_ele->pathname = xstrdup(tok->name);
  }

  // check if size is specified
  if (tok->value)               // size is specified in second token
    m_ele->size = ink_atoi(tok->value);

  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
  m_valid = false;
}

StorageObj::~StorageObj()
{
  INKStorageEleDestroy(m_ele);
}

char *
StorageObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  if (m_ele->size < 0) {        // if size < 0, then raw partition
    snprintf(buf, sizeof(buf), "%s", m_ele->pathname);
  } else {
    snprintf(buf, sizeof(buf), "%s %d", m_ele->pathname, m_ele->size);
  }

  return xstrdup(buf);
}

bool StorageObj::isValid()
{
  if (m_ele->cfg_ele.error != INK_ERR_OKAY) {
    m_valid = false;
  }

  if (!(m_ele->pathname))
    m_valid = false;

  if (!m_valid)
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;

  return m_valid;
}

INKCfgEle *
StorageObj::getCfgEleCopy()
{
  return (INKCfgEle *) copy_storage_ele(m_ele);
}

//--------------------------------------------------------------------------
// UpdateObj
//--------------------------------------------------------------------------
UpdateObj::UpdateObj(INKUpdateEle * ele)
{
  m_ele = ele;
  m_valid = true;
  m_valid = isValid();
}

UpdateObj::UpdateObj(TokenList * tokens)
{
  Token *token;

  m_ele = INKUpdateEleCreate();
  m_ele->cfg_ele.error = INK_ERR_OKAY;
  m_valid = true;

  if (!tokens || tokens->length < 5) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, INK_FNAME_UPDATE_URL);
  if (m_ele->cfg_ele.type == INK_TYPE_UNDEFINED) {
    goto FORMAT_ERR;
  }
  // URL
  token = tokens->first();
  if (strcmp(token->name, "") == 0) {
    goto FORMAT_ERR;
  }
  m_ele->url = xstrdup(token->name);

  // Request_headers
  token = tokens->next(token);
  m_ele->headers = string_to_string_list(token->name, ";");

  // Offset_hour
  token = tokens->next(token);
  if (strcmp(token->name, "") == 0) {
    goto FORMAT_ERR;
  }
  m_ele->offset_hour = ink_atoi(token->name);

  // Interval
  token = tokens->next(token);
  if (strcmp(token->name, "") == 0) {
    goto FORMAT_ERR;
  }
  m_ele->interval = ink_atoi(token->name);

  // Recursion_depth
  token = tokens->next(token);
  if (strcmp(token->name, "") == 0) {
    goto FORMAT_ERR;
  }
  m_ele->recursion_depth = ink_atoi(token->name);
  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
  m_valid = false;
}

UpdateObj::~UpdateObj()
{
  INKUpdateEleDestroy(m_ele);
}

char *
UpdateObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  char *list_str;
  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  list_str = string_list_to_string(m_ele->headers, ";");
  if (list_str) {
    snprintf(buf, sizeof(buf), "%s\\%s\\%d\\%d\\%d\\",
             m_ele->url, list_str, m_ele->offset_hour, m_ele->interval, m_ele->recursion_depth);
    xfree(list_str);
  } else {
    snprintf(buf, sizeof(buf), "%s\\\\%d\\%d\\%d\\",
             m_ele->url, m_ele->offset_hour, m_ele->interval, m_ele->recursion_depth);
  }

  return xstrdup(buf);
}

bool UpdateObj::isValid()
{
  if (m_ele->cfg_ele.error != INK_ERR_OKAY) {
    m_valid = false;
  }
  // check url 
  if (!m_ele->url || strcmp(m_ele->url, "") == 0 ||
      strstr(m_ele->url, "\\") ||
      (!strstr(m_ele->url, "http") && !strstr(m_ele->url, "rtsp"))) {
    m_valid = false;
  }
  // bug 49322: check that there are no "\" in the url or headers 
  char *
    list_str = string_list_to_string(m_ele->headers, ";");
  if (list_str) {
    if (strstr(list_str, "\\"))
      m_valid = false;
    xfree(list_str);
  }
  // offset hour range is 00-23 
  if (m_ele->offset_hour < 0 || m_ele->offset_hour > 23)
    m_valid = false;

  if (m_ele->interval < 0)
    m_valid = false;

  // optional - default is 0
  if (m_ele->recursion_depth < 0) {
    m_valid = false;
  }
  // recursion depth can only be specified for http
  if (m_ele->recursion_depth > 0) {
    if (!strstr(m_ele->url, "http"))
      m_valid = false;
  }

  if (!m_valid)
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;

  return m_valid;
}

INKCfgEle *
UpdateObj::getCfgEleCopy()
{
  return (INKCfgEle *) copy_update_ele(m_ele);
}

//--------------------------------------------------------------------------
// VirtIpAddrObj
//--------------------------------------------------------------------------
VirtIpAddrObj::VirtIpAddrObj(INKVirtIpAddrEle * ele)
{
  m_ele = ele;
  m_valid = true;
  m_valid = isValid();
}

VirtIpAddrObj::VirtIpAddrObj(TokenList * tokens)
{
  Token *tok;

  m_ele = INKVirtIpAddrEleCreate();
  m_ele->cfg_ele.error = INK_ERR_OKAY;
  m_valid = true;

  if (!tokens || (tokens->length != 3)) {
    goto FORMAT_ERR;
  }

  m_ele->cfg_ele.type = get_rule_type(tokens, INK_FNAME_VADDRS);
  if (m_ele->cfg_ele.type == INK_TYPE_UNDEFINED) {
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
  m_ele->intr = xstrdup(tok->name);

  // Subinterface
  tok = tokens->next(tok);
  if (tok->value != NULL) {
    goto FORMAT_ERR;
  }
  m_ele->sub_intr = ink_atoi(tok->name);        // ERROR: can't convert?

  return;

FORMAT_ERR:
  m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
  m_valid = false;
}

VirtIpAddrObj::~VirtIpAddrObj()
{
  INKVirtIpAddrEleDestroy(m_ele);
}

char *
VirtIpAddrObj::formatEleToRule()
{
  if (!isValid()) {
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
    return NULL;
  }

  char *ip_str;
  char buf[MAX_RULE_SIZE];
  memset(buf, 0, MAX_RULE_SIZE);

  ip_str = ip_addr_to_string(m_ele->ip_addr);
  snprintf(buf, sizeof(buf), "%s %s %d", ip_str, m_ele->intr, m_ele->sub_intr);
  if (ip_str)
    xfree(ip_str);

  return xstrdup(buf);
}

bool VirtIpAddrObj::isValid()
{
  if (m_ele->cfg_ele.error != INK_ERR_OKAY) {
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
    m_ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;

  return m_valid;
}

INKCfgEle *
VirtIpAddrObj::getCfgEleCopy()
{
  return (INKCfgEle *) copy_virt_ip_addr_ele(m_ele);
}


/*****************************************************************
 * CfgContext
 *****************************************************************/
CfgContext::CfgContext(INKFileNameT filename)
{
  m_file = filename;
  m_ver = -1;
}

CfgContext::~CfgContext()
{
  CfgEleObj *ele;
  while ((ele = m_eles.dequeue())) {
    delete ele;
  }
}

INKError CfgContext::addEle(CfgEleObj * ele)
{
  ink_assert(ele != NULL);
  m_eles.enqueue(ele);          // enqueue CfgEleObj at end of Queue 
  return INK_ERR_OKAY;
}

INKError CfgContext::removeEle(CfgEleObj * ele)
{
  ink_assert(ele != NULL);
  m_eles.remove(ele);
  delete
    ele;

  return INK_ERR_OKAY;
}

INKError CfgContext::insertEle(CfgEleObj * ele, CfgEleObj * after_ele)
{
  ink_assert(ele != NULL && after_ele != NULL);
  m_eles.insert(ele, after_ele);

  return INK_ERR_OKAY;
}

// insert Ele at front of the Queue
INKError CfgContext::pushEle(CfgEleObj * ele)
{
  ink_assert(ele != NULL);
  m_eles.push(ele);

  return INK_ERR_OKAY;
}
