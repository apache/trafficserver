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

#include "ts/ink_config.h"
#include <stdio.h>
#include <string.h>

#include "P_EventSystem.h"
// #include "I_Disk.h" unused
#include "I_Cache.h"
#include "I_Net.h"
//#include "P_Cluster.h"
#include "I_HostDB.h"
#include "BaseManager.h"
#include "P_MultiCache.h"

/*-------------------------------------------------------------------------
  event_int_to_string

  This routine will translate an integer event number to a string
  identifier based on a brute-force search of a switch tag.  If the event
  cannot be located in the switch table, the routine will construct and
  return a string of the integer identifier.
  -------------------------------------------------------------------------*/

const char *
event_int_to_string(int event, int blen, char *buffer)
{
  switch (event) {
  case -1:
    return "<no event>";
  case VC_EVENT_READ_READY:
    return "VC_EVENT_READ_READY";
  case VC_EVENT_WRITE_READY:
    return "VC_EVENT_WRITE_READY";
  case VC_EVENT_READ_COMPLETE:
    return "VC_EVENT_READ_COMPLETE";
  case VC_EVENT_WRITE_COMPLETE:
    return "VC_EVENT_WRITE_COMPLETE";
  case VC_EVENT_EOS:
    return "VC_EVENT_EOS";
  case VC_EVENT_INACTIVITY_TIMEOUT:
    return "VC_EVENT_INACTIVITY_TIMEOUT";
  case VC_EVENT_ACTIVE_TIMEOUT:
    return "VC_EVENT_ACTIVE_TIMEOUT";

  case NET_EVENT_OPEN:
    return "NET_EVENT_OPEN";
  case NET_EVENT_OPEN_FAILED:
    return "NET_EVENT_OPEN_FAILED";
  case NET_EVENT_ACCEPT:
    return "NET_EVENT_ACCEPT";
  case NET_EVENT_ACCEPT_SUCCEED:
    return "NET_EVENT_ACCEPT_SUCCEED";
  case NET_EVENT_ACCEPT_FAILED:
    return "NET_EVENT_ACCEPT_FAILED";

#ifdef CLUSTER_CACHE
  case CLUSTER_EVENT_CHANGE:
    return "CLUSTER_EVENT_CHANGE";
  case CLUSTER_EVENT_CONFIGURATION:
    return "CLUSTER_EVENT_CONFIGURATION";
  case CLUSTER_EVENT_OPEN:
    return "CLUSTER_EVENT_OPEN";
  case CLUSTER_EVENT_OPEN_FAILED:
    return "CLUSTER_EVENT_OPEN_FAILED";
  case CLUSTER_EVENT_STEAL_THREAD:
    return "CLUSTER_EVENT_STEAL_THREAD";
#endif
  case EVENT_HOST_DB_LOOKUP:
    return "EVENT_HOST_DB_LOOKUP";
  case EVENT_HOST_DB_GET_RESPONSE:
    return "EVENT_HOST_DB_GET_RESPONSE";

  case DNS_EVENT_EVENTS_START:
    return "DNS_EVENT_EVENTS_START";

  case MULTI_CACHE_EVENT_SYNC:
    return "MULTI_CACHE_EVENT_SYNC";

  case CACHE_EVENT_LOOKUP:
    return "CACHE_EVENT_LOOKUP";
  case CACHE_EVENT_LOOKUP_FAILED:
    return "CACHE_EVENT_LOOKUP_FAILED";
  case CACHE_EVENT_OPEN_READ:
    return "CACHE_EVENT_OPEN_READ";
  case CACHE_EVENT_OPEN_READ_FAILED:
    return "CACHE_EVENT_OPEN_READ_FAILED";
  case CACHE_EVENT_OPEN_WRITE:
    return "CACHE_EVENT_OPEN_WRITE";
  case CACHE_EVENT_OPEN_WRITE_FAILED:
    return "CACHE_EVENT_OPEN_WRITE_FAILED";
  case CACHE_EVENT_REMOVE:
    return "CACHE_EVENT_REMOVE";
  case CACHE_EVENT_REMOVE_FAILED:
    return "CACHE_EVENT_REMOVE_FAILED";
  case CACHE_EVENT_UPDATE:
    return "CACHE_EVENT_UPDATE";
  case CACHE_EVENT_UPDATE_FAILED:
    return "CACHE_EVENT_UPDATE_FAILED";
  case CACHE_EVENT_LINK:
    return "CACHE_EVENT_LINK";
  case CACHE_EVENT_LINK_FAILED:
    return "CACHE_EVENT_LINK_FAILED";
  case CACHE_EVENT_DEREF:
    return "CACHE_EVENT_DEREF";
  case CACHE_EVENT_DEREF_FAILED:
    return "CACHE_EVENT_DEREF_FAILED";
  case CACHE_EVENT_RESPONSE:
    return "CACHE_EVENT_RESPONSE";
  case CACHE_EVENT_RESPONSE_MSG:
    return "CACHE_EVENT_RESPONSE_MSG";

  case MGMT_EVENT_SHUTDOWN:
    return "MGMT_EVENT_SHUTDOWN";
  case MGMT_EVENT_RESTART:
    return "MGMT_EVENT_RESTART";
  case MGMT_EVENT_BOUNCE:
    return "MGMT_EVENT_BOUNCE";
  case MGMT_EVENT_CONFIG_FILE_UPDATE:
    return "MGMT_EVENT_CONFIG_FILE_UPDATE";
  case MGMT_EVENT_CONFIG_FILE_UPDATE_NO_INC_VERSION:
    return "MGMT_EVENT_CONFIG_FILE_UPDATE_NO_INC_VERSION";
  case MGMT_EVENT_CLEAR_STATS:
    return "MGMT_EVENT_CLEAR_STATS";

  default:
    if (buffer != NULL) {
      snprintf(buffer, blen, "%d", event);
      return buffer;
    } else {
      return "UNKNOWN_EVENT";
    }
  }
}
