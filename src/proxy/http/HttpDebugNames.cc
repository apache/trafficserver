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

#include "iocore/dns/DNSProcessor.h"
#include "proxy/http/HttpDebugNames.h"
#include "../../iocore/eventsystem/P_EventSystem.h"
#include "proxy/http/HttpTunnel.h"
#include "proxy/Transform.h"
#include "proxy/http/HttpSM.h"
#include <ts/apidefs.h>
#include "iocore/eventsystem/Event.h"

//----------------------------------------------------------------------------
const char *
HttpDebugNames::get_server_state_name(HttpTransact::ServerState_t state)
{
  switch (state) {
  case HttpTransact::STATE_UNDEFINED:
    return "STATE_UNDEFINED";
  case HttpTransact::ACTIVE_TIMEOUT:
    return "ACTIVE_TIMEOUT";
  case HttpTransact::BAD_INCOMING_RESPONSE:
    return "BAD_INCOMING_RESPONSE";
  case HttpTransact::CONNECTION_ALIVE:
    return "CONNECTION_ALIVE";
  case HttpTransact::CONNECTION_CLOSED:
    return "CONNECTION_CLOSED";
  case HttpTransact::CONNECTION_ERROR:
    return "CONNECTION_ERROR";
  case HttpTransact::INACTIVE_TIMEOUT:
    return "INACTIVE_TIMEOUT";
  case HttpTransact::OPEN_RAW_ERROR:
    return "OPEN_RAW_ERROR";
  case HttpTransact::PARSE_ERROR:
    return "PARSE_ERROR";
  case HttpTransact::TRANSACTION_COMPLETE:
    return "TRANSACTION_COMPLETE";
  case HttpTransact::PARENT_RETRY:
    return "PARENT_RETRY";
  case HttpTransact::OUTBOUND_CONGESTION:
    return "OUTBOUND_CONGESTION";
  }

  return ("unknown state name");
}

//////////////////////////////////////////////////////////////////
//
//  HttpDebugNames::get_method_name()
//
//////////////////////////////////////////////////////////////////
const char *
HttpDebugNames::get_method_name(const char *method)
{
  if (method == HTTP_METHOD_CONNECT.c_str()) {
    return ("HTTP_METHOD_CONNECT");
  } else if (method == HTTP_METHOD_DELETE.c_str()) {
    return ("HTTP_METHOD_DELETE");
  } else if (method == HTTP_METHOD_GET.c_str()) {
    return ("HTTP_METHOD_GET");
  } else if (method == HTTP_METHOD_HEAD.c_str()) {
    return ("HTTP_METHOD_HEAD");
  } else if (method == HTTP_METHOD_OPTIONS.c_str()) {
    return ("HTTP_METHOD_OPTIONS");
  } else if (method == HTTP_METHOD_POST.c_str()) {
    return ("HTTP_METHOD_POST");
  } else if (method == HTTP_METHOD_PURGE.c_str()) {
    return ("HTTP_METHOD_PURGE");
  } else if (method == HTTP_METHOD_PUT.c_str()) {
    return ("HTTP_METHOD_PUT");
  } else if (method == HTTP_METHOD_TRACE.c_str()) {
    return ("HTTP_METHOD_TRACE");
  } else {
    return ("HTTP_METHOD_UNKNOWN");
  }
}

