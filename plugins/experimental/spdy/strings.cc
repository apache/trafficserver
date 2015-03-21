/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ts/ts.h>
#include <spdy/spdy.h>
#include <base/logging.h>

template <>
std::string
stringof<TSEvent>(const TSEvent &ev)
{
  static const detail::named_value<unsigned> event_names[] = {
    {"TS_EVENT_NONE", 0},
    {"TS_EVENT_IMMEDIATE", 1},
    {"TS_EVENT_TIMEOUT", 2},
    {"TS_EVENT_ERROR", 3},
    {"TS_EVENT_CONTINUE", 4},
    {"TS_EVENT_VCONN_READ_READY", 100},
    {"TS_EVENT_VCONN_WRITE_READY", 101},
    {"TS_EVENT_VCONN_READ_COMPLETE", 102},
    {"TS_EVENT_VCONN_WRITE_COMPLETE", 103},
    {"TS_EVENT_VCONN_EOS", 104},
    {"TS_EVENT_VCONN_INACTIVITY_TIMEOUT", 105},
    {"TS_EVENT_NET_CONNECT", 200},
    {"TS_EVENT_NET_CONNECT_FAILED", 201},
    {"TS_EVENT_NET_ACCEPT", 202},
    {"TS_EVENT_NET_ACCEPT_FAILED", 204},
    {"TS_EVENT_INTERNAL_206", 206},
    {"TS_EVENT_INTERNAL_207", 207},
    {"TS_EVENT_INTERNAL_208", 208},
    {"TS_EVENT_INTERNAL_209", 209},
    {"TS_EVENT_INTERNAL_210", 210},
    {"TS_EVENT_INTERNAL_211", 211},
    {"TS_EVENT_INTERNAL_212", 212},
    {"TS_EVENT_HOST_LOOKUP", 500},
    {"TS_EVENT_CACHE_OPEN_READ", 1102},
    {"TS_EVENT_CACHE_OPEN_READ_FAILED", 1103},
    {"TS_EVENT_CACHE_OPEN_WRITE", 1108},
    {"TS_EVENT_CACHE_OPEN_WRITE_FAILED", 1109},
    {"TS_EVENT_CACHE_REMOVE", 1112},
    {"TS_EVENT_CACHE_REMOVE_FAILED", 1113},
    {"TS_EVENT_CACHE_SCAN", 1120},
    {"TS_EVENT_CACHE_SCAN_FAILED", 1121},
    {"TS_EVENT_CACHE_SCAN_OBJECT", 1122},
    {"TS_EVENT_CACHE_SCAN_OPERATION_BLOCKED", 1123},
    {"TS_EVENT_CACHE_SCAN_OPERATION_FAILED", 1124},
    {"TS_EVENT_CACHE_SCAN_DONE", 1125},
    {"TS_EVENT_CACHE_LOOKUP", 1126},
    {"TS_EVENT_CACHE_READ", 1127},
    {"TS_EVENT_CACHE_DELETE", 1128},
    {"TS_EVENT_CACHE_WRITE", 1129},
    {"TS_EVENT_CACHE_WRITE_HEADER", 1130},
    {"TS_EVENT_CACHE_CLOSE", 1131},
    {"TS_EVENT_CACHE_LOOKUP_READY", 1132},
    {"TS_EVENT_CACHE_LOOKUP_COMPLETE", 1133},
    {"TS_EVENT_CACHE_READ_READY", 1134},
    {"TS_EVENT_CACHE_READ_COMPLETE", 1135},
    {"TS_EVENT_INTERNAL_1200", 1200},
    {"TS_AIO_EVENT_DONE", 3900},
    {"TS_EVENT_HTTP_CONTINUE", 60000},
    {"TS_EVENT_HTTP_ERROR", 60001},
    {"TS_EVENT_HTTP_READ_REQUEST_HDR", 60002},
    {"TS_EVENT_HTTP_OS_DNS", 60003},
    {"TS_EVENT_HTTP_SEND_REQUEST_HDR", 60004},
    {"TS_EVENT_HTTP_READ_CACHE_HDR", 60005},
    {"TS_EVENT_HTTP_READ_RESPONSE_HDR", 60006},
    {"TS_EVENT_HTTP_SEND_RESPONSE_HDR", 60007},
    {"TS_EVENT_HTTP_REQUEST_TRANSFORM", 60008},
    {"TS_EVENT_HTTP_RESPONSE_TRANSFORM", 60009},
    {"TS_EVENT_HTTP_SELECT_ALT", 60010},
    {"TS_EVENT_HTTP_TXN_START", 60011},
    {"TS_EVENT_HTTP_TXN_CLOSE", 60012},
    {"TS_EVENT_HTTP_SSN_START", 60013},
    {"TS_EVENT_HTTP_SSN_CLOSE", 60014},
    {"TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE", 60015},
    {"TS_EVENT_HTTP_PRE_REMAP", 60016},
    {"TS_EVENT_HTTP_POST_REMAP", 60017},
    {"TS_EVENT_MGMT_UPDATE", 60100},
    {"TS_EVENT_INTERNAL_60200", 60200},
    {"TS_EVENT_INTERNAL_60201", 60201},
    {"TS_EVENT_INTERNAL_60202", 60202},
  };

  return detail::match(event_names, (unsigned)ev);
}
