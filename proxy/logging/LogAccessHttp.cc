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
 LogAccessHttp.cc

 This file defines the Http implementation of a LogAccess object, and
 implements the accessor functions using information about an Http state
 machine.

 
 ***************************************************************************/
#include "ink_unused.h"

#include "inktomi++.h"
#include "Error.h"
#include "LogAccessHttp.h"
#include "http2/HttpSM.h"
#include "MIME.h"
#include "HTTP.h"
#include "LogUtils.h"
#include "LogObject.h"
#include "LogConfig.h"
#include "Log.h"

/*------------------------------------------------------------------------- 
  LogAccessHttp

  Initialize the private data members and assert that we got a valid state
  machine pointer.
  -------------------------------------------------------------------------*/

LogAccessHttp::LogAccessHttp(HttpSM * sm)
:m_http_sm(sm), m_arena(), m_url(NULL), m_client_request(NULL), m_proxy_response(NULL), m_proxy_request(NULL), m_server_response(NULL), m_client_req_url_str(NULL), m_client_req_url_len(0), m_client_req_url_canon_str(NULL), m_client_req_url_canon_len(0), m_client_req_unmapped_url_canon_str(NULL), m_client_req_unmapped_url_canon_len(-1),      // undetermined
  m_client_req_unmapped_url_path_str(NULL), m_client_req_unmapped_url_path_len(-1),     // undetermined
  m_client_req_url_path_str(NULL),
m_client_req_url_path_len(0), m_proxy_resp_content_type_str(NULL), m_proxy_resp_content_type_len(0)
{
  ink_assert(m_http_sm != NULL);
}

/*------------------------------------------------------------------------- 
  LogAccessHttp::~LogAccessHttp

  Deallocate space for any strings allocated in the init routine.
  -------------------------------------------------------------------------*/

LogAccessHttp::~LogAccessHttp()
{
}

/*------------------------------------------------------------------------- 
  LogAccessHttp::init

  Build some strings that will come in handy for processing later, such as
  URL.  This saves us from having to build the strings twice: once to
  compute their length and a second time to actually marshal them.  We also
  initialize local pointers to each of the 4 http headers.  However, there
  is no guarantee that these headers will all be valid, so we must always
  check the validity of these pointers before using them.
  -------------------------------------------------------------------------*/

#define HIDDEN_CONTENT_TYPE "@Content-Type"
#define HIDDEN_CONTENT_TYPE_LEN 13

void
LogAccessHttp::init()
{
  HttpTransact::HeaderInfo * hdr = &(m_http_sm->t_state.hdr_info);

  if (hdr->client_request.valid()) {
    m_client_request = &(hdr->client_request);
    m_url = m_client_request->url_get();
    if (m_url) {
      m_client_req_url_str = m_url->string_get_ref(&m_client_req_url_len);
      m_client_req_url_canon_str = LogUtils::escapify_url(&m_arena,
                                                          m_client_req_url_str, m_client_req_url_len,
                                                          &m_client_req_url_canon_len);
      m_client_req_url_path_str = (char *) m_url->path_get(&m_client_req_url_path_len);
    }
  }
  if (hdr->client_response.valid()) {
    m_proxy_response = &(hdr->client_response);
    MIMEField *field = m_proxy_response->field_find(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE);
    if (field) {
      m_proxy_resp_content_type_str = (char *) field->value_get(&m_proxy_resp_content_type_len);
      //
      // here is the assert
      //
      //assert (m_proxy_resp_content_type_str[0] >= 'A' && m_proxy_resp_content_type_str[0] <= 'z');
      LogUtils::remove_content_type_attributes(m_proxy_resp_content_type_str, &m_proxy_resp_content_type_len);
    } else {
      // If Content-Type field is missing, check for @Content-Type
      MIMEField *field = m_proxy_response->field_find(HIDDEN_CONTENT_TYPE, HIDDEN_CONTENT_TYPE_LEN);
      if (field) {
        m_proxy_resp_content_type_str = (char *) field->value_get(&m_proxy_resp_content_type_len);
        LogUtils::remove_content_type_attributes(m_proxy_resp_content_type_str, &m_proxy_resp_content_type_len);
      }
    }
  }
  if (hdr->server_request.valid()) {
    m_proxy_request = &(hdr->server_request);
  }
  if (hdr->server_response.valid()) {
    m_server_response = &(hdr->server_response);
  }
}

