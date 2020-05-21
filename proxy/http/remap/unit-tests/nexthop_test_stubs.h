/** @file

  unit test stubs header for linking nexthop unit tests.

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

  @section details Details

  Implements code necessary for Reverse Proxy which mostly consists of
  general purpose hostname substitution in URLs.

 */

#pragma once

#include <map>
#include <string>
#include <iostream>

#include <stdio.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define NH_Debug(tag, fmt, ...) PrintToStdErr("%s %s:%d:%s() " fmt "\n", tag, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#define NH_Error(fmt, ...) PrintToStdErr("%s:%d:%s() " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#define NH_Note(fmt, ...) PrintToStdErr("%s:%d:%s() " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#define NH_Warn(fmt, ...) PrintToStdErr("%s:%d:%s() " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__)

class HttpRequestData;

void PrintToStdErr(const char *fmt, ...);
void br_destroy(HttpRequestData &h);
void build_request(HttpRequestData &h, const char *os_hostname);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#include "ControlMatcher.h"
struct TestData : public HttpRequestData {
  std::string hostname;
  sockaddr client_ip;
  sockaddr server_ip;

  TestData()
  {
    client_ip.sa_family = AF_INET;
    memset(client_ip.sa_data, 0, sizeof(client_ip.sa_data));
  }
  const char *
  get_host()
  {
    return hostname.c_str();
  }
  sockaddr const *
  get_ip()
  {
    return &server_ip;
  }
  sockaddr const *
  get_client_ip()
  {
    return &client_ip;
  }
  char *
  get_string()
  {
    return nullptr;
  }
};
