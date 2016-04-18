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

#ifndef LOG_ACCESS_ICP_H
#define LOG_ACCESS_ICP_H

#include "LogAccess.h"

class ICPlog;

/*-------------------------------------------------------------------------
  LogAccessICP

  This class extends the logging system interface as implemented by the
  ICPlog class.
  -------------------------------------------------------------------------*/

class LogAccessICP : public LogAccess
{
public:
  LogAccessICP(ICPlog *icp_log);
  virtual ~LogAccessICP();

  LogEntryType
  entry_type()
  {
    return LOG_ENTRY_ICP;
  }

  //
  // client -> proxy fields
  //
  virtual int marshal_client_host_ip(char *);         // STR
  virtual int marshal_client_host_port(char *);       // INT
  virtual int marshal_client_auth_user_name(char *);  // STR
  virtual int marshal_client_req_text(char *);        // STR
  virtual int marshal_client_req_http_method(char *); // INT
  virtual int marshal_client_req_url(char *);         // STR
  virtual int marshal_client_req_url_canon(char *);   // STR

  //
  // proxy -> client fields
  //
  virtual int marshal_proxy_resp_content_type(char *); // STR
  virtual int marshal_proxy_resp_squid_len(char *);    // INT
  virtual int marshal_proxy_resp_content_len(char *);  // INT
  virtual int marshal_proxy_resp_status_code(char *);  // INT
  virtual int marshal_cache_result_code(char *);       // INT

  //
  // proxy -> server fields
  //
  virtual int marshal_proxy_hierarchy_route(char *); // INT

  //
  // server -> proxy fields
  //
  virtual int marshal_server_host_name(char *); // STR

  //
  // other fields
  //
  virtual int marshal_transfer_time_ms(char *); // INT
  virtual int marshal_transfer_time_s(char *);  // INT

private:
  ICPlog *m_icp_log;

  // -- member functions that are not allowed --
  LogAccessICP(const LogAccessICP &rhs);
  LogAccessICP &operator=(LogAccessICP &rhs);
};

#endif
