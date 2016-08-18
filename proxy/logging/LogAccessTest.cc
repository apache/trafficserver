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
 LogAccessTest.cc


 ***************************************************************************/

/*-------------------------------------------------------------------------
  Ok, the name of the game here is to generate 'random' data, with strings
  of varying length, so that we can use these accessor objects to test the
  hell out the logging system without relying on the rest of the proxy to
  provide the data.

  We'll use the random(3) number generator, which returns pseudo-random
  numbers in the range from 0 to (2^31)-1.  The period is 16^((2^31)-1).
  srandom(3) is used to seed the random number generator.
  -------------------------------------------------------------------------*/

#include <assert.h>
#include <stdio.h>
#include "LogAccessTest.h"

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LogAccessTest::LogAccessTest()
{
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LogAccessTest::~LogAccessTest()
{
}

/*-------------------------------------------------------------------------
  The marshalling routines ...
  -------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_client_host_ip(char *buf)
{
  IpEndpoint lo;
  ats_ip4_set(&lo, INADDR_LOOPBACK);
  return marshal_ip(buf, &lo.sa);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_client_auth_user_name(char *buf)
{
  static char const *str = "major tom";
  int len                = LogAccess::strlen(str);
  if (buf) {
    marshal_str(buf, str, len);
  }
  return len;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_client_req_text(char *buf)
{
  static char const *str = "GET http://www.foobar.com/ HTTP/1.0";
  int len                = LogAccess::strlen(str);
  if (buf) {
    marshal_str(buf, str, len);
  }
  return len;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_client_req_http_method(char *buf)
{
  if (buf) {
    int64_t val = 1;
    marshal_int(buf, val);
  }
  return sizeof(int64_t);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_client_req_url(char *buf)
{
  static char const *str = "http://www.foobar.com/";
  int len                = LogAccess::strlen(str);
  if (buf) {
    marshal_str(buf, str, len);
  }
  return len;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_client_req_http_version(char *buf)
{
  if (buf) {
    int64_t val = 2;
    marshal_int(buf, val);
  }
  return sizeof(int64_t);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_client_req_header_len(char *buf)
{
  if (buf) {
    int64_t val = 3;
    marshal_int(buf, val);
  }
  return sizeof(int64_t);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_client_req_body_len(char *buf)
{
  if (buf) {
    int64_t val = 4;
    marshal_int(buf, val);
  }
  return sizeof(int64_t);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_client_finish_status_code(char *buf)
{
  if (buf) {
    int64_t val = 5;
    marshal_int(buf, val);
  }
  return sizeof(int64_t);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_proxy_resp_content_type(char *buf)
{
  static char const *str = "text/html";
  int len                = LogAccess::strlen(str);
  if (buf) {
    marshal_str(buf, str, len);
  }
  return len;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_proxy_resp_squid_len(char *buf)
{
  if (buf) {
    int64_t val = 100;
    marshal_int(buf, val);
  }
  return sizeof(int64_t);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_proxy_resp_content_len(char *buf)
{
  if (buf) {
    int64_t val = 6;
    marshal_int(buf, val);
  }
  return sizeof(int64_t);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_proxy_resp_status_code(char *buf)
{
  if (buf) {
    int64_t val = 7;
    marshal_int(buf, val);
  }
  return sizeof(int64_t);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_proxy_resp_header_len(char *buf)
{
  if (buf) {
    int64_t val = 8;
    marshal_int(buf, val);
  }
  return sizeof(int64_t);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_proxy_finish_status_code(char *buf)
{
  if (buf) {
    int64_t val = 9;
    marshal_int(buf, val);
  }
  return sizeof(int64_t);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_cache_result_code(char *buf)
{
  if (buf) {
    int64_t val = 10;
    marshal_int(buf, val);
  }
  return sizeof(int64_t);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_cache_miss_hit(char *buf)
{
  if (buf) {
    int64_t val = 10;
    marshal_int(buf, val);
  }
  return sizeof(int64_t);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_proxy_req_header_len(char *buf)
{
  if (buf) {
    int64_t val = 11;
    marshal_int(buf, val);
  }
  return sizeof(int64_t);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_proxy_req_body_len(char *buf)
{
  if (buf) {
    int64_t val = 12;
    marshal_int(buf, val);
  }
  return sizeof(int64_t);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_proxy_hierarchy_route(char *buf)
{
  if (buf) {
    int64_t val = 13;
    marshal_int(buf, val);
  }
  return sizeof(int64_t);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_server_host_ip(char *buf)
{
  IpEndpoint lo;
  ats_ip4_set(&lo, INADDR_LOOPBACK);
  return marshal_ip(buf, &lo.sa);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_server_host_name(char *buf)
{
  static char const *str = "www.foobar.com";
  int len                = LogAccess::strlen(str);
  if (buf) {
    marshal_str(buf, str, len);
  }
  return len;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_server_resp_status_code(char *buf)
{
  if (buf) {
    int64_t val = 15;
    marshal_int(buf, val);
  }
  return sizeof(int64_t);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_server_resp_content_len(char *buf)
{
  if (buf) {
    int64_t val = 16;
    marshal_int(buf, val);
  }
  return sizeof(int64_t);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_server_resp_header_len(char *buf)
{
  if (buf) {
    int64_t val = 17;
    marshal_int(buf, val);
  }
  return sizeof(int64_t);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_transfer_time_ms(char *buf)
{
  if (buf) {
    int64_t val = 18;
    marshal_int(buf, val);
  }
  return sizeof(int64_t);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccessTest::marshal_http_header_field(char *header_symbol, char *field, char *buf)
{
  return 0; // STUB
}
