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



#include <stdio.h>
#include <string.h>

/*------------------------------------------------------------------------- 
  event_int_to_string

  This routine will translate an integer event number to a string
  identifier based on a brute-force search of a switch tag.  If the event
  cannot be located in the switch table, the routine will construct and
  return a string of the integer identifier.
  -------------------------------------------------------------------------*/

char *
event_int_to_string(int event, char buffer[32])
{
  switch (event) {
  case -1:
    return "<no event>";
  case 100:
    return "VC_EVENT_READ_READY";
  case 101:
    return "VC_EVENT_WRITE_READY";
  case 102:
    return "VC_EVENT_READ_COMPLETE";
  case 103:
    return "VC_EVENT_WRITE_COMPLETE";
  case 104:
    return "VC_EVENT_EOS";
  case 105:
    return "VC_EVENT_INACTIVITY_TIMEOUT";
  case 106:
    return "VC_EVENT_ACTIVE_TIMEOUT";

  case 200:
    return "NET_EVENT_OPEN";
  case 201:
    return "NET_EVENT_OPEN_FAILED";
  case 202:
    return "NET_EVENT_ACCEPT";
  case 203:
    return "NET_EVENT_ACCEPT_SUCCEED";
  case 204:
    return "NET_EVENT_ACCEPT_FAILED";

  case 300:
    return "DISK_EVENT_OPEN";
  case 301:
    return "DISK_EVENT_OPEN_FAILED";
  case 304:
    return "DISK_EVENT_CLOSE_COMPLETE";
  case 305:
    return "DISK_EVENT_STAT_COMPLETE";
  case 306:
    return "DISK_EVENT_SEEK_COMPLETE";

  case 400:
    return "CLUSTER_EVENT_CHANGE";
  case 401:
    return "CLUSTER_EVENT_CONFIGURATION";
  case 402:
    return "CLUSTER_EVENT_OPEN";
  case 403:
    return "CLUSTER_EVENT_OPEN_FAILED";
  case 450:
    return "CLUSTER_EVENT_STEAL_THREAD";

  case 500:
    return "EVENT_HOST_DB_LOOKUP";
  case 501:
    return "EVENT_HOST_DB_GET_RESPONSE";

  case 600:
    return "DNS_EVENT_EVENTS_START";

  case 700:
    return "FTP_EVENT_OPEN";
  case 701:
    return "FTP_EVENT_ACCEPT";
  case 702:
    return "FTP_EVENT_OPEN_FAILED";

  case 800:
    return "MANAGEMENT_EVENT";

  case 900:
    return "LOGIO_FINISHED";
  case 901:
    return "LOGIO_WRITE";
  case 902:
    return "LOGIO_COUNTEDWRITE";
  case 903:
    return "LOGIO_HAVENETIO";
  case 904:
    return "LOGIO_RECONFIG";
  case 905:
    return "LOGIO_RECONFIG_FILE";
  case 906:
    return "LOGIO_RECONFIG_FILEREAD";
  case 907:
    return "LOGIO_PULSE";
  case 908:
    return "LOGIO_STARTUP";

  case 1000:
    return "MULTI_CACHE_EVENT_SYNC";

  case 1100:
    return "CACHE_EVENT_LOOKUP";
  case 1101:
    return "CACHE_EVENT_LOOKUP_FAILED";
  case 1102:
    return "CACHE_EVENT_OPEN_READ";
  case 1103:
    return "CACHE_EVENT_OPEN_READ_FAILED";
  case 1104:
    return "CACHE_EVENT_OPEN_READ_DUMMY";
  case 1105:
    return "CACHE_EVENT_OPEN_READ_FAILED_IN_PROGRESS";
  case 1106:
    return "CACHE_EVENT_OPEN_READ_VIO";
  case 1107:
    return "CACHE_EVENT_OPEN_READ_VIO_XXX";
  case 1108:
    return "CACHE_EVENT_OPEN_WRITE";
  case 1109:
    return "CACHE_EVENT_OPEN_WRITE_FAILED";
  case 1110:
    return "CACHE_EVENT_OPEN_WRITE_VIO";
  case 1111:
    return "CACHE_EVENT_OPEN_WRITE_VIO_XXX";
  case 1112:
    return "CACHE_EVENT_REMOVE";
  case 1113:
    return "CACHE_EVENT_REMOVE_FAILED";
  case 1114:
    return "CACHE_EVENT_UPDATE";
  case 1115:
    return "CACHE_EVENT_UPDATE_FAILED";
  case 1116:
    return "CACHE_EVENT_LINK";
  case 1117:
    return "CACHE_EVENT_LINK_FAILED";
  case 1118:
    return "CACHE_EVENT_DEREF";
  case 1119:
    return "CACHE_EVENT_DEREF_FAILED";
  case 1150:
    return "CACHE_EVENT_RESPONSE";
  case 1151:
    return "CACHE_EVENT_RESPONSE_MSG";

  case 1300:
    return "CACHE_DB_EVENT_POOL_SYNC";
  case 1301:
    return "CACHE_DB_EVENT_ITERATE_VECVEC";
  case 1302:
    return "CACHE_DB_EVENT_ITERATE_FRAG_HDR";
  case 1303:
    return "CACHE_DB_EVENT_ITERATE_DONE";

  case 1500:
    return "HTTP_EVENT_CONNECTION_OPEN";
  case 1501:
    return "HTTP_EVENT_CONNECTION_OPEN_ERROR";
  case 1502:
    return "HTTP_EVENT_READ_HEADER_COMPLETE";
  case 1503:
    return "HTTP_EVENT_READ_HEADER_ERROR";
  case 1504:
    return "HTTP_EVENT_READ_BODY_READY";
  case 1505:
    return "HTTP_EVENT_READ_BODY_COMPLETE";
  case 1506:
    return "HTTP_EVENT_WRITE_READY";
  case 1507:
    return "HTTP_EVENT_WRITE_COMPLETE";
  case 1508:
    return "HTTP_EVENT_EOS";
  case 1509:
    return "HTTP_EVENT_CLOSED";

  case 1700:
    return "NNTP_EVENT_CMD";
  case 1701:
    return "NNTP_EVENT_CALL";
  case 1702:
    return "NNTP_EVENT_CALL_DONE";
  case 1703:
    return "NNTP_EVENT_ACQUIRE";
  case 1704:
    return "NNTP_EVENT_ACQUIRE_FAILED";
  case 1705:
    return "NNTP_EVENT_SLAVE_RESPONSE";
  case 1706:
    return "NNTP_EVENT_SLAVE_INITIAL_ERROR";
  case 1707:
    return "NNTP_EVENT_SLAVE_ERROR";
  case 1708:
    return "NNTP_EVENT_SLAVE_DONE";
  case 1709:
    return "NNTP_EVENT_TUNNEL_DONE";
  case 1710:
    return "NNTP_EVENT_TUNNEL_ERROR";
  case 1711:
    return "NNTP_EVENT_TUNNEL_CONT";
  case 1712:
    return "NNTP_EVENT_CLUSTER_MSG";

  case 10000:
    return "MGMT_EVENT_SHUTDOWN";
  case 10001:
    return "MGMT_EVENT_RESTART";
  case 10002:
    return "MGMT_EVENT_BOUNCE";
  case 10003:
    return "MGMT_EVENT_CONFIG_FILE_UPDATE";
  case 10004:
    return "MGMT_EVENT_CLEAR_STATS";

  default:
    if (buffer != NULL) {
      snprintf(buffer, sizeof(buffer), "%d", event);
      return buffer;
    } else {
      return "UNKNOWN_EVENT";
    }
  }
}