/*------------------------------------------------------------------------- 
  The marshalling routines ...

  We know that m_http_sm is a valid pointer (we assert so in the ctor), but
  we still need to check the other header pointers before using them in the
  routines.
  -------------------------------------------------------------------------*/

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_client_host_ip(char *buf)
{
  if (buf) {
    LOG_INT ip = m_http_sm->t_state.client_info.ip;
    // the ip is already in network order, no byte order conversion
    // is needed
    marshal_int_no_byte_order_conversion(buf, ip);
  }
  return MIN_ALIGN;
}

/*------------------------------------------------------------------------- 
  user authenticated to the proxy (RFC931)
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_client_auth_user_name(char *buf)
{
  char *str = NULL;
  int len = MIN_ALIGN;

  // Jira TS-40:
  // NOTE: Authentication related code and modules were removed/disabled.
  //       Uncomment code path below when re-added/enabled.
  /*if (m_http_sm->t_state.auth_params.user_name) {
    str = m_http_sm->t_state.auth_params.user_name;
    len = LogAccess::strlen(str);
    } */
  if (buf) {
    marshal_str(buf, str, len);
  }
  return len;
}

/*-------------------------------------------------------------------------
  Private utility function to remove ftp password, if there is any
  Only return the size of the new url if buf is null.
  -------------------------------------------------------------------------*/
int
LogAccessHttp::remove_ftp_password(char *buf, const char *url_str, int url_str_len)
{
  //INKqa09824
  if (m_url && url_str_len > 6 && memcmp(url_str, "ftp://", 6) == 0) {
    const char *pass_word = NULL;
    int pass_word_len = 0;
    int len1, len2;

    pass_word = m_url->password_get(&pass_word_len);
    if (pass_word != NULL && pass_word_len > 0) {
      pass_word = (const char *) memchr((void *) (url_str + 6), ':', url_str_len - 6);
      if (pass_word != NULL) {
        int size = url_str_len - pass_word_len + 6;
        int tsize = round_strlen(size + 1);     // +1 for trailing 0

        // If buf is not null, perform the removing of the password,
        // otherwise, just return the size the new url would have been.
        if (buf != NULL) {
          char *new_url = (char *) xmalloc(tsize);

          len1 = (int) (pass_word - url_str + 1);
          len2 = (int) (url_str_len - len1 - pass_word_len);
          memcpy(new_url, url_str, len1);
          memcpy(new_url + len1, "*****", 5);
          memcpy(new_url + len1 + 5, url_str + len1 + pass_word_len, len2);
          new_url[size - 1] = '\0';
          marshal_mem(buf, new_url, size - 1, tsize);
          xfree(new_url);
        }
        return tsize;
      }
    }
  }

  return -1;
}

/*-------------------------------------------------------------------------
  Private utility function to validate m_client_req_unmapped_url_canon_str &
  m_client_req_unmapped_url_canon_len fields.
  -------------------------------------------------------------------------*/

void
LogAccessHttp::validate_unmapped_url(void)
{
  if (m_client_req_unmapped_url_canon_len < 0) {

    char *unmapped_url = m_http_sm->t_state.unmapped_request_url;

    if (unmapped_url && unmapped_url[0] != 0) {
      int unmapped_url_len =::strlen(unmapped_url);
      m_client_req_unmapped_url_canon_str =
        LogUtils::escapify_url(&m_arena, unmapped_url, unmapped_url_len, &m_client_req_unmapped_url_canon_len);
    } else {
      m_client_req_unmapped_url_canon_len = 0;
    }
  }
}

/*-------------------------------------------------------------------------
  Private utility function to validate m_client_req_unmapped_url_path_str &
  m_client_req_unmapped_url_path_len fields.
  -------------------------------------------------------------------------*/