//////////////////////////////////////////////////////////////////
//
//  HttpDebugNames::get_event_name()
//
//////////////////////////////////////////////////////////////////
const char *
HttpDebugNames::get_event_name(int event)
{
  switch (event) {
  case EVENT_NONE:
    static_assert(static_cast<int>(EVENT_NONE) == static_cast<int>(VC_EVENT_NONE));
    return "EVENT_NONE/VC_EVENT_NONE";
  case EVENT_IMMEDIATE:
    static_assert(static_cast<int>(EVENT_IMMEDIATE) == static_cast<int>(TS_EVENT_IMMEDIATE));
    static_assert(static_cast<int>(EVENT_IMMEDIATE) == static_cast<int>(VC_EVENT_IMMEDIATE));
    return "EVENT_IMMEDIATE/TS_EVENT_IMMEDIATE/VC_EVENT_IMMEDIATE";
  case EVENT_ERROR:
    static_assert(static_cast<int>(EVENT_ERROR) == static_cast<int>(TS_EVENT_ERROR));
    static_assert(static_cast<int>(EVENT_ERROR) == static_cast<int>(VC_EVENT_ERROR));
    return "EVENT_ERROR/TS_EVENT_ERROR/VC_EVENT_ERROR";
  case EVENT_INTERVAL:
    return "EVENT_INTERVAL";
  case VC_EVENT_READ_READY:
    static_assert(static_cast<int>(VC_EVENT_READ_READY) == static_cast<int>(TS_EVENT_VCONN_READ_READY));
    return "VC_EVENT_READ_READY/TS_EVENT_VCONN_READ_READY";
  case VC_EVENT_WRITE_READY:
    static_assert(static_cast<int>(VC_EVENT_WRITE_READY) == static_cast<int>(TS_EVENT_VCONN_WRITE_READY));
    return "VC_EVENT_WRITE_READY/TS_EVENT_VCONN_WRITE_READY";
  case VC_EVENT_READ_COMPLETE:
    static_assert(static_cast<int>(VC_EVENT_READ_COMPLETE) == static_cast<int>(TS_EVENT_VCONN_READ_COMPLETE));
    return "VC_EVENT_READ_COMPLETE/TS_EVENT_VCONN_READ_COMPLETE";
  case VC_EVENT_WRITE_COMPLETE:
    static_assert(static_cast<int>(VC_EVENT_WRITE_COMPLETE) == static_cast<int>(TS_EVENT_VCONN_WRITE_COMPLETE));
    return "VC_EVENT_WRITE_COMPLETE/TS_EVENT_VCONN_WRITE_COMPLETE";
  case VC_EVENT_EOS:
    static_assert(static_cast<int>(VC_EVENT_EOS) == static_cast<int>(TS_EVENT_VCONN_EOS));
    return "VC_EVENT_EOS/TS_EVENT_VCONN_EOS";
  case VC_EVENT_INACTIVITY_TIMEOUT:
    static_assert(static_cast<int>(VC_EVENT_INACTIVITY_TIMEOUT) == static_cast<int>(TS_EVENT_VCONN_INACTIVITY_TIMEOUT));
    return "VC_EVENT_INACTIVITY_TIMEOUT/TS_EVENT_VCONN_INACTIVITY_TIMEOUT";
  case VC_EVENT_ACTIVE_TIMEOUT:
    static_assert(static_cast<int>(VC_EVENT_ACTIVE_TIMEOUT) == static_cast<int>(TS_EVENT_VCONN_ACTIVE_TIMEOUT));
    return "VC_EVENT_ACTIVE_TIMEOUT/TS_EVENT_VCONN_ACTIVE_TIMEOUT";

  /////////////////
  // NET  EVENTS //
  /////////////////
  case NET_EVENT_OPEN:
    static_assert(static_cast<int>(NET_EVENT_OPEN) == static_cast<int>(TS_EVENT_NET_CONNECT));
    return "NET_EVENT_OPEN/TS_EVENT_NET_CONNECT";
  case NET_EVENT_ACCEPT:
    static_assert(static_cast<int>(NET_EVENT_ACCEPT) == static_cast<int>(TS_EVENT_NET_ACCEPT));
    return "NET_EVENT_ACCEPT/TS_EVENT_NET_ACCEPT";
  case NET_EVENT_OPEN_FAILED:
    static_assert(static_cast<int>(NET_EVENT_OPEN_FAILED) == static_cast<int>(TS_EVENT_NET_CONNECT_FAILED));
    return "NET_EVENT_OPEN_FAILED/TS_EVENT_NET_CONNECT_FAILED";

  ////////////////////
  // HOSTDB  EVENTS //
  ////////////////////
  case EVENT_HOST_DB_LOOKUP:
    static_assert(static_cast<int>(EVENT_HOST_DB_LOOKUP) == static_cast<int>(TS_EVENT_HOST_LOOKUP));
    return "EVENT_HOST_DB_LOOKUP/TS_EVENT_HOST_LOOKUP";
  case EVENT_HOST_DB_GET_RESPONSE:
    return "EVENT_HOST_DB_GET_RESPONSE";
  case EVENT_SRV_LOOKUP:
    return "EVENT_SRV_LOOKUP";
  case EVENT_SRV_IP_REMOVED:
    return "EVENT_SRV_IP_REMOVED";
  case EVENT_SRV_GET_RESPONSE:
    return "EVENT_SRV_GET_RESPONSE";

  ////////////////////
  // DNS     EVENTS //
  ////////////////////
  case DNS_EVENT_LOOKUP:
    return "DNS_EVENT_LOOKUP";

  ////////////////////
  // CACHE   EVENTS //
  ////////////////////
  case CACHE_EVENT_LOOKUP_FAILED:
    return "CACHE_EVENT_LOOKUP_FAILED";
  case CACHE_EVENT_OPEN_READ:
    static_assert(static_cast<int>(CACHE_EVENT_OPEN_READ) == static_cast<int>(TS_EVENT_CACHE_OPEN_READ));
    return "CACHE_EVENT_OPEN_READ/TS_EVENT_CACHE_OPEN_READ";
  case CACHE_EVENT_OPEN_READ_FAILED:
    static_assert(static_cast<int>(CACHE_EVENT_OPEN_READ_FAILED) == static_cast<int>(TS_EVENT_CACHE_OPEN_READ_FAILED));
    return "CACHE_EVENT_OPEN_READ_FAILED/TS_EVENT_CACHE_OPEN_READ_FAILED";
  case CACHE_EVENT_OPEN_WRITE:
    static_assert(static_cast<int>(CACHE_EVENT_OPEN_WRITE) == static_cast<int>(TS_EVENT_CACHE_OPEN_WRITE));
    return "CACHE_EVENT_OPEN_WRITE/TS_EVENT_CACHE_OPEN_WRITE";
  case CACHE_EVENT_OPEN_WRITE_FAILED:
    static_assert(static_cast<int>(CACHE_EVENT_OPEN_WRITE_FAILED) == static_cast<int>(TS_EVENT_CACHE_OPEN_WRITE_FAILED));
    return "CACHE_EVENT_OPEN_WRITE_FAILED/TS_EVENT_CACHE_OPEN_WRITE_FAILED";
  case CACHE_EVENT_REMOVE:
    static_assert(static_cast<int>(CACHE_EVENT_REMOVE) == static_cast<int>(TS_EVENT_CACHE_REMOVE));
    return "CACHE_EVENT_REMOVE/TS_EVENT_CACHE_REMOVE";
  case CACHE_EVENT_REMOVE_FAILED:
    static_assert(static_cast<int>(CACHE_EVENT_REMOVE_FAILED) == static_cast<int>(TS_EVENT_CACHE_REMOVE_FAILED));
    return "CACHE_EVENT_REMOVE_FAILED/TS_EVENT_CACHE_REMOVE_FAILED";
  case CACHE_EVENT_UPDATE:
    return "CACHE_EVENT_UPDATE";
  case CACHE_EVENT_UPDATE_FAILED:
    return "CACHE_EVENT_UPDATE_FAILED";
  case TRANSFORM_READ_READY:
    static_assert(static_cast<int>(TRANSFORM_READ_READY) == static_cast<int>(TS_EVENT_SSL_SESSION_GET));
    return "TRANSFORM_READ_READY/TS_EVENT_SSL_SESSION_GET";

  /////////////////////////
  //  HttpTunnel Events  //
  /////////////////////////
  case HTTP_TUNNEL_EVENT_DONE:
    return "HTTP_TUNNEL_EVENT_DONE";
  case HTTP_TUNNEL_EVENT_PRECOMPLETE:
    return "HTTP_TUNNEL_EVENT_PRECOMPLETE";
  case HTTP_TUNNEL_EVENT_CONSUMER_DETACH:
    return "HTTP_TUNNEL_EVENT_CONSUMER_DETACH";

  /////////////////////////////
  //  Plugin Events
  /////////////////////////////
  case HTTP_API_CONTINUE:
    static_assert(static_cast<int>(HTTP_API_CONTINUE) == static_cast<int>(TS_EVENT_HTTP_CONTINUE));
    return "HTTP_API_CONTINUE/TS_EVENT_HTTP_CONTINUE";
  case HTTP_API_ERROR:
    static_assert(static_cast<int>(HTTP_API_ERROR) == static_cast<int>(TS_EVENT_HTTP_ERROR));
    return "HTTP_API_ERROR/TS_EVENT_HTTP_ERROR";

  case TS_EVENT_NET_ACCEPT_FAILED:
    return "TS_EVENT_NET_ACCEPT_FAILED";
  case TS_EVENT_CACHE_SCAN:
    return "TS_EVENT_CACHE_SCAN";
  case TS_EVENT_CACHE_SCAN_FAILED:
    return "TS_EVENT_CACHE_SCAN_FAILED";
  case TS_EVENT_CACHE_SCAN_OBJECT:
    return "TS_EVENT_CACHE_SCAN_OBJECT";
  case TS_EVENT_CACHE_SCAN_OPERATION_BLOCKED:
    return "TS_EVENT_CACHE_SCAN_OPERATION_BLOCKED";
  case TS_EVENT_CACHE_SCAN_OPERATION_FAILED:
    return "TS_EVENT_CACHE_SCAN_OPERATION_FAILED";
  case TS_EVENT_CACHE_SCAN_DONE:
    return "TS_EVENT_CACHE_SCAN_DONE";
  case TS_EVENT_CACHE_LOOKUP:
    return "TS_EVENT_CACHE_LOOKUP";
  case TS_EVENT_CACHE_READ:
    return "TS_EVENT_CACHE_READ";
  case TS_EVENT_CACHE_DELETE:
    return "TS_EVENT_CACHE_DELETE";
  case TS_EVENT_CACHE_WRITE:
    return "TS_EVENT_CACHE_WRITE";
  case TS_EVENT_CACHE_WRITE_HEADER:
    return "TS_EVENT_CACHE_WRITE_HEADER";
  case TS_EVENT_CACHE_CLOSE:
    return "TS_EVENT_CACHE_CLOSE";
  case TS_EVENT_CACHE_LOOKUP_READY:
    return "TS_EVENT_CACHE_LOOKUP_READY";
  case TS_EVENT_CACHE_LOOKUP_COMPLETE:
    return "TS_EVENT_CACHE_LOOKUP_COMPLETE";
  case TS_EVENT_CACHE_READ_READY:
    return "TS_EVENT_CACHE_READ_READY";
  case TS_EVENT_CACHE_READ_COMPLETE:
    return "TS_EVENT_CACHE_READ_COMPLETE";
  case TS_EVENT_INTERNAL_1200:
    return "TS_EVENT_INTERNAL_1200";
  case TS_EVENT_SSL_SESSION_NEW:
    return "TS_EVENT_SSL_SESSION_NEW";
  case TS_EVENT_SSL_SESSION_REMOVE:
    return "TS_EVENT_SSL_SESSION_REMOVE";
  case TS_EVENT_AIO_DONE:
    return "TS_EVENT_AIO_DONE";
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    return "TS_EVENT_HTTP_READ_REQUEST_HDR";
  case TS_EVENT_HTTP_OS_DNS:
    return "TS_EVENT_HTTP_OS_DNS";
  case TS_EVENT_HTTP_SEND_REQUEST_HDR:
    return "TS_EVENT_HTTP_SEND_REQUEST_HDR";
  case TS_EVENT_HTTP_READ_CACHE_HDR:
    return "TS_EVENT_HTTP_READ_CACHE_HDR";
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    return "TS_EVENT_HTTP_READ_RESPONSE_HDR";
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    return "TS_EVENT_HTTP_SEND_RESPONSE_HDR";
  case TS_EVENT_HTTP_REQUEST_TRANSFORM:
    return "TS_EVENT_HTTP_REQUEST_TRANSFORM";
  case TS_EVENT_HTTP_RESPONSE_TRANSFORM:
    return "TS_EVENT_HTTP_RESPONSE_TRANSFORM";
  case TS_EVENT_HTTP_SELECT_ALT:
    return "TS_EVENT_HTTP_SELECT_ALT";
  case TS_EVENT_HTTP_TXN_START:
    return "TS_EVENT_HTTP_TXN_START";
  case TS_EVENT_HTTP_TXN_CLOSE:
    return "TS_EVENT_HTTP_TXN_CLOSE";
  case TS_EVENT_HTTP_SSN_START:
    return "TS_EVENT_HTTP_SSN_START";
  case TS_EVENT_HTTP_SSN_CLOSE:
    return "TS_EVENT_HTTP_SSN_CLOSE";
  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    return "TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE";
  case TS_EVENT_HTTP_PRE_REMAP:
    return "TS_EVENT_HTTP_PRE_REMAP";
  case TS_EVENT_HTTP_POST_REMAP:
    return "TS_EVENT_HTTP_POST_REMAP";
  case TS_EVENT_HTTP_TUNNEL_START:
    return "TS_EVENT_HTTP_TUNNEL_START";
  case TS_EVENT_LIFECYCLE_PORTS_INITIALIZED:
    return "TS_EVENT_LIFECYCLE_PORTS_INITIALIZED";
  case TS_EVENT_LIFECYCLE_PORTS_READY:
    return "TS_EVENT_LIFECYCLE_PORTS_READY";
  case TS_EVENT_LIFECYCLE_CACHE_READY:
    return "TS_EVENT_LIFECYCLE_CACHE_READY";
  case TS_EVENT_LIFECYCLE_SERVER_SSL_CTX_INITIALIZED:
    return "TS_EVENT_LIFECYCLE_SERVER_SSL_CTX_INITIALIZED";
  case TS_EVENT_LIFECYCLE_CLIENT_SSL_CTX_INITIALIZED:
    return "TS_EVENT_LIFECYCLE_CLIENT_SSL_CTX_INITIALIZED";
  case TS_EVENT_LIFECYCLE_TASK_THREADS_READY:
    return "TS_EVENT_LIFECYCLE_TASK_THREADS_READY";
  case TS_EVENT_LIFECYCLE_SHUTDOWN:
    return "TS_EVENT_LIFECYCLE_SHUTDOWN";
  case TS_EVENT_VCONN_START:
    return "TS_EVENT_VCONN_START";
  case TS_EVENT_VCONN_CLOSE:
    return "TS_EVENT_VCONN_CLOSE";
  case TS_EVENT_LIFECYCLE_MSG:
    return "TS_EVENT_LIFECYCLE_MSG";
  case TS_EVENT_HTTP_REQUEST_BUFFER_READ_COMPLETE:
    return "TS_EVENT_HTTP_REQUEST_BUFFER_READ_COMPLETE";
  case TS_EVENT_MGMT_UPDATE:
    return "TS_EVENT_MGMT_UPDATE";
  case TS_EVENT_INTERNAL_60200:
    return "TS_EVENT_INTERNAL_60200";
  case TS_EVENT_INTERNAL_60201:
    return "TS_EVENT_INTERNAL_60201";
  case TS_EVENT_INTERNAL_60202:
    return "TS_EVENT_INTERNAL_60202";
  case TS_EVENT_SSL_CLIENT_HELLO:
    return "TS_EVENT_SSL_CLIENT_HELLO";
  case TS_EVENT_SSL_CERT:
    return "TS_EVENT_SSL_CERT";
  case TS_EVENT_SSL_SERVERNAME:
    return "TS_EVENT_SSL_SERVERNAME";
  case TS_EVENT_SSL_VERIFY_SERVER:
    return "TS_EVENT_SSL_VERIFY_SERVER";
  case TS_EVENT_SSL_VERIFY_CLIENT:
    return "TS_EVENT_SSL_VERIFY_CLIENT";
  case TS_EVENT_VCONN_OUTBOUND_START:
    return "TS_EVENT_VCONN_OUTBOUND_START";
  case TS_EVENT_VCONN_OUTBOUND_CLOSE:
    return "TS_EVENT_VCONN_OUTBOUND_CLOSE";
  }

  return ("unknown event");
}

