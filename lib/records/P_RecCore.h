/** @file

  Private record core declarations

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

#pragma once

#include "tscore/ink_thread.h"
#include "tscore/ink_llqueue.h"
#include "tscore/ink_rwlock.h"
#include "tscore/TextBuffer.h"

#include "I_RecCore.h"
#include "P_RecDefs.h"
#include "P_RecUtils.h"

#include <unordered_set>
#include <unordered_map>

// records, record hash-table, and hash-table rwlock
extern RecRecord *g_records;
extern std::unordered_map<std::string, RecRecord *> g_records_ht;
extern ink_rwlock g_records_rwlock;
extern int g_num_records;
extern RecModeT g_mode_type;

// records.config items
extern const char *g_rec_config_fpath;
extern LLQ *g_rec_config_contents_llq;
extern std::unordered_set<std::string> g_rec_config_contents_ht;
extern ink_mutex g_rec_config_lock;

//-------------------------------------------------------------------------
// Initialization
//-------------------------------------------------------------------------

int RecCoreInit(RecModeT mode_type, Diags *diags);

//-------------------------------------------------------------------------
// Registration/Insertion
//-------------------------------------------------------------------------

RecRecord *RecRegisterStat(RecT rec_type, const char *name, RecDataT data_type, RecData data_default, RecPersistT persist_type);

RecRecord *RecRegisterConfig(RecT rec_type, const char *name, RecDataT data_type, RecData data_default, RecUpdateT update_type,
                             RecCheckT check_type, const char *check_regex, RecSourceT source, RecAccessT access_type = RECA_NULL);

RecRecord *RecForceInsert(RecRecord *record);

//-------------------------------------------------------------------------
// Setting/Getting
//-------------------------------------------------------------------------

RecErrT RecSetRecord(RecT rec_type, const char *name, RecDataT data_type, RecData *data, RecRawStat *raw_stat, RecSourceT source,
                     bool lock = true, bool inc_version = true);

RecErrT RecGetRecord_Xmalloc(const char *name, RecDataT data_type, RecData *data, bool lock = true);

//-------------------------------------------------------------------------
// Read/Sync to Disk
//-------------------------------------------------------------------------

RecErrT RecReadStatsFile();
RecErrT RecSyncStatsFile();
RecErrT RecReadConfigFile(bool inc_version);
RecErrT RecWriteConfigFile(TextBuffer *tb);
RecErrT RecSyncConfigToTB(TextBuffer *tb, bool *inc_version = nullptr);

//-------------------------------------------------------------------------
// Misc
//-------------------------------------------------------------------------

bool i_am_the_record_owner(RecT rec_type);
RecErrT send_push_message();
RecErrT send_pull_message(RecMessageT msg_type);
RecErrT send_register_message(RecRecord *record);
RecErrT recv_message_cb(RecMessage *msg, RecMessageT msg_type, void *cookie);
RecUpdateT RecExecConfigUpdateCbs(unsigned int update_required_type);

void RecDumpRecordsHt(RecT rec_type = RECT_NULL);

void RecDumpRecords(RecT rec_type, RecDumpEntryCb callback, void *edata);
