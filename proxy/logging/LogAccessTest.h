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

#pragma once

#include "LogAccess.h"

/*-------------------------------------------------------------------------
  LogAccessTest

  This class is used to test the logging system standalone from the proxy
  by generating random data for the fields.
  -------------------------------------------------------------------------*/

class LogAccessTest : public LogAccess
{
public:
  LogAccessTest();
  virtual ~LogAccessTest();

  void
  init()
  {
  }

  //
  // client -> proxy fields
  //
  virtual int marshal_client_host_ip(char *);        // INT
  virtual int marshal_client_auth_user_name(char *); // STR
  // marshal_client_req_timestamp_sec is non-virtual!
  virtual int marshal_client_req_text(char *);           // STR
  virtual int marshal_client_req_http_method(char *);    // INT
  virtual int marshal_client_req_url(char *);            // STR
  virtual int marshal_client_req_http_version(char *);   // INT
  virtual int marshal_client_req_header_len(char *);     // INT
  virtual int marshal_client_req_content_len(char *);    // INT
  virtual int marshal_client_finish_status_code(char *); // INT

  //
  // proxy -> client fields
  //
  virtual int marshal_proxy_resp_content_type(char *);  // STR
  virtual int marshal_proxy_resp_reason_phrase(char *); // STR
  virtual int marshal_proxy_resp_squid_len(char *);     // INT
  virtual int marshal_proxy_resp_content_len(char *);   // INT
  virtual int marshal_proxy_resp_status_code(char *);   // INT
  virtual int marshal_proxy_resp_header_len(char *);    // INT
  virtual int marshal_proxy_finish_status_code(char *); // INT
  virtual int marshal_cache_result_code(char *);        // INT
  virtual int marshal_cache_miss_hit(char *);           // INT

  //
  // proxy -> server fields
  //
  virtual int marshal_proxy_req_header_len(char *);  // INT
  virtual int marshal_proxy_req_content_len(char *); // INT
  virtual int marshal_proxy_hierarchy_route(char *); // INT

  //
  // server -> proxy fields
  //
  virtual int marshal_server_host_ip(char *);          // INT
  virtual int marshal_server_host_name(char *);        // STR
  virtual int marshal_server_resp_status_code(char *); // INT
  virtual int marshal_server_resp_content_len(char *); // INT
  virtual int marshal_server_resp_header_len(char *);  // INT

  //
  // other fields
  //
  virtual int marshal_transfer_time_ms(char *); // INT

  //
  // named fields from within a http header
  //
  virtual int marshal_http_header_field(char *header_symbol, char *field, char *buf);

  // noncopyable
  // -- member functions that are not allowed --
  LogAccessTest(const LogAccessTest &rhs) = delete;
  LogAccessTest &operator=(LogAccessTest &rhs) = delete;
};