//////////////////////////////////////////////////////////////////
//
//  HttpDebugNames::get_action_name()
//
//////////////////////////////////////////////////////////////////
const char *
HttpDebugNames::get_action_name(HttpTransact::StateMachineAction_t e)
{
  switch (e) {
  case HttpTransact::StateMachineAction_t::UNDEFINED:
    return ("StateMachineAction_t::UNDEFINED");

  case HttpTransact::StateMachineAction_t::CACHE_ISSUE_WRITE:
    return ("StateMachineAction_t::CACHE_ISSUE_WRITE");

  case HttpTransact::StateMachineAction_t::CACHE_ISSUE_WRITE_TRANSFORM:
    return ("StateMachineAction_t::CACHE_ISSUE_WRITE_TRANSFORM");

  case HttpTransact::StateMachineAction_t::CACHE_LOOKUP:
    return ("StateMachineAction_t::CACHE_LOOKUP");

  case HttpTransact::StateMachineAction_t::DNS_LOOKUP:
    return ("StateMachineAction_t::DNS_LOOKUP");

  case HttpTransact::StateMachineAction_t::DNS_REVERSE_LOOKUP:
    return ("StateMachineAction_t::DNS_REVERSE_LOOKUP");

  case HttpTransact::StateMachineAction_t::CACHE_PREPARE_UPDATE:
    return ("StateMachineAction_t::CACHE_PREPARE_UPDATE");

  case HttpTransact::StateMachineAction_t::CACHE_ISSUE_UPDATE:
    return ("StateMachineAction_t::CACHE_ISSUE_UPDATE");

  case HttpTransact::StateMachineAction_t::ORIGIN_SERVER_OPEN:
    return ("StateMachineAction_t::ORIGIN_SERVER_OPEN");

  case HttpTransact::StateMachineAction_t::ORIGIN_SERVER_RAW_OPEN:
    return ("StateMachineAction_t::ORIGIN_SERVER_RAW_OPEN");

  case HttpTransact::StateMachineAction_t::ORIGIN_SERVER_RR_MARK_DOWN:
    return ("StateMachineAction_t::ORIGIN_SERVER_RR_MARK_DOWN");

  case HttpTransact::StateMachineAction_t::READ_PUSH_HDR:
    return ("StateMachineAction_t::READ_PUSH_HDR");

  case HttpTransact::StateMachineAction_t::STORE_PUSH_BODY:
    return ("StateMachineAction_t::STORE_PUSH_BODY");

  case HttpTransact::StateMachineAction_t::INTERNAL_CACHE_WRITE:
    return ("StateMachineAction_t::INTERNAL_CACHE_WRITE");

  case HttpTransact::StateMachineAction_t::INTERNAL_CACHE_DELETE:
    return ("StateMachineAction_t::INTERNAL_CACHE_DELETE");

  case HttpTransact::StateMachineAction_t::INTERNAL_CACHE_NOOP:
    return ("StateMachineAction_t::INTERNAL_CACHE_NOOP");

  case HttpTransact::StateMachineAction_t::INTERNAL_CACHE_UPDATE_HEADERS:
    return ("StateMachineAction_t::INTERNAL_CACHE_UPDATE_HEADERS");

  case HttpTransact::StateMachineAction_t::SEND_ERROR_CACHE_NOOP:
    return ("StateMachineAction_t::SEND_ERROR_CACHE_NOOP");

  case HttpTransact::StateMachineAction_t::SERVE_FROM_CACHE:
    return ("StateMachineAction_t::SERVE_FROM_CACHE");

  case HttpTransact::StateMachineAction_t::SERVER_READ:
    return ("StateMachineAction_t::SERVER_READ");

  case HttpTransact::StateMachineAction_t::SSL_TUNNEL:
    return ("StateMachineAction_t::SSL_TUNNEL");

  case HttpTransact::StateMachineAction_t::CONTINUE:
    return ("StateMachineAction_t::CONTINUE");

  case HttpTransact::StateMachineAction_t::API_READ_REQUEST_HDR:
    return ("StateMachineAction_t::API_READ_REQUEST_HDR");

  case HttpTransact::StateMachineAction_t::API_TUNNEL_START:
    return ("StateMachineAction_t::API_TUNNEL_START");

  case HttpTransact::StateMachineAction_t::API_OS_DNS:
    return ("StateMachineAction_t::API_OS_DNS");

  case HttpTransact::StateMachineAction_t::API_SEND_REQUEST_HDR:
    return ("StateMachineAction_t::API_SEND_REQUEST_HDR");

  case HttpTransact::StateMachineAction_t::API_READ_CACHE_HDR:
    return ("StateMachineAction_t::API_READ_CACHE_HDR");

  case HttpTransact::StateMachineAction_t::API_CACHE_LOOKUP_COMPLETE:
    return ("StateMachineAction_t::API_CACHE_LOOKUP_COMPLETE");

  case HttpTransact::StateMachineAction_t::API_READ_RESPONSE_HDR:
    return ("StateMachineAction_t::API_READ_RESPONSE_HDR");

  case HttpTransact::StateMachineAction_t::API_SEND_RESPONSE_HDR:
    return ("StateMachineAction_t::API_SEND_RESPONSE_HDR");

  case HttpTransact::StateMachineAction_t::INTERNAL_100_RESPONSE:
    return ("StateMachineAction_t::INTERNAL_100_RESPONSE");

  case HttpTransact::StateMachineAction_t::SERVER_PARSE_NEXT_HDR:
    return ("StateMachineAction_t::SERVER_PARSE_NEXT_HDR");

  case HttpTransact::StateMachineAction_t::TRANSFORM_READ:
    return ("StateMachineAction_t::TRANSFORM_READ");

  case HttpTransact::StateMachineAction_t::WAIT_FOR_FULL_BODY:
    return ("StateMachineAction_t::WAIT_FOR_FULL_BODY");

  case HttpTransact::StateMachineAction_t::REQUEST_BUFFER_READ_COMPLETE:
    return ("StateMachineAction_t::REQUEST_BUFFER_READ_COMPLETE");
  case HttpTransact::StateMachineAction_t::API_SM_START:
    return ("StateMachineAction_t::API_SM_START");
  case HttpTransact::StateMachineAction_t::REDIRECT_READ:
    return ("StateMachineAction_t::REDIRECT_READ");
  case HttpTransact::StateMachineAction_t::API_SM_SHUTDOWN:
    return ("StateMachineAction_t::API_SM_SHUTDOWN");
  case HttpTransact::StateMachineAction_t::REMAP_REQUEST:
    return ("StateMachineAction_t::REMAP_REQUEST");
  case HttpTransact::StateMachineAction_t::API_PRE_REMAP:
    return ("StateMachineAction_t::API_PRE_REMAP");
  case HttpTransact::StateMachineAction_t::API_POST_REMAP:
    return ("StateMachineAction_t::API_POST_REMAP");
  case HttpTransact::StateMachineAction_t::POST_REMAP_SKIP:
    return ("StateMachineAction_t::POST_REMAP_SKIP");
  }

  return ("unknown state name");
}