void
LogAccessHttp::validate_unmapped_url_path(void)
{
  int len;
  char *c;

  if (m_client_req_unmapped_url_path_len < 0) {
    // Use unmapped canonical URL as default
    m_client_req_unmapped_url_path_str = m_client_req_unmapped_url_canon_str;
    m_client_req_unmapped_url_path_len = m_client_req_unmapped_url_canon_len;

    if (m_client_req_unmapped_url_path_len >= 6) {      // xxx:// - minimum schema size
      c = (char *) memchr((void *) m_client_req_unmapped_url_path_str, ':', m_client_req_unmapped_url_path_len - 1);
      if (c && (len = (int) (c - m_client_req_unmapped_url_path_str)) <= 4) {   // 4 - max schema size
        if (len + 2 <= m_client_req_unmapped_url_canon_len && c[1] == '/' && c[2] == '/') {
          len += 3;             // Skip "://"
          m_client_req_unmapped_url_path_str = &m_client_req_unmapped_url_canon_str[len];
          m_client_req_unmapped_url_path_len -= len;
          // Attempt to find first '/' in the path
          if (m_client_req_unmapped_url_path_len > 0 &&
              (c =
               (char *) memchr((void *) m_client_req_unmapped_url_path_str, '/',
                               m_client_req_unmapped_url_path_len)) != 0) {
            len = (int) (c - m_client_req_unmapped_url_path_str) + 1;
            m_client_req_unmapped_url_path_str = &m_client_req_unmapped_url_path_str[len];
            m_client_req_unmapped_url_path_len -= len;
          }
        }
      }
    }
  }
}

