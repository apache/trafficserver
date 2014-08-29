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



#ifndef LOG_ACCESS_HTTP_H
#define LOG_ACCESS_HTTP_H

#include "Arena.h"
#include "HTTP.h"
#include "LogAccess.h"

class HttpSM;
class URL;

/*-------------------------------------------------------------------------
  LogAccessHttp

  This class extends the logging system interface as implemented by the
  HttpStateMachineGet class.
  -------------------------------------------------------------------------*/

class LogAccessHttp:public LogAccess
{
public:
  LogAccessHttp(HttpSM * sm);
  virtual ~ LogAccessHttp();

  void init();
  LogEntryType entry_type()
  {
    return LOG_ENTRY_HTTP;
  }

  //
  // client -> proxy fields
  //
  virtual int marshal_client_host_ip(char *);   // STR
  virtual int marshal_client_host_port(char *); // INT
  virtual int marshal_client_auth_user_name(char *);    // STR
  virtual int marshal_client_req_text(char *);  // STR
  virtual int marshal_client_req_http_method(char *);   // INT
  virtual int marshal_client_req_url(char *);   // STR
  virtual int marshal_client_req_url_canon(char *);     // STR
  virtual int marshal_client_req_unmapped_url_canon(char *);    // STR
  virtual int marshal_client_req_unmapped_url_path(char *);     // STR
  virtual int marshal_client_req_unmapped_url_host(char *);     // STR
  virtual int marshal_client_req_url_path(char *);      // STR
  virtual int marshal_client_req_url_scheme(char *);    // STR
  virtual int marshal_client_req_http_version(char *);  // INT
  virtual int marshal_client_req_header_len(char *);    // INT
  virtual int marshal_client_req_body_len(char *);      // INT
  virtual int marshal_client_finish_status_code(char *);        // INT

  //
  // proxy -> client fields
  //
  virtual int marshal_proxy_resp_content_type(char *);  // STR
  virtual int marshal_proxy_resp_squid_len(char *);     // INT
  virtual int marshal_proxy_resp_content_len(char *);   // INT
  virtual int marshal_proxy_resp_status_code(char *);   // INT
  virtual int marshal_proxy_resp_header_len(char *);    // INT
  virtual int marshal_proxy_finish_status_code(char *); // INT
  virtual int marshal_cache_result_code(char *);        // INT

  //
  // proxy -> server fields
  //
  virtual int marshal_proxy_req_header_len(char *);     // INT
  virtual int marshal_proxy_req_body_len(char *);       // INT
  virtual int marshal_proxy_req_server_name(char *);    // STR
  virtual int marshal_proxy_req_server_ip(char *);      // INT
  virtual int marshal_proxy_hierarchy_route(char *);    // INT

  //
  // server -> proxy fields
  //
  virtual int marshal_server_host_ip(char *);   // INT
  virtual int marshal_server_host_name(char *); // STR
  virtual int marshal_server_resp_status_code(char *);  // INT
  virtual int marshal_server_resp_content_len(char *);  // INT
  virtual int marshal_server_resp_header_len(char *);   // INT
  virtual int marshal_server_resp_http_version(char *); // INT

  //
  // cache -> client fields
  //
  virtual int marshal_cache_resp_status_code(char *);  // INT
  virtual int marshal_cache_resp_content_len(char *);  // INT
  virtual int marshal_cache_resp_header_len(char *);   // INT
  virtual int marshal_cache_resp_http_version(char *); // INT

  //
  // congestion control client_retry_after_time
  //
  virtual int marshal_client_retry_after_time(char *);  // INT

  //
  // cache write fields
  //
  virtual int marshal_cache_write_code(char *); // INT
  virtual int marshal_cache_write_transform_code(char *);       // INT

  //
  // other fields
  //
  virtual int marshal_transfer_time_ms(char *); // INT
  virtual int marshal_transfer_time_s(char *);  // INT
  virtual int marshal_file_size(char *); // INT
  virtual int marshal_plugin_identity_id(char *);    // INT
  virtual int marshal_plugin_identity_tag(char *);    // STR

  //
  // named fields from within a http header
  //
  virtual int marshal_http_header_field(LogField::Container container, char *field, char *buf);
  virtual int marshal_http_header_field_escapify(LogField::Container container, char *field, char *buf);

  virtual void set_client_req_url(char *, int);        // STR
  virtual void set_client_req_url_canon(char *, int);  // STR
  virtual void set_client_req_unmapped_url_canon(char *, int); // STR
  virtual void set_client_req_unmapped_url_path(char *, int);  // STR
  virtual void set_client_req_unmapped_url_host(char *, int);  // STR
  virtual void set_client_req_url_path(char *, int);   // STR

private:
  HttpSM * m_http_sm;

  Arena m_arena;
  //  URL *m_url;

  HTTPHdr *m_client_request;
  HTTPHdr *m_proxy_response;
  HTTPHdr *m_proxy_request;
  HTTPHdr *m_server_response;
  HTTPHdr *m_cache_response;

  char *m_client_req_url_str;
  int m_client_req_url_len;
  char *m_client_req_url_canon_str;
  int m_client_req_url_canon_len;
  char *m_client_req_unmapped_url_canon_str;
  int m_client_req_unmapped_url_canon_len;
  char *m_client_req_unmapped_url_path_str;
  int m_client_req_unmapped_url_path_len;
  char *m_client_req_unmapped_url_host_str;
  int m_client_req_unmapped_url_host_len;
  char const*m_client_req_url_path_str;
  int m_client_req_url_path_len;
  char *m_proxy_resp_content_type_str;
  int m_proxy_resp_content_type_len;

  void validate_unmapped_url(void);
  void validate_unmapped_url_path(void);

  // -- member functions that are not allowed --
  LogAccessHttp(const LogAccessHttp & rhs);
  LogAccessHttp & operator=(LogAccessHttp & rhs);
};

#endif
