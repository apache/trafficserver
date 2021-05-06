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
#define NH_GetConfig(v, n) GetConfigInteger(&v, n)

class HttpRequestData;

void GetConfigInteger(int *v, const char *n);
void PrintToStdErr(const char *fmt, ...);
void build_request(HttpRequestData &h, const char *os_hostname);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#include "ControlMatcher.h"
#include "ParentSelection.h"

struct trans_config {
  int64_t parent_retry_time     = 0;
  int64_t parent_fail_threshold = 0;
  trans_config() {}
};

struct trans_state {
  ParentResult parent_result;
  HttpRequestData request_data;
  trans_config txn_conf;
  trans_state() {}
};

class HttpSM;

void br_destroy(HttpSM &sm);

void build_request(int64_t sm_id, HttpSM *sm, sockaddr_in *ip, const char *os_hostname, sockaddr const *dest_ip);