//////////////////////////////////////////////////////////////////
//
//  HttpDebugNames::get_cache_action_name()
//
//////////////////////////////////////////////////////////////////
const char *
HttpDebugNames::get_cache_action_name(HttpTransact::CacheAction_t t)
{
  switch (t) {
  case HttpTransact::CacheAction_t::UNDEFINED:
    return ("CacheAction_t::UNDEFINED");
  case HttpTransact::CacheAction_t::NO_ACTION:
    return ("CacheAction_t::NO_ACTION");
  case HttpTransact::CacheAction_t::DELETE:
    return ("CacheAction_t::DELETE");
  case HttpTransact::CacheAction_t::LOOKUP:
    return ("CacheAction_t::LOOKUP");
  case HttpTransact::CacheAction_t::REPLACE:
    return ("CacheAction_t::REPLACE");
  case HttpTransact::CacheAction_t::SERVE:
    return ("CacheAction_t::SERVE");
  case HttpTransact::CacheAction_t::SERVE_AND_DELETE:
    return ("CacheAction_t::SERVE_AND_DELETE");
  case HttpTransact::CacheAction_t::SERVE_AND_UPDATE:
    return ("CacheAction_t::SERVE_AND_UPDATE");
  case HttpTransact::CacheAction_t::UPDATE:
    return ("CacheAction_t::UPDATE");
  case HttpTransact::CacheAction_t::WRITE:
    return ("CacheAction_t::WRITE");
  case HttpTransact::CacheAction_t::PREPARE_TO_DELETE:
    return ("CacheAction_t::PREPARE_TO_DELETE");
  case HttpTransact::CacheAction_t::PREPARE_TO_UPDATE:
    return ("CacheAction_t::PREPARE_TO_UPDATE");
  case HttpTransact::CacheAction_t::PREPARE_TO_WRITE:
    return ("CacheAction_t::PREPARE_TO_WRITE");
  case HttpTransact::CacheAction_t::TOTAL_TYPES:
    return ("CacheAction_t::TOTAL_TYPES");
  }

  return ("unknown cache action");
}

