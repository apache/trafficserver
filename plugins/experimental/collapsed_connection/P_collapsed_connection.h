/** @file

  Implements Collapsed connection

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

#include <cstdint>
#include <map>
#include <list>
#include <utility>

#pragma once

#define PLUGIN_NAME "collapsed_connection"
#define PLUGIN_VENDOR "Apache Software Foundation"
#define PLUGIN_SUPPORT "dev@trafficserver.apache.org"

#define DEFAULT_INSERT_LOCK_RETRY_TIME 10
#define DEFAULT_MAX_LOCK_RETRY_TIMEOUT 2000
#define DEFAULT_KEEP_PASS_RECORD_TIME 5000

typedef enum {
  CcEnabled,
  CcRequiredHeader,
  CcInsertLockRetryTime,
  CcMaxLockRetryTimeout,
  CcKeepPassRecordTime,
} CcConfigKey;

typedef enum {
  CC_NONE,
  CC_LOCKED,
  CC_INSERT,
  CC_PASS,
  CC_PASSED,
  CC_REMOVE,
  CC_DONE,
} CcTxnState;

struct PassRecord {
  int64_t timeout;
  uint32_t hash_key;
};

typedef std::map<uint32_t, int8_t> UintMap;
typedef std::list<PassRecord> UsecList;

typedef struct {
  bool enabled;
  TSMgmtString required_header;
  int required_header_len;
  TSMgmtInt insert_lock_retry_time;
  TSMgmtInt max_lock_retry_timeout;
  TSMgmtInt keep_pass_record_time;
} CcPluginConfig;

typedef struct {
  UintMap *active_hash_map;
  TSMutex mutex;
  uint64_t seq_id;
  int txn_slot;
  CcPluginConfig *global_config;
  UsecList *keep_pass_list;
  TSHRTime last_gc_time;
  bool read_while_writer;
  int tol_global_hook_reqs;
  int tol_remap_hook_reqs;
  int tol_collapsed_reqs;
  int tol_non_cacheable_reqs;
  int tol_got_passed_reqs;
  int cur_hash_entries;
  int cur_keep_pass_entries;
  int max_hash_entries;
  int max_keep_pass_entries;
} CcPluginData;

typedef struct {
  uint64_t seq_id;
  TSHttpTxn txnp;
  TSCont contp;
  CcPluginConfig *config;
  uint32_t hash_key;
  CcTxnState cc_state;
  TSHRTime wait_time;
} CcTxnData;

typedef struct {
  TSEvent event;
  CcTxnData *txn_data;
} TryLockData;

// hash seed for MurmurHash3_x86_32, must be a prime number
const unsigned int c_hashSeed = 27240313;

static CcPluginData *getCcPlugin();
static int collapsedConnectionMainHandler(TSCont contp, TSEvent event, void *edata);
