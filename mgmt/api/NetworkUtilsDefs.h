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
 * NetworkUtilsDefs.h
 *
 * contains general definitions used by both NetworkUtilsRemote and
 * NetworkUtilsLocal
 *
 *
 ***************************************************************************/

#ifndef _NETWORK_UTILS_DEFS_H_
#define _NETWORK_UTILS_DEFS_H_

#define REMOTE_DELIM ':'
#define REMOTE_DELIM_STR ":"

#define MAX_CONN_TRIES 10       // maximum number of attemps to reconnect to TM
#define MAX_TIME_WAIT  60       // num secs for a timeout on a select call (remote only)

// measure in bytes used in construcing network messages
#define SIZE_OP_T     2         // num bytes used to specify OpType
#define SIZE_FILE_T   2         // num bytes used to specify INKFileNameT
#define SIZE_LEN      4         // max num bytes used to specify length of anything
#define SIZE_ERR_T    2         // num bytes used to specify INKError return value
#define SIZE_VER      2         // num bytes used to specify file version
#define SIZE_REC_T    2         // num bytes used to specify INKRecordT
#define SIZE_PROXY_T  2         // num bytes used to specify INKProxyStateT
#define SIZE_TS_ARG_T 2         // num bytes used to specify INKCacheClearT
#define SIZE_DIAGS_T  2         // num bytes used to specify INKDiagsT
#define SIZE_BOOL     2
#define SIZE_ACTION_T 2         // num bytes used to specify INKActionNeedT
#define SIZE_EVENT_ID 2         // num bytes used to specify event_id


// the possible operations or msg types sent from remote client to TM
typedef enum
{
  FILE_READ,
  FILE_WRITE,
  RECORD_SET,
  RECORD_GET,
  PROXY_STATE_GET,
  PROXY_STATE_SET,
  RECONFIGURE,
  RESTART,
  BOUNCE,
  EVENT_RESOLVE,
  EVENT_GET_MLT,
  EVENT_ACTIVE,
  EVENT_REG_CALLBACK,
  EVENT_UNREG_CALLBACK,
  EVENT_NOTIFY,                 /* only msg sent from TM to client */
  SNAPSHOT_TAKE,
  SNAPSHOT_RESTORE,
  SNAPSHOT_REMOVE,
  SNAPSHOT_GET_MLT,
  DIAGS,
  STATS_RESET_NODE,
  STATS_RESET_CLUSTER,
  STORAGE_DEVICE_CMD_OFFLINE,
  RECORD_MATCH_GET,
  UNDEFINED_OP /* This must be last */
} OpType;

#endif