//////////////////////////////////////////////////////////////////
//
//  HttpDebugNames::get_api_hook_name()
//
//////////////////////////////////////////////////////////////////
const char *
HttpDebugNames::get_api_hook_name(TSHttpHookID t)
{
  switch (t) {
  case TS_HTTP_READ_REQUEST_HDR_HOOK:
    return "TS_HTTP_READ_REQUEST_HDR_HOOK";
  case TS_HTTP_TUNNEL_START_HOOK:
    return "TS_HTTP_TUNNEL_START_HOOK";
  case TS_HTTP_OS_DNS_HOOK:
    return "TS_HTTP_OS_DNS_HOOK";
  case TS_HTTP_SEND_REQUEST_HDR_HOOK:
    return "TS_HTTP_SEND_REQUEST_HDR_HOOK";
  case TS_HTTP_READ_CACHE_HDR_HOOK:
    return "TS_HTTP_READ_CACHE_HDR_HOOK";
  case TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK:
    return "TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK";
  case TS_HTTP_READ_RESPONSE_HDR_HOOK:
    return "TS_HTTP_READ_RESPONSE_HDR_HOOK";
  case TS_HTTP_SEND_RESPONSE_HDR_HOOK:
    return "TS_HTTP_SEND_RESPONSE_HDR_HOOK";
  case TS_HTTP_REQUEST_TRANSFORM_HOOK:
    return "TS_HTTP_REQUEST_TRANSFORM_HOOK";
  case TS_HTTP_REQUEST_BUFFER_READ_COMPLETE_HOOK:
    return "TS_HTTP_REQUEST_BUFFER_READ_COMPLETE_HOOK";
  case TS_HTTP_RESPONSE_TRANSFORM_HOOK:
    return "TS_HTTP_RESPONSE_TRANSFORM_HOOK";
  case TS_HTTP_SELECT_ALT_HOOK:
    return "TS_HTTP_SELECT_ALT_HOOK";
  case TS_HTTP_TXN_START_HOOK:
    return "TS_HTTP_TXN_START_HOOK";
  case TS_HTTP_TXN_CLOSE_HOOK:
    return "TS_HTTP_TXN_CLOSE_HOOK";
  case TS_HTTP_SSN_START_HOOK:
    return "TS_HTTP_SSN_START_HOOK";
  case TS_HTTP_SSN_CLOSE_HOOK:
    return "TS_HTTP_SSN_CLOSE_HOOK";
  case TS_HTTP_PRE_REMAP_HOOK:
    return "TS_HTTP_PRE_REMAP_HOOK";
  case TS_HTTP_POST_REMAP_HOOK:
    return "TS_HTTP_POST_REMAP_HOOK";
  case TS_HTTP_RESPONSE_CLIENT_HOOK:
    return "TS_HTTP_RESPONSE_CLIENT_HOOK";
  case TS_HTTP_REQUEST_CLIENT_HOOK:
    return "TS_HTTP_REQUEST_CLIENT_HOOK";
  case TS_HTTP_LAST_HOOK:
    return "TS_HTTP_LAST_HOOK";
  case TS_VCONN_START_HOOK:
    return "TS_VCONN_START_HOOK";
  case TS_VCONN_CLOSE_HOOK:
    return "TS_VCONN_CLOSE_HOOK";
  case TS_SSL_CLIENT_HELLO_HOOK:
    return "TS_SSL_CLIENT_HELLO_HOOK";
  case TS_SSL_CERT_HOOK:
    return "TS_SSL_CERT_HOOK";
  case TS_SSL_SERVERNAME_HOOK:
    return "TS_SSL_SERVERNAME_HOOK";
  case TS_SSL_VERIFY_SERVER_HOOK:
    return "TS_SSL_VERIFY_SERVER_HOOK";
  case TS_SSL_VERIFY_CLIENT_HOOK:
    return "TS_SSL_VERIFY_CLIENT_HOOK";
  case TS_SSL_SESSION_HOOK:
    return "TS_SSL_SESSION_HOOK";
  case TS_VCONN_OUTBOUND_START_HOOK:
    return "TS_VCONN_OUTBOUND_START_HOOK";
  case TS_VCONN_OUTBOUND_CLOSE_HOOK:
    return "TS_VCONN_OUTBOUND_CLOSE_HOOK";
  }

  return "unknown hook";
}

