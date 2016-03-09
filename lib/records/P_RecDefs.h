/** @file

  Private record declarations

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

#ifndef _P_REC_DEFS_H_
#define _P_REC_DEFS_H_

#include "I_RecDefs.h"

#define REC_CONFIG_FILE "records.config"
#define REC_SHADOW_EXT ".shadow"
#define REC_RAW_STATS_FILE "records.snap"

#define REC_MESSAGE_ELE_MAGIC 0xF00DF00D

// This is for the internal stats and configs, as well as API stats. We currently use
// about 1600 stats + configs for the core, but we're allocating 2000 for some growth.
// TODO: if/when we switch to a new config system, we should make this run-time dynamic.
#define REC_MAX_RECORDS (2000 + TS_MAX_API_STATS)

#define REC_CONFIG_UPDATE_INTERVAL_MS 3000
#define REC_REMOTE_SYNC_INTERVAL_MS 5000

#define REC_RAW_STAT_SYNC_INTERVAL_MS 5000
#define REC_STAT_UPDATE_INTERVAL_MS 10000

//-------------------------------------------------------------------------
// Record Items
//-------------------------------------------------------------------------

#define REC_LOCAL_UPDATE_REQUIRED 1
#define REC_PROCESS_UPDATE_REQUIRED (REC_LOCAL_UPDATE_REQUIRED << 1)
#define REC_UPDATE_REQUIRED (REC_LOCAL_UPDATE_REQUIRED | REC_PROCESS_UPDATE_REQUIRED)

#define REC_DISK_SYNC_REQUIRED 1
#define REC_PEER_SYNC_REQUIRED (REC_DISK_SYNC_REQUIRED << 1)
#define REC_INC_CONFIG_VERSION (REC_PEER_SYNC_REQUIRED << 1)
#define REC_SYNC_REQUIRED (REC_DISK_SYNC_REQUIRED | REC_PEER_SYNC_REQUIRED)

enum RecEntryT {
  RECE_NULL,
  RECE_COMMENT,
  RECE_RECORD,
};

struct RecConfigFileEntry {
  RecEntryT entry_type;
  char *entry;
};

typedef struct RecConfigCbList_t {
  RecConfigUpdateCb update_cb;
  void *update_cookie;
  struct RecConfigCbList_t *next;
} RecConfigUpdateCbList;

typedef struct RecStatUpdateFuncList_t {
  RecRawStatBlock *rsb;
  int id;
  RecStatUpdateFunc update_func;
  void *update_cookie;
  struct RecStatUpdateFuncList_t *next;
} RecStatUpdateFuncList;

struct RecStatMeta {
  RecRawStat data_raw;
  RecRawStatSyncCb sync_cb;
  RecRawStatBlock *sync_rsb;
  int sync_id;
  RecPersistT persist_type;
};

struct RecConfigMeta {
  unsigned char update_required;
  RecConfigUpdateCbList *update_cb_list;
  void *update_cookie;
  RecUpdateT update_type;
  RecCheckT check_type;
  char *check_expr;
  RecAccessT access_type;
  RecSourceT source; ///< Source of the configuration value.
};

struct RecRecord {
  RecT rec_type;
  const char *name;
  RecDataT data_type;
  RecData data;
  RecData data_default;
  RecMutex lock;
  unsigned char sync_required;
  uint32_t version;
  bool registered;
  union {
    RecStatMeta stat_meta;
    RecConfigMeta config_meta;
  };
  int order;
  int rsb_id;
};

// Used for cluster. TODO: Do we still need this?
struct RecRecords {
  int num_recs;
  RecRecord *recs;
};

//-------------------------------------------------------------------------
// Message Items
//-------------------------------------------------------------------------

enum RecMessageT {
  RECG_NULL,
  RECG_SET,
  RECG_REGISTER,
  RECG_PUSH,
  RECG_PULL_REQ,
  RECG_PULL_ACK,
  RECG_RESET,
};

struct RecMessageHdr {
  RecMessageT msg_type;
  int o_start;
  int o_write;
  int o_end;
  int entries;
  int alignment; // needs to be 8 byte aligned
};

struct RecMessageEleHdr {
  unsigned int magic;
  int o_next;
};

struct RecMessageItr {
  RecMessageEleHdr *ele_hdr;
  int next;
};

typedef RecMessageHdr RecMessage;

typedef void (*RecDumpEntryCb)(RecT rec_type, void *edata, int registered, const char *name, int data_type, RecData *datum);

typedef int (*RecMessageRecvCb)(RecMessage *msg, RecMessageT msg_type, void *cookie);

#endif
