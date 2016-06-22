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

/***************************************************************************
 LogAccessICP.cc

 This file implements the LogAccessICP class, which specializes the
 LogAccess class for ICP logging.  Some of the field requests are not
 relevant to ICP logging, and for those we simply return a default value
 (NULL strings, 0 values).


 ***************************************************************************/
#include "ts/ink_platform.h"
#include "Error.h"
#include "HTTP.h"
#include "ICP.h"
#include "ICPlog.h"
#include "LogAccessICP.h"
#include "LogUtils.h"

/*-------------------------------------------------------------------------
  LogAccessICP
  -------------------------------------------------------------------------*/

LogAccessICP::LogAccessICP(ICPlog *icp_log) : m_icp_log(icp_log)
{
  ink_assert(m_icp_log != NULL);
}

/*-------------------------------------------------------------------------
  LogAccessICP::~LogAccessICP
  -------------------------------------------------------------------------*/

LogAccessICP::~LogAccessICP()
{
}

/*-------------------------------------------------------------------------
  The marshalling routines ...
  -------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessICP::marshal_client_host_ip(char *buf)
{
  return marshal_ip(buf, m_icp_log->GetClientIP());
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessICP::marshal_client_host_port(char *buf)
{
  if (buf) {
    uint16_t port = ntohs(m_icp_log->GetClientPort());
    marshal_int(buf, port);
  }
  return INK_MIN_ALIGN;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessICP::marshal_client_auth_user_name(char *buf)
{
  char *str = (char *)m_icp_log->GetIdent();
  int len   = LogAccess::strlen(str);
  if (buf) {
    marshal_str(buf, str, len);
  }
  return len;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessICP::marshal_client_req_text(char *buf)
{
  int len = marshal_client_req_http_method(NULL) + marshal_client_req_url(NULL) + marshal_client_req_http_version(NULL);

  if (buf) {
    int offset = 0;
    offset += marshal_client_req_http_method(&buf[offset]);
    offset += marshal_client_req_url(&buf[offset]);
    offset += marshal_client_req_http_version(&buf[offset]);
    len = offset;
  }
  return len;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessICP::marshal_client_req_http_method(char *buf)
{
  char *str = (char *)m_icp_log->GetMethod();
  int len   = LogAccess::strlen(str);

  if (buf) {
    marshal_str(buf, str, len);
  }
  return len;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessICP::marshal_client_req_url(char *buf)
{
  char *str = (char *)m_icp_log->GetURI();
  int len   = LogAccess::strlen(str);
  if (buf) {
    marshal_str(buf, str, len);
  }
  return len;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessICP::marshal_client_req_url_canon(char *buf)
{
  int escapified_len;
  Arena arena;

  // FIXME: need to ensure that m_icp_log->GetURI() is NUL-terminated
  //
  char *uri_str = (char *)m_icp_log->GetURI();
  int uri_len   = ::strlen(uri_str);
  char *str     = LogUtils::escapify_url(&arena, uri_str, uri_len, &escapified_len);

  int len = round_strlen(escapified_len + 1); // the padded len
  if (buf)
    marshal_str(buf, str, len);
  return len;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessICP::marshal_proxy_resp_content_type(char *buf)
{
  // FIXME: need to ensure that m_icp_log->GetContentType() is NUL-terminated
  //
  char *ct_str = (char *)m_icp_log->GetContentType();
  int ct_len   = ::strlen(ct_str);

  // FIXME: need to be sure remove_content_type_attributecan mutate ct_str
  //
  LogUtils::remove_content_type_attributes(ct_str, &ct_len);
  int len = LogAccess::strlen(ct_str);
  if (buf)
    marshal_str(buf, ct_str, len);
  return len;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessICP::marshal_proxy_resp_squid_len(char *buf)
{
  if (buf) {
    int64_t val = m_icp_log->GetSize();
    marshal_int(buf, val);
  }
  return INK_MIN_ALIGN;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessICP::marshal_proxy_resp_content_len(char *buf)
{
  if (buf) {
    int64_t val = m_icp_log->GetSize();
    marshal_int(buf, val);
  }
  return INK_MIN_ALIGN;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessICP::marshal_proxy_resp_status_code(char *buf)
{
  if (buf) {
    int64_t status = 0; // '000' for ICP
    marshal_int(buf, status);
  }
  return INK_MIN_ALIGN;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessICP::marshal_cache_result_code(char *buf)
{
  if (buf) {
    SquidLogCode code = m_icp_log->GetAction();
    marshal_int(buf, (int64_t)code);
  }
  return INK_MIN_ALIGN;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessICP::marshal_proxy_hierarchy_route(char *buf)
{
  if (buf) {
    SquidHierarchyCode code = m_icp_log->GetHierarchy();
    marshal_int(buf, (int64_t)code);
  }
  return INK_MIN_ALIGN;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessICP::marshal_server_host_name(char *buf)
{
  char *str = (char *)m_icp_log->GetFromHost();
  int len   = LogAccess::strlen(str);
  if (buf) {
    marshal_str(buf, str, len);
  }
  return len;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessICP::marshal_transfer_time_ms(char *buf)
{
  if (buf) {
    ink_hrtime elapsed = m_icp_log->GetElapsedTime();
    elapsed /= HRTIME_MSECOND;
    int64_t val = (int64_t)elapsed;
    marshal_int(buf, val);
  }
  return INK_MIN_ALIGN;
}

int
LogAccessICP::marshal_transfer_time_s(char *buf)
{
  if (buf) {
    ink_hrtime elapsed = m_icp_log->GetElapsedTime();
    elapsed /= HRTIME_SECOND;
    int64_t val = (int64_t)elapsed;
    marshal_int(buf, val);
  }
  return INK_MIN_ALIGN;
}