swoc::BufferWriter &
bwformat(swoc::BufferWriter &w, swoc::bwf::Spec const &spec, HttpTransact::ServerState_t state)
{
  if (spec.has_numeric_type()) {
    return bwformat(w, spec, static_cast<uintmax_t>(state));
  } else {
    return bwformat(w, spec, HttpDebugNames::get_server_state_name(state));
  }
}

swoc::BufferWriter &
bwformat(swoc::BufferWriter &w, swoc::bwf::Spec const &spec, HttpTransact::CacheAction_t state)
{
  if (spec.has_numeric_type()) {
    return bwformat(w, spec, static_cast<uintmax_t>(state));
  } else {
    return bwformat(w, spec, HttpDebugNames::get_cache_action_name(state));
  }
}

swoc::BufferWriter &
bwformat(swoc::BufferWriter &w, swoc::bwf::Spec const &spec, HttpTransact::StateMachineAction_t state)
{
  if (spec.has_numeric_type()) {
    return bwformat(w, spec, static_cast<uintmax_t>(state));
  } else {
    return bwformat(w, spec, HttpDebugNames::get_action_name(state));
  }
}

swoc::BufferWriter &
bwformat(swoc::BufferWriter &w, swoc::bwf::Spec const &spec, TSHttpHookID id)
{
  if (spec.has_numeric_type()) {
    return bwformat(w, spec, static_cast<uintmax_t>(id));
  } else {
    return bwformat(w, spec, HttpDebugNames::get_api_hook_name(id));
  }
}