/*------------------------------------------------------------------------- 
  This is the method, url, and version all rolled into one.  Use the
  respective marshalling routines to do the job.
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_client_req_text(char *buf)
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
LogAccessHttp::marshal_client_req_http_method(char *buf)
{
  char *str = NULL;
  int alen = 0;
  int plen = MIN_ALIGN;

  if (m_client_request) {
    str = (char *) m_client_request->method_get(&alen);

    // calculate the the padded length only if the actual length
    // is not zero. We don't want the padded length to be zero 
    // because marshal_mem should write the DEFAULT_STR to the 
    // buffer if str is nil, and we need room for this.
    //
    if (alen) {
      plen = round_strlen(alen + 1);    // +1 for trailing 0
    }
  }

  if (buf)
    marshal_mem(buf, str, alen, plen);
  return plen;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_client_req_url(char *buf)
{
  int res;
  int len = round_strlen(m_client_req_url_len + 1);     // +1 for trailing 0
  //INKqa09824
  res = remove_ftp_password(buf, m_client_req_url_canon_str, m_client_req_url_canon_len);
  if (res > 0) {
    len = res;
  } else if (buf) {
    marshal_mem(buf, m_client_req_url_str, m_client_req_url_len, len);
  }
  return len;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_client_req_url_canon(char *buf)
{
  int res;
  int len = round_strlen(m_client_req_url_canon_len + 1);
  //INKqa09824
  res = remove_ftp_password(buf, m_client_req_url_canon_str, m_client_req_url_canon_len);
  if (res > 0) {
    len = res;
  } else if (buf) {
    marshal_mem(buf, m_client_req_url_canon_str, m_client_req_url_canon_len, len);
  }
  return len;
}


/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_client_req_unmapped_url_canon(char *buf)
{

  validate_unmapped_url();

  int res;
  int len = round_strlen(m_client_req_unmapped_url_canon_len + 1);      // +1 for eos
  //INKqa09824
  res = remove_ftp_password(buf, m_client_req_unmapped_url_canon_str, m_client_req_unmapped_url_canon_len);
  if (res > 0) {
    len = res;
  } else if (buf) {
    marshal_mem(buf, m_client_req_unmapped_url_canon_str, m_client_req_unmapped_url_canon_len, len);
  }
  return len;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_client_req_unmapped_url_path(char *buf)
{
  int len;

  validate_unmapped_url();

  validate_unmapped_url_path();

  len = round_strlen(m_client_req_unmapped_url_path_len + 1);   // +1 for eos
  if (buf) {
    marshal_mem(buf, m_client_req_unmapped_url_path_str, m_client_req_unmapped_url_path_len, len);
  }
  return len;
}

int
LogAccessHttp::marshal_client_req_url_path(char *buf)
{
  int len = round_strlen(m_client_req_url_path_len + 1);
  if (buf) {
    marshal_mem(buf, m_client_req_url_path_str, m_client_req_url_path_len, len);
  }
  return len;
}

int
LogAccessHttp::marshal_client_req_url_scheme(char *buf)
{
  char *str = NULL;
  int alen = 0;
  int plen = MIN_ALIGN;

  if (m_url) {
    str = (char *) m_url->scheme_get(&alen);

    // calculate the the padded length only if the actual length
    // is not zero. We don't want the padded length to be zero 
    // because marshal_mem should write the DEFAULT_STR to the 
    // buffer if str is nil, and we need room for this.
    //
    if (alen) {
      plen = round_strlen(alen + 1);    // +1 for trailing 0
    }
  }

  if (buf) {
    marshal_mem(buf, str, alen, plen);
  }

  return plen;
}

/*------------------------------------------------------------------------- 
  For this one we're going to marshal two INTs, one the first representing
  the major number and the second representing the minor.
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_client_req_http_version(char *buf)
{
  if (buf) {
    LOG_INT major = 0;
    LOG_INT minor = 0;
    if (m_client_request) {
      HTTPVersion versionObject = m_client_request->version_get();
      major = HTTP_MAJOR(versionObject.m_version);
      minor = HTTP_MINOR(versionObject.m_version);
    }
    marshal_int(buf, major);
    marshal_int((buf + MIN_ALIGN), minor);
  }
  return (2 * MIN_ALIGN);
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_client_req_header_len(char *buf)
{
  if (buf) {
    LOG_INT len = 0;
    if (m_client_request) {
      len = m_client_request->length_get();
    }
    marshal_int(buf, len);
  }
  return MIN_ALIGN;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_client_req_body_len(char *buf)
{
  if (buf) {
    LOG_INT len = 0;
    if (m_client_request) {
      len = m_http_sm->client_request_body_bytes;
    }
    marshal_int(buf, len);
  }
  return MIN_ALIGN;
}

int
LogAccessHttp::marshal_client_finish_status_code(char *buf)
{
  if (buf) {
    int code = LOG_FINISH_FIN;
    HttpTransact::AbortState_t cl_abort_state = m_http_sm->t_state.client_info.abort;
    if (cl_abort_state == HttpTransact::ABORTED) {

      // Check to see if the abort is due to a timeout
      if (m_http_sm->t_state.client_info.state == HttpTransact::ACTIVE_TIMEOUT ||
          m_http_sm->t_state.client_info.state == HttpTransact::INACTIVE_TIMEOUT) {
        code = LOG_FINISH_TIMEOUT;
      } else {
        code = LOG_FINISH_INTR;
      }
    }
    marshal_int(buf, code);
  }
  return MIN_ALIGN;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_proxy_resp_content_type(char *buf)
{
  int len = round_strlen(m_proxy_resp_content_type_len + 1);
  if (buf) {
    marshal_mem(buf, m_proxy_resp_content_type_str, m_proxy_resp_content_type_len, len);
  }
  return len;
}

/*------------------------------------------------------------------------- 
  Squid returns the content-length + header length as the total length.
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_proxy_resp_squid_len(char *buf)
{
  if (buf) {
    LOG_INT val = m_http_sm->client_response_hdr_bytes + m_http_sm->client_response_body_bytes;
    marshal_int(buf, val);
  }
  return MIN_ALIGN;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_proxy_resp_content_len(char *buf)
{
  if (buf) {
    LOG_INT val = m_http_sm->client_response_body_bytes;
    marshal_int(buf, val);
  }
  return MIN_ALIGN;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_proxy_resp_status_code(char *buf)
{
  if (buf) {
    HTTPStatus status;
    if (m_proxy_response && m_client_request) {
      if (m_client_request->version_get() >= HTTPVersion(1, 0)) {
        status = m_proxy_response->status_get();
      }
      // INKqa10788
      // For bad/incomplete request, the request version may be 0.9.
      // However, we can still log the status code if there is one.
      else if (m_proxy_response->valid()) {
        status = m_proxy_response->status_get();
      } else {
        status = HTTP_STATUS_OK;
      }
    } else {
      status = HTTP_STATUS_NONE;
    }
    marshal_int(buf, (LOG_INT) status);
  }
  return MIN_ALIGN;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_proxy_resp_header_len(char *buf)
{
  if (buf) {
    LOG_INT val = 0;
    if (m_proxy_response) {
      val = m_proxy_response->length_get();
    }
    marshal_int(buf, val);
  }
  return MIN_ALIGN;
}

int
LogAccessHttp::marshal_proxy_finish_status_code(char *buf)
{
  /* FIXME: Should there be no server transaction code if
     the result comes out of the cache.  Right now we default
     to FIN */
  if (buf) {
    int code = LOG_FINISH_FIN;
    if (m_http_sm->t_state.current.server) {
      switch (m_http_sm->t_state.current.server->state) {
      case HttpTransact::ACTIVE_TIMEOUT:
      case HttpTransact::INACTIVE_TIMEOUT:
        code = LOG_FINISH_TIMEOUT;
        break;
      case HttpTransact::CONNECTION_ERROR:
        code = LOG_FINISH_INTR;
        break;
      default:
        if (m_http_sm->t_state.current.server->abort == HttpTransact::ABORTED) {
          code = LOG_FINISH_INTR;
        }
        break;
      }
    }

    marshal_int(buf, code);
  }

  return MIN_ALIGN;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_cache_result_code(char *buf)
{
  if (buf) {
    SquidLogCode code = m_http_sm->t_state.squid_codes.log_code;
    marshal_int(buf, (LOG_INT) code);
  }
  return MIN_ALIGN;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_proxy_req_header_len(char *buf)
{
  if (buf) {
    LOG_INT val = 0;
    if (m_proxy_request) {
      val = m_proxy_request->length_get();
    }
    marshal_int(buf, val);
  }
  return MIN_ALIGN;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_proxy_req_body_len(char *buf)
{
  if (buf) {
    LOG_INT val = 0;
    if (m_proxy_request) {
      val = m_http_sm->server_request_body_bytes;
    }
    marshal_int(buf, val);
  }
  return MIN_ALIGN;
}

int
LogAccessHttp::marshal_proxy_req_server_name(char *buf)
{
  char *str = NULL;
  int len = MIN_ALIGN;

  if (m_http_sm->t_state.current.server) {
    str = m_http_sm->t_state.current.server->name;
    len = LogAccess::strlen(str);
  }

  if (buf) {
    marshal_str(buf, str, len);
  }
  return len;
}

int
LogAccessHttp::marshal_proxy_req_server_ip(char *buf)
{
  if (buf) {
    unsigned int the_ip = 0;
    if (m_http_sm->t_state.current.server != NULL) {
      the_ip = m_http_sm->t_state.current.server->ip;
    }
    // the ip is already in network order, no byte order conversion
    // is needed
    marshal_int_no_byte_order_conversion(buf, (LOG_INT) the_ip);
  }
  return MIN_ALIGN;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_proxy_hierarchy_route(char *buf)
{
  if (buf) {
    SquidHierarchyCode code = m_http_sm->t_state.squid_codes.hier_code;
    marshal_int(buf, (LOG_INT) code);
  }
  return MIN_ALIGN;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_server_host_ip(char *buf)
{
  if (buf) {
    LOG_INT val = 0;
    val = m_http_sm->t_state.server_info.ip;
    if (val == 0) {
      if (m_http_sm->t_state.current.server != NULL) {
        val = m_http_sm->t_state.current.server->ip;
      }
    }
    // the ip is already in network order, no byte order conversion
    // is needed
    marshal_int_no_byte_order_conversion(buf, val);
  }
  return MIN_ALIGN;
}


/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_server_host_name(char *buf)
{
  char *str = NULL;
  int padded_len = MIN_ALIGN;
  int actual_len = 0;

  if (m_client_request) {
    MIMEField *field = m_client_request->field_find(MIME_FIELD_HOST,
                                                    MIME_LEN_HOST);

    if (field) {
      str = (char *) field->value_get(&actual_len);
      padded_len = round_strlen(actual_len + 1);        // +1 for trailing 0
    } else if (m_url) {
      str = (char *) m_url->host_get(&actual_len);
      padded_len = round_strlen(actual_len + 1);        // +1 for trailing 0
    } else {
      str = NULL;
      actual_len = 0;
      padded_len = MIN_ALIGN;
    }
  }
  if (buf) {
    marshal_mem(buf, str, actual_len, padded_len);
  }
  return padded_len;
}


/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_client_accelerator_id(char *buf)
{
  char *str = NULL;
  int padded_len = MIN_ALIGN;
  int actual_len = 0;

  if (Log::config->xuid_logging_enabled) {

    if (m_client_request) {
      MIMEField *field = m_client_request->field_find(MIME_FIELD_X_ID,
                                                      MIME_LEN_X_ID);

      if (field) {
        str = (char *) field->value_get(&actual_len);
        padded_len = round_strlen(actual_len + 1);      // +1 for trailing 0
      } else {
        str = NULL;
        actual_len = 0;
        padded_len = MIN_ALIGN;
      }
    }
  } else {
    str = NULL;
    actual_len = 0;
    padded_len = MIN_ALIGN;
  }

  if (buf) {
    marshal_mem(buf, str, actual_len, padded_len);
  }
  return padded_len;
}


/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_server_resp_status_code(char *buf)
{
  if (buf) {
    HTTPStatus status;
    if (m_server_response) {
      status = m_server_response->status_get();
    } else {
      status = HTTP_STATUS_NONE;
    }
    marshal_int(buf, (LOG_INT) status);
  }
  return MIN_ALIGN;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_server_resp_content_len(char *buf)
{
  if (buf) {
    LOG_INT val = 0;
    if (m_server_response) {
      val = m_http_sm->server_response_body_bytes;
    }
    marshal_int(buf, val);
  }
  return MIN_ALIGN;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_server_resp_header_len(char *buf)
{
  if (buf) {
    LOG_INT val = 0;
    if (m_server_response) {
      val = m_server_response->length_get();
    }
    marshal_int(buf, val);
  }
  return MIN_ALIGN;
}

int
LogAccessHttp::marshal_server_resp_http_version(char *buf)
{
  if (buf) {
    LOG_INT major = 0;
    LOG_INT minor = 0;
    if (m_server_response) {
      major = HTTP_MAJOR(m_server_response->version_get().m_version);
      minor = HTTP_MINOR(m_server_response->version_get().m_version);
    }
    marshal_int(buf, major);
    marshal_int((buf + MIN_ALIGN), minor);
  }
  return (2 * MIN_ALIGN);
}

int
LogAccessHttp::marshal_client_retry_after_time(char *buf)
{
  if (buf) {
    LOG_INT crat = m_http_sm->t_state.congestion_control_crat;
    marshal_int(buf, crat);
  }
  return MIN_ALIGN;
}

static LogCacheWriteCodeType
convert_cache_write_code(HttpTransact::CacheWriteStatus_t t)
{

  LogCacheWriteCodeType code;
  switch (t) {
  case HttpTransact::NO_CACHE_WRITE:
    code = LOG_CACHE_WRITE_NONE;
    break;
  case HttpTransact::CACHE_WRITE_LOCK_MISS:
    code = LOG_CACHE_WRITE_LOCK_MISSED;
    break;
  case HttpTransact::CACHE_WRITE_IN_PROGRESS:
    // Hack - the HttpSM doesn't record
    //   cache write aborts currently so
    //   if it's not complete declare it
    //   aborted
    code = LOG_CACHE_WRITE_LOCK_ABORTED;
    break;
  case HttpTransact::CACHE_WRITE_ERROR:
    code = LOG_CACHE_WRITE_ERROR;
    break;
  case HttpTransact::CACHE_WRITE_COMPLETE:
    code = LOG_CACHE_WRITE_COMPLETE;
    break;
  default:
    ink_assert(!"bad cache write code");
    code = LOG_CACHE_WRITE_NONE;
    break;
  }

  return code;
}


int
LogAccessHttp::marshal_cache_write_code(char *buf)
{

  if (buf) {
    int code = convert_cache_write_code(m_http_sm->t_state.cache_info.write_status);

    marshal_int(buf, code);
  }

  return MIN_ALIGN;
}

int
LogAccessHttp::marshal_cache_write_transform_code(char *buf)
{

  if (buf) {
    int code = convert_cache_write_code(m_http_sm->t_state.cache_info.transform_write_status);

    marshal_int(buf, code);
  }

  return MIN_ALIGN;
}


/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_transfer_time_ms(char *buf)
{
  if (buf) {
    ink_hrtime elapsed = m_http_sm->milestones.sm_finish - m_http_sm->milestones.sm_start;
    elapsed /= HRTIME_MSECOND;
    LOG_INT val = (LOG_INT) elapsed;
    marshal_int(buf, val);
  }
  return MIN_ALIGN;
}

int
LogAccessHttp::marshal_transfer_time_s(char *buf)
{
  if (buf) {
    ink_hrtime elapsed = m_http_sm->milestones.sm_finish - m_http_sm->milestones.sm_start;
    elapsed /= HRTIME_SECOND;
    LOG_INT val = (LOG_INT) elapsed;
    marshal_int(buf, val);
  }
  return MIN_ALIGN;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccessHttp::marshal_http_header_field(LogField::Container container, char *field, char *buf)
{
  char *str = NULL;
  int padded_len = MIN_ALIGN;
  int actual_len = 0;
  bool valid_field = false;
  HTTPHdr *header;

  switch (container) {
  case LogField::CQH:
    header = m_client_request;
    break;

  case LogField::PSH:
    header = m_proxy_response;
    break;

  case LogField::PQH:
    header = m_proxy_request;
    break;

  case LogField::SSH:
    header = m_server_response;
    break;

  default:
    header = NULL;
    break;
  }

  if (header) {
    MIMEField *fld = header->field_find(field, (int)::strlen(field));
    if (fld) {
      valid_field = true;

      // Loop over dups, marshalling each one into the buffer and
      // summing up their length
      //
      int running_len = 0;
      while (fld) {
        str = (char *) fld->value_get(&actual_len);
        if (buf) {
          memcpy(buf, str, actual_len);
          buf += actual_len;
        }
        running_len += actual_len;
        fld = fld->m_next_dup;

        // Dups need to be comma separated.  So if there's another
        // dup, then add a comma and a space ...
        //
        if (fld != NULL) {
          if (buf) {
            memcpy(buf, ", ", 2);
            buf += 2;
          }
          running_len += 2;
        }
      }

      // Done with all dups.  Ensure that the string is terminated
      // and that the running_len is padded.
      //
      if (buf) {
        *buf = '\0';
        buf++;
      }
      running_len += 1;
      padded_len = round_strlen(running_len);

      // Note: marshal_string fills the padding to
      //  prevent purify UMRs so we do it here too
      //  since we always pass the unpadded length on
      //  our calls to marshal string
#ifdef DEBUG
      if (buf) {
        int pad_len = padded_len - running_len;
        for (int i = 0; i < pad_len; i++) {
          *buf = '$';
          buf++;
        }
      }
#endif
    }
  }

  if (valid_field == false) {
    padded_len = MIN_ALIGN;
    if (buf) {
      marshal_str(buf, NULL, padded_len);
    }
  }

  return (padded_len);
}

int
LogAccessHttp::marshal_http_header_field_escapify(LogField::Container container, char *field, char *buf)
{
  char *str = NULL, *new_str = NULL;
  int padded_len = MIN_ALIGN;
  int actual_len = 0, new_len = 0;
  bool valid_field = false;
  HTTPHdr *header;

  switch (container) {
  case LogField::ECQH:
    header = m_client_request;
    break;

  case LogField::EPSH:
    header = m_proxy_response;
    break;

  case LogField::EPQH:
    header = m_proxy_request;
    break;

  case LogField::ESSH:
    header = m_server_response;
    break;

  default:
    header = NULL;
    break;
  }

  if (header) {
    MIMEField *fld = header->field_find(field, (int)::strlen(field));
    if (fld) {
      valid_field = true;

      // Loop over dups, marshalling each one into the buffer and
      // summing up their length
      //
      int running_len = 0;
      while (fld) {
        str = (char *) fld->value_get(&actual_len);
        new_str = LogUtils::escapify_url(&m_arena, str, actual_len, &new_len);
        if (buf) {
          memcpy(buf, new_str, new_len);
          buf += new_len;
        }
        running_len += new_len;
        fld = fld->m_next_dup;

        // Dups need to be comma separated.  So if there's another
        // dup, then add a comma and a space ...
        //
        if (fld != NULL) {
          if (buf) {
            memcpy(buf, ", ", 2);
            buf += 2;
          }
          running_len += 2;
        }
      }

      // Done with all dups.  Ensure that the string is terminated
      // and that the running_len is padded.
      //
      if (buf) {
        *buf = '\0';
        buf++;
      }
      running_len += 1;
      padded_len = round_strlen(running_len);

      // Note: marshal_string fills the padding to
      //  prevent purify UMRs so we do it here too
      //  since we always pass the unpadded length on
      //  our calls to marshal string
#ifdef DEBUG
      if (buf) {
        int pad_len = padded_len - running_len;
        for (int i = 0; i < pad_len; i++) {
          *buf = '$';
          buf++;
        }
      }
#endif
    }
  }

  if (valid_field == false) {
    padded_len = MIN_ALIGN;
    if (buf) {
      marshal_str(buf, NULL, padded_len);
    }
  }

  return (padded_len);
}
