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

#include "HttpDebugNames.h"
#include "P_EventSystem.h"
#include "StatPages.h"
#include "HttpTunnel.h"
#include "Transform.h"
#include "ICPevents.h"
#include "HttpSM.h"
#include "HttpUpdateSM.h"

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
  case HttpTransact::CONGEST_CONTROL_CONGESTED_ON_F:
    return "CONGEST_CONTROL_CONGESTED_ON_F";
  case HttpTransact::CONGEST_CONTROL_CONGESTED_ON_M:
    return "CONGEST_CONTROL_CONGESTED_ON_M";
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
  if (method == HTTP_METHOD_CONNECT) {
    return ("HTTP_METHOD_CONNECT");
  } else if (method == HTTP_METHOD_DELETE) {
    return ("HTTP_METHOD_DELETE");
  } else if (method == HTTP_METHOD_GET) {
    return ("HTTP_METHOD_GET");
  } else if (method == HTTP_METHOD_HEAD) {
    return ("HTTP_METHOD_HEAD");
  } else if (method == HTTP_METHOD_OPTIONS) {
    return ("HTTP_METHOD_OPTIONS");
  } else if (method == HTTP_METHOD_POST) {
    return ("HTTP_METHOD_POST");
  } else if (method == HTTP_METHOD_PURGE) {
    return ("HTTP_METHOD_PURGE");
  } else if (method == HTTP_METHOD_PUT) {
    return ("HTTP_METHOD_PUT");
  } else if (method == HTTP_METHOD_TRACE) {
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
    /////////////////////////
    // VCONNECTION  EVENTS //
    /////////////////////////
  case VC_EVENT_NONE:
    return ("VC_EVENT_NONE");
  case VC_EVENT_IMMEDIATE:
    return ("VC_EVENT_IMMEDIATE");
  case VC_EVENT_READ_READY:
    return ("VC_EVENT_READ_READY");
  case VC_EVENT_WRITE_READY:
    return ("VC_EVENT_WRITE_READY");
  case VC_EVENT_READ_COMPLETE:
    return ("VC_EVENT_READ_COMPLETE");
  case VC_EVENT_WRITE_COMPLETE:
    return ("VC_EVENT_WRITE_COMPLETE");
  case VC_EVENT_EOS:
    return ("VC_EVENT_EOS");
  case VC_EVENT_ERROR:
    return ("VC_EVENT_ERROR");
  case VC_EVENT_INACTIVITY_TIMEOUT:
    return ("VC_EVENT_INACTIVITY_TIMEOUT");
  case VC_EVENT_ACTIVE_TIMEOUT:
    return ("VC_EVENT_ACTIVE_TIMEOUT");
  case EVENT_INTERVAL:
    return ("VC_EVENT_INTERVAL");


    /////////////////
    // NET  EVENTS //
    /////////////////
  case NET_EVENT_OPEN:
    return ("NET_EVENT_OPEN");
  case NET_EVENT_ACCEPT:
    return ("NET_EVENT_ACCEPT");
  case NET_EVENT_OPEN_FAILED:
    return ("NET_EVENT_OPEN_FAILED");

    ////////////////////
    // HOSTDB  EVENTS //
    ////////////////////
  case EVENT_HOST_DB_LOOKUP:
    return ("EVENT_HOST_DB_LOOKUP");

  case EVENT_HOST_DB_GET_RESPONSE:
    return ("EVENT_HOST_DB_GET_RESPONSE");

    ////////////////////
    // HOSTDB  EVENTS //
    ////////////////////
  case EVENT_SRV_LOOKUP:
    return ("EVENT_SRV_LOOKUP");

  case EVENT_SRV_IP_REMOVED:
    return ("EVENT_SRV_IP_REMOVED");

  case EVENT_SRV_GET_RESPONSE:
    return ("EVENT_SRV_GET_RESPONSE");


    ////////////////////
    // DNS     EVENTS //
    ////////////////////
  case DNS_EVENT_LOOKUP:
    return ("DNS_EVENT_LOOKUP");

    ////////////////////
    // CACHE   EVENTS //
    ////////////////////

  case CACHE_EVENT_LOOKUP:
    return ("CACHE_EVENT_LOOKUP");
  case CACHE_EVENT_LOOKUP_FAILED:
    return ("CACHE_EVENT_LOOKUP_FAILED");
  case CACHE_EVENT_OPEN_READ:
    return ("CACHE_EVENT_OPEN_READ");
  case CACHE_EVENT_OPEN_READ_FAILED:
    return ("CACHE_EVENT_OPEN_READ_FAILED");
  case CACHE_EVENT_OPEN_WRITE:
    return ("CACHE_EVENT_OPEN_WRITE");
  case CACHE_EVENT_OPEN_WRITE_FAILED:
    return ("CACHE_EVENT_OPEN_WRITE_FAILED");
  case CACHE_EVENT_REMOVE:
    return ("CACHE_EVENT_REMOVE");
  case CACHE_EVENT_REMOVE_FAILED:
    return ("CACHE_EVENT_REMOVE_FAILED");
  case CACHE_EVENT_UPDATE:
    return ("CACHE_EVENT_UPDATE");
  case CACHE_EVENT_UPDATE_FAILED:
    return ("CACHE_EVENT_UPDATE_FAILED");

  case STAT_PAGE_SUCCESS:
    return ("STAT_PAGE_SUCCESS");
  case STAT_PAGE_FAILURE:
    return ("STAT_PAGE_FAILURE");

  case TRANSFORM_READ_READY:
    return ("TRANSFORM_READ_READY");

    /////////////////////////
    //  HttpTunnel Events //
    /////////////////////////
  case HTTP_TUNNEL_EVENT_DONE:
    return ("HTTP_TUNNEL_EVENT_DONE");
  case HTTP_TUNNEL_EVENT_PRECOMPLETE:
    return ("HTTP_TUNNEL_EVENT_PRECOMPLETE");
  case HTTP_TUNNEL_EVENT_CONSUMER_DETACH:
    return ("HTTP_TUNNEL_EVENT_CONSUMER_DETACH");

    //////////////////////////
    //  ICP Events
    //////////////////////////
  case ICP_LOOKUP_FOUND:
    return ("ICP_LOOKUP_FOUND");
  case ICP_LOOKUP_FAILED:
    return ("ICP_LOOKUP_FAILED");

    //////////////////////////////
    //  CongestionControl Events
    //////////////////////////////
  case CONGESTION_EVENT_CONGESTED_ON_F:
    return ("CONGESTION_EVENT_CONGESTED_ON_F");
  case CONGESTION_EVENT_CONGESTED_ON_M:
    return ("CONGESTION_EVENT_CONGESTED_ON_M");

    //////////////////////////////
    //  Plugin Events
    //////////////////////////////
  case HTTP_API_CONTINUE:
    return ("HTTP_API_CONTINUE");
  case HTTP_API_ERROR:
    return ("HTTP_API_ERROR");

    ///////////////////////////////
    //  Scheduled Update Events
    ///////////////////////////////
  case HTTP_SCH_UPDATE_EVENT_WRITTEN:
    return "HTTP_SCH_UPDATE_EVENT_WRITTEN";
  case HTTP_SCH_UPDATE_EVENT_UPDATED:
    return "HTTP_SCH_UPDATE_EVENT_UPDATED";
  case HTTP_SCH_UPDATE_EVENT_DELETED:
    return "HTTP_SCH_UPDATE_EVENT_DELETED";
  case HTTP_SCH_UPDATE_EVENT_NOT_CACHED:
    return "HTTP_SCH_UPDATE_EVENT_NOT_CACHED";
  case HTTP_SCH_UPDATE_EVENT_ERROR:
    return "HTTP_SCH_UPDATE_EVENT_ERROR";
  case HTTP_SCH_UPDATE_EVENT_NO_ACTION:
    return "HTTP_SCH_UPDATE_EVENT_NO_ACTION";

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
  case HttpTransact::STATE_MACHINE_ACTION_UNDEFINED:
    return ("STATE_UNDEFINED");

  case HttpTransact::CACHE_ISSUE_WRITE:
    return ("CACHE_ISSUE_WRITE");

  case HttpTransact::CACHE_ISSUE_WRITE_TRANSFORM:
    return ("CACHE_ISSUE_WRITE_TRANSFORM");

  case HttpTransact::CACHE_LOOKUP:
    return ("CACHE_LOOKUP");

  case HttpTransact::DNS_LOOKUP:
    return ("DNS_LOOKUP");

  case HttpTransact::REVERSE_DNS_LOOKUP:
    return ("REVERSE_DNS_LOOKUP");

  case HttpTransact::ICP_QUERY:
    return ("ICP_QUERY");

  case HttpTransact::ISSUE_CACHE_DELETE:
    return ("ISSUE_CACHE_DELETE");

  case HttpTransact::PREPARE_CACHE_UPDATE:
    return ("PREPARE_CACHE_UPDATE");

  case HttpTransact::ISSUE_CACHE_UPDATE:
    return ("ISSUE_CACHE_UPDATE");

  case HttpTransact::ORIGIN_SERVER_OPEN:
    return ("ORIGIN_SERVER_OPEN");

  case HttpTransact::ORIGIN_SERVER_RAW_OPEN:
    return ("ORIGIN_SERVER_RAW_OPEN");

  case HttpTransact::OS_RR_MARK_DOWN:
    return ("OS_RR_MARK_DOWN");

  case HttpTransact::READ_PUSH_HDR:
    return ("READ_PUSH_HDR");

  case HttpTransact::STORE_PUSH_BODY:
    return ("STORE_PUSH_BODY");

  case HttpTransact::PROXY_INTERNAL_CACHE_WRITE:
    return ("PROXY_INTERNAL_CACHE_WRITE");

  case HttpTransact::PROXY_INTERNAL_CACHE_DELETE:
    return ("PROXY_INTERNAL_CACHE_DELETE");

  case HttpTransact::PROXY_INTERNAL_CACHE_NOOP:
    return ("PROXY_INTERNAL_CACHE_NOOP");

  case HttpTransact::PROXY_INTERNAL_CACHE_UPDATE_HEADERS:
    return ("PROXY_INTERNAL_CACHE_UPDATE_HEADERS");

  case HttpTransact::PROXY_INTERNAL_REQUEST:
    return ("PROXY_INTERNAL_REQUEST");

  case HttpTransact::PROXY_SEND_ERROR_CACHE_NOOP:
    return ("PROXY_SEND_ERROR_CACHE_NOOP");

  case HttpTransact::SERVE_FROM_CACHE:
    return ("SERVE_FROM_CACHE");

  case HttpTransact::SERVER_READ:
    return ("SERVER_READ");

  case HttpTransact::SSL_TUNNEL:
    return ("SSL_TUNNEL");

  case HttpTransact::CONTINUE:
    return ("CONTINUE");

  case HttpTransact::HTTP_API_READ_REQUEST_HDR:
    return ("API_READ_REQUEST_HDR");

  case HttpTransact::HTTP_API_OS_DNS:
    return ("API_OS_DNS");

  case HttpTransact::HTTP_API_SEND_REQUEST_HDR:
    return ("API_SEND_REQUEST_HDR");

  case HttpTransact::HTTP_API_READ_CACHE_HDR:
    return ("API_READ_CACHE_HDR");

  case HttpTransact::HTTP_API_CACHE_LOOKUP_COMPLETE:
    return ("API_CACHE_LOOKUP_COMPLETE");

  case HttpTransact::HTTP_API_READ_RESPONSE_HDR:
    return ("API_READ_RESPONSE_HDR");

  case HttpTransact::HTTP_API_SEND_RESPONSE_HDR:
    return ("API_SEND_RESPONSE_HDR");

  case HttpTransact::PROXY_INTERNAL_100_RESPONSE:
    return ("PROXY_INTERNAL_100_RESPONSE");

  case HttpTransact::SERVER_PARSE_NEXT_HDR:
    return ("SERVER_PARSE_NEXT_HDR");

  case HttpTransact::TRANSFORM_READ:
    return ("TRANSFORM_READ");

#ifdef PROXY_DRAIN
  case HttpTransact::PROXY_DRAIN_REQUEST_BODY:
    return ("PROXY_DRAIN_REQUEST_BODY");
#endif /* PROXY_DRAIN */

  //case HttpTransact::AUTH_LOOKUP:
  //  return ("AUTH_LOOKUP");
  case HttpTransact::SEND_QUERY_TO_INCOMING_ROUTER:
    return ("SEND_QUERY_TO_INCOMING_ROUTER");
  case HttpTransact::EXTENSION_METHOD_TUNNEL:
    return ("EXTENSION_METHOD_TUNNEL");
  case HttpTransact::HTTP_API_SM_START:
    return ("HTTP_API_SM_START");
  case HttpTransact::REDIRECT_READ:
    return ("REDIRECT_READ");
  case HttpTransact::HTTP_API_SM_SHUTDOWN:
    return ("HTTP_API_SM_SHUTDOWN");
  case HttpTransact::HTTP_REMAP_REQUEST:
    return ("HTTP_REMAP_REQUEST");
  case HttpTransact::HTTP_API_PRE_REMAP:
    return ("HTTP_API_PRE_REMAP");
  case HttpTransact::HTTP_API_POST_REMAP:
    return ("HTTP_API_POST_REMAP");
  case HttpTransact::HTTP_POST_REMAP_SKIP:
    return ("HTTP_POST_REMAP_SKIP");
  case HttpTransact::HTTP_POST_REMAP_UPGRADE:
    return ("HTTP_POST_REMAP_UPGRADE");
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
  case HttpTransact::CACHE_DO_UNDEFINED:
    return ("CACHE_DO_UNDEFINED");
  case HttpTransact::CACHE_DO_NO_ACTION:
    return ("CACHE_DO_NO_ACTION");
  case HttpTransact::CACHE_DO_DELETE:
    return ("CACHE_DO_DELETE");
  case HttpTransact::CACHE_DO_LOOKUP:
    return ("CACHE_DO_LOOKUP");
  case HttpTransact::CACHE_DO_REPLACE:
    return ("CACHE_DO_REPLACE");
  case HttpTransact::CACHE_DO_SERVE:
    return ("CACHE_DO_SERVE");
  case HttpTransact::CACHE_DO_SERVE_AND_DELETE:
    return ("CACHE_DO_SERVE_AND_DELETE");
  case HttpTransact::CACHE_DO_SERVE_AND_UPDATE:
    return ("CACHE_DO_SERVE_AND_UPDATE");
  case HttpTransact::CACHE_DO_UPDATE:
    return ("CACHE_DO_UPDATE");
  case HttpTransact::CACHE_DO_WRITE:
    return ("CACHE_DO_WRITE");
  case HttpTransact::CACHE_PREPARE_TO_DELETE:
    return ("CACHE_PREPARE_TO_DELETE");
  case HttpTransact::CACHE_PREPARE_TO_UPDATE:
    return ("CACHE_PREPARE_TO_UPDATE");
  case HttpTransact::CACHE_PREPARE_TO_WRITE:
    return ("CACHE_PREPARE_TO_WRITE");
  case HttpTransact::TOTAL_CACHE_ACTION_TYPES:
    return ("TOTAL_CACHE_ACTION_TYPES");
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
  case TS_HTTP_LAST_HOOK:
    return "TS_HTTP_LAST_HOOK";
  }

  return "unknown hook";
}
