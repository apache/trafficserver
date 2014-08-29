/** @file

  Private record core definitions

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

#include "TextBuffer.h"
#include "Tokenizer.h"
#include "ink_defs.h"
#include "ink_string.h"

#include "P_RecFile.h"
#include "P_RecUtils.h"
#include "P_RecMessage.h"
#include "P_RecCore.h"

RecModeT g_mode_type = RECM_NULL;


//-------------------------------------------------------------------------
// send_reset_message
//-------------------------------------------------------------------------
static int
send_reset_message(RecRecord * record)
{
  RecMessage *m;

  rec_mutex_acquire(&(record->lock));
  m = RecMessageAlloc(RECG_RESET);
  m = RecMessageMarshal_Realloc(m, record);
  RecDebug(DL_Note, "[send] RECG_RESET [%d bytes]", sizeof(RecMessageHdr) + m->o_write - m->o_start);
  RecMessageSend(m);
  RecMessageFree(m);
  rec_mutex_release(&(record->lock));

  return REC_ERR_OKAY;
}


//-------------------------------------------------------------------------
// send_set_message
//-------------------------------------------------------------------------
static int
send_set_message(RecRecord * record)
{
  RecMessage *m;

  rec_mutex_acquire(&(record->lock));
  m = RecMessageAlloc(RECG_SET);
  m = RecMessageMarshal_Realloc(m, record);
  RecDebug(DL_Note, "[send] RECG_SET [%d bytes]", sizeof(RecMessageHdr) + m->o_write - m->o_start);
  RecMessageSend(m);
  RecMessageFree(m);
  rec_mutex_release(&(record->lock));

  return REC_ERR_OKAY;
}


//-------------------------------------------------------------------------
// send_register_message
//-------------------------------------------------------------------------
int
send_register_message(RecRecord * record)
{
  RecMessage *m;

  rec_mutex_acquire(&(record->lock));
  m = RecMessageAlloc(RECG_REGISTER);
  m = RecMessageMarshal_Realloc(m, record);
  RecDebug(DL_Note, "[send] RECG_REGISTER [%d bytes]", sizeof(RecMessageHdr) + m->o_write - m->o_start);
  RecMessageSend(m);
  RecMessageFree(m);
  rec_mutex_release(&(record->lock));

  return REC_ERR_OKAY;
}


//-------------------------------------------------------------------------
// send_push_message
//-------------------------------------------------------------------------
int
send_push_message()
{
  RecRecord *r;
  RecMessage *m;
  int i, num_records;
  bool send_msg = false;

  m = RecMessageAlloc(RECG_PUSH);
  num_records = g_num_records;
  for (i = 0; i < num_records; i++) {
    r = &(g_records[i]);
    rec_mutex_acquire(&(r->lock));
    if (i_am_the_record_owner(r->rec_type)) {
      if (r->sync_required & REC_PEER_SYNC_REQUIRED) {
        m = RecMessageMarshal_Realloc(m, r);
        r->sync_required &= ~REC_PEER_SYNC_REQUIRED;
        send_msg = true;
      }
    }
    rec_mutex_release(&(r->lock));
  }
  if (send_msg) {
    RecDebug(DL_Note, "[send] RECG_PUSH [%d bytes]", sizeof(RecMessageHdr) + m->o_write - m->o_start);
    RecMessageSend(m);
  }
  RecMessageFree(m);

  return REC_ERR_OKAY;
}


//-------------------------------------------------------------------------
// send_pull_message
//-------------------------------------------------------------------------
int
send_pull_message(RecMessageT msg_type)
{
  RecRecord *r;
  RecMessage *m;
  int i, num_records;

  m = RecMessageAlloc(msg_type);
  switch (msg_type) {

  case RECG_PULL_REQ:
    // We're requesting all of the records from our peer.  No payload
    // here, just send the message.
    RecDebug(DL_Note, "[send] RECG_PULL_REQ [%d bytes]", sizeof(RecMessageHdr) + m->o_write - m->o_start);
    break;

  case RECG_PULL_ACK:
    // Respond to a RECG_PULL_REQ message from our peer.  Send ALL
    // records!  Also be sure to send a response even if it has no
    // payload.  Our peer may be blocking and waiting for a response!
    num_records = g_num_records;
    for (i = 0; i < num_records; i++) {
      r = &(g_records[i]);
      if (i_am_the_record_owner(r->rec_type) ||
          (REC_TYPE_IS_STAT(r->rec_type) && !(r->registered)) ||
          (REC_TYPE_IS_STAT(r->rec_type) && (r->stat_meta.persist_type == RECP_NON_PERSISTENT))) {
        rec_mutex_acquire(&(r->lock));
        m = RecMessageMarshal_Realloc(m, r);
        r->sync_required &= ~REC_PEER_SYNC_REQUIRED;
        rec_mutex_release(&(r->lock));
      }
    }
    RecDebug(DL_Note, "[send] RECG_PULL_ACK [%d bytes]", sizeof(RecMessageHdr) + m->o_write - m->o_start);
    break;

  default:
    RecMessageFree(m);
    return REC_ERR_FAIL;

  }

  RecMessageSend(m);
  RecMessageFree(m);

  return REC_ERR_OKAY;
}


//-------------------------------------------------------------------------
// recv_message_cb
//-------------------------------------------------------------------------
int
recv_message_cb(RecMessage * msg, RecMessageT msg_type, void */* cookie */)
{
  RecRecord *r;
  RecMessageItr itr;

  switch (msg_type) {

  case RECG_SET:

    RecDebug(DL_Note, "[recv] RECG_SET [%d bytes]", sizeof(RecMessageHdr) + msg->o_end - msg->o_start);
    if (RecMessageUnmarshalFirst(msg, &itr, &r) != REC_ERR_FAIL) {
      do {
        if (REC_TYPE_IS_STAT(r->rec_type)) {
          RecSetRecord(r->rec_type, r->name, r->data_type, &(r->data), &(r->stat_meta.data_raw));
        } else {
          RecSetRecord(r->rec_type, r->name, r->data_type, &(r->data), NULL);
        }
      } while (RecMessageUnmarshalNext(msg, &itr, &r) != REC_ERR_FAIL);
    }
    break;

  case RECG_RESET:

    RecDebug(DL_Note, "[recv] RECG_RESET [%d bytes]", sizeof(RecMessageHdr) + msg->o_end - msg->o_start);
    if (RecMessageUnmarshalFirst(msg, &itr, &r) != REC_ERR_FAIL) {
      do {
        if (REC_TYPE_IS_STAT(r->rec_type)) {
          RecResetStatRecord(r->name);
        } else {
          RecSetRecord(r->rec_type, r->name, r->data_type, &(r->data), NULL);
        }
      } while (RecMessageUnmarshalNext(msg, &itr, &r) != REC_ERR_FAIL);
    }
    break;

  case RECG_REGISTER:
    RecDebug(DL_Note, "[recv] RECG_REGISTER [%d bytes]", sizeof(RecMessageHdr) + msg->o_end - msg->o_start);
    if (RecMessageUnmarshalFirst(msg, &itr, &r) != REC_ERR_FAIL) {
      do {
        if (REC_TYPE_IS_STAT(r->rec_type)) {
          RecRegisterStat(r->rec_type, r->name, r->data_type, r->data_default, r->stat_meta.persist_type);
        } else if (REC_TYPE_IS_CONFIG(r->rec_type)) {
          RecRegisterConfig(r->rec_type, r->name, r->data_type,
                            r->data_default, r->config_meta.update_type,
                            r->config_meta.check_type, r->config_meta.check_expr, r->config_meta.access_type);
        }
      } while (RecMessageUnmarshalNext(msg, &itr, &r) != REC_ERR_FAIL);
    }
    break;

  case RECG_PUSH:
    RecDebug(DL_Note, "[recv] RECG_PUSH [%d bytes]", sizeof(RecMessageHdr) + msg->o_end - msg->o_start);
    if (RecMessageUnmarshalFirst(msg, &itr, &r) != REC_ERR_FAIL) {
      do {
        RecForceInsert(r);
      } while (RecMessageUnmarshalNext(msg, &itr, &r) != REC_ERR_FAIL);
    }
    break;

  case RECG_PULL_ACK:
    RecDebug(DL_Note, "[recv] RECG_PULL_ACK [%d bytes]", sizeof(RecMessageHdr) + msg->o_end - msg->o_start);
    if (RecMessageUnmarshalFirst(msg, &itr, &r) != REC_ERR_FAIL) {
      do {
        RecForceInsert(r);
      } while (RecMessageUnmarshalNext(msg, &itr, &r) != REC_ERR_FAIL);
    }
    break;

  case RECG_PULL_REQ:
    RecDebug(DL_Note, "[recv] RECG_PULL_REQ [%d bytes]", sizeof(RecMessageHdr) + msg->o_end - msg->o_start);
    send_pull_message(RECG_PULL_ACK);
    break;

  default:
    ink_assert(!"Unexpected RecG type");
    return REC_ERR_FAIL;

  }

  return REC_ERR_OKAY;
}


//-------------------------------------------------------------------------
// RecRegisterStatXXX
//-------------------------------------------------------------------------
#define REC_REGISTER_STAT_XXX(A, B) \
  ink_assert((rec_type == RECT_NODE)    || \
                   (rec_type == RECT_CLUSTER) || \
                   (rec_type == RECT_PROCESS) || \
                   (rec_type == RECT_LOCAL)   || \
                   (rec_type == RECT_PLUGIN));   \
  RecRecord *r; \
  RecData my_data_default; \
  my_data_default.A = data_default; \
  if ((r = RecRegisterStat(rec_type, name, B, my_data_default, \
                           persist_type)) != NULL) { \
    if (i_am_the_record_owner(r->rec_type)) { \
      r->sync_required = r->sync_required | REC_PEER_SYNC_REQUIRED; \
    } else { \
      send_register_message(r); \
    } \
    return REC_ERR_OKAY; \
  } else { \
    return REC_ERR_FAIL; \
  }

int
_RecRegisterStatInt(RecT rec_type, const char *name, RecInt data_default, RecPersistT persist_type)
{
  REC_REGISTER_STAT_XXX(rec_int, RECD_INT);
}

int
_RecRegisterStatFloat(RecT rec_type, const char *name, RecFloat data_default, RecPersistT persist_type)
{
  REC_REGISTER_STAT_XXX(rec_float, RECD_FLOAT);
}

int
_RecRegisterStatString(RecT rec_type, const char *name, RecString data_default, RecPersistT persist_type)
{
  REC_REGISTER_STAT_XXX(rec_string, RECD_STRING);
}

int
_RecRegisterStatCounter(RecT rec_type, const char *name, RecCounter data_default, RecPersistT persist_type)
{
  REC_REGISTER_STAT_XXX(rec_counter, RECD_COUNTER);
}


//-------------------------------------------------------------------------
// RecRegisterConfigXXX
//-------------------------------------------------------------------------
#define REC_REGISTER_CONFIG_XXX(A, B) \
  RecRecord *r; \
  RecData my_data_default; \
  my_data_default.A = data_default; \
  if ((r = RecRegisterConfig(rec_type, name, B, my_data_default, \
                             update_type, check_type,              \
                             check_regex, access_type)) != NULL) { \
    if (i_am_the_record_owner(r->rec_type)) { \
      r->sync_required = r->sync_required | REC_PEER_SYNC_REQUIRED; \
    } else { \
      send_register_message(r); \
    } \
    return REC_ERR_OKAY; \
  } else { \
    return REC_ERR_FAIL; \
  }

int
RecRegisterConfigInt(RecT rec_type, const char *name,
                     RecInt data_default, RecUpdateT update_type,
                     RecCheckT check_type, const char *check_regex, RecAccessT access_type)
{
  ink_assert((rec_type == RECT_CONFIG) || (rec_type == RECT_LOCAL));
  REC_REGISTER_CONFIG_XXX(rec_int, RECD_INT);
}

int
RecRegisterConfigFloat(RecT rec_type, const char *name,
                       RecFloat data_default, RecUpdateT update_type,
                       RecCheckT check_type, const char *check_regex, RecAccessT access_type)
{
  ink_assert((rec_type == RECT_CONFIG) || (rec_type == RECT_LOCAL));
  REC_REGISTER_CONFIG_XXX(rec_float, RECD_FLOAT);
}


int
RecRegisterConfigString(RecT rec_type, const char *name,
                        const char *data_default_tmp, RecUpdateT update_type,
                        RecCheckT check_type, const char *check_regex, RecAccessT access_type)
{
  RecString data_default = (RecString)data_default_tmp;
  ink_assert((rec_type == RECT_CONFIG) || (rec_type == RECT_LOCAL));
  REC_REGISTER_CONFIG_XXX(rec_string, RECD_STRING);
}

int
RecRegisterConfigCounter(RecT rec_type, const char *name,
                         RecCounter data_default, RecUpdateT update_type,
                         RecCheckT check_type, const char *check_regex, RecAccessT access_type)
{
  ink_assert((rec_type == RECT_CONFIG) || (rec_type == RECT_LOCAL));
  REC_REGISTER_CONFIG_XXX(rec_counter, RECD_COUNTER);
}


//-------------------------------------------------------------------------
// RecSetRecordXXX
//-------------------------------------------------------------------------
int
RecSetRecord(RecT rec_type, const char *name, RecDataT data_type, RecData *data, RecRawStat *data_raw, bool lock, bool inc_version)
{
  int err = REC_ERR_OKAY;
  RecRecord *r1;

  // FIXME: Most of the time we set, we don't actually need to wrlock
  // since we are not modifying the g_records_ht.
  if (lock) {
    ink_rwlock_wrlock(&g_records_rwlock);
  }

  if (ink_hash_table_lookup(g_records_ht, name, (void **) &r1)) {
    if (i_am_the_record_owner(r1->rec_type)) {
      rec_mutex_acquire(&(r1->lock));
      if ((data_type != RECD_NULL) && (r1->data_type != data_type)) {
        err = REC_ERR_FAIL;
      } else {
        if (data_type == RECD_NULL) {
          ink_assert(data->rec_string);
          switch (r1->data_type) {
          case RECD_INT:
            r1->data.rec_int = ink_atoi64(data->rec_string);
            data_type = RECD_INT;
            break;
          case RECD_FLOAT:
            r1->data.rec_float = atof(data->rec_string);
            data_type = RECD_FLOAT;
            break;
          case RECD_STRING:
            data_type = RECD_STRING;
            r1->data.rec_string = data->rec_string;
            break;
          case RECD_COUNTER:
            r1->data.rec_int = ink_atoi64(data->rec_string);
            data_type = RECD_COUNTER;
            break;
          default:
            err = REC_ERR_FAIL;
            break;
          }
        }

        if (RecDataSet(data_type, &(r1->data), data)) {
          r1->sync_required = REC_SYNC_REQUIRED;
          if (inc_version) {
            r1->sync_required |= REC_INC_CONFIG_VERSION;
          }
          if (REC_TYPE_IS_CONFIG(r1->rec_type)) {
            r1->config_meta.update_required = REC_UPDATE_REQUIRED;
          }
        }
        if (REC_TYPE_IS_STAT(r1->rec_type) && (data_raw != NULL)) {
          r1->stat_meta.data_raw = *data_raw;
        }
      }
      rec_mutex_release(&(r1->lock));
    } else {
      // We don't need to ats_strdup() here as we will make copies of any
      // strings when we marshal them into our RecMessage buffer.
      RecRecord r2;

      RecRecordInit(&r2);
      r2.rec_type = rec_type;
      r2.name = name;
      r2.data_type = (data_type != RECD_NULL) ? data_type : r1->data_type;
      r2.data = *data;
      if (REC_TYPE_IS_STAT(r2.rec_type) && (data_raw != NULL)) {
        r2.stat_meta.data_raw = *data_raw;
      }
      err = send_set_message(&r2);
      RecRecordFree(&r2);
    }
  } else {
    // Add the record but do not set the 'registered' flag, as this
    // record really hasn't been registered yet.  Also, in order to
    // add the record, we need to have a rec_type, so if the user
    // calls RecSetRecord on a record we haven't registered yet, we
    // should fail out here.
    if ((rec_type == RECT_NULL) || (data_type == RECD_NULL)) {
      err = REC_ERR_FAIL;
      goto Ldone;
    }
    r1 = RecAlloc(rec_type, name, data_type);
    RecDataSet(data_type, &(r1->data), data);
    if (REC_TYPE_IS_STAT(r1->rec_type) && (data_raw != NULL)) {
      r1->stat_meta.data_raw = *data_raw;
    }
    if (i_am_the_record_owner(r1->rec_type)) {
      r1->sync_required = r1->sync_required | REC_PEER_SYNC_REQUIRED;
    } else {
      err = send_set_message(r1);
    }
    ink_hash_table_insert(g_records_ht, name, (void *) r1);

  }

Ldone:
  if (lock) {
    ink_rwlock_unlock(&g_records_rwlock);
  }

  return err;
}

int
RecSetRecordConvert(const char *name, const RecString rec_string, bool lock, bool inc_version)
{
  RecData data;
  data.rec_string = rec_string;
  return RecSetRecord(RECT_NULL, name, RECD_NULL, &data, NULL, lock, inc_version);
}

int
RecSetRecordInt(const char *name, RecInt rec_int, bool lock, bool inc_version)
{
  RecData data;
  data.rec_int = rec_int;
  return RecSetRecord(RECT_NULL, name, RECD_INT, &data, NULL, lock, inc_version);
}

int
RecSetRecordFloat(const char *name, RecFloat rec_float, bool lock, bool inc_version)
{
  RecData data;
  data.rec_float = rec_float;
  return RecSetRecord(RECT_NULL, name, RECD_FLOAT, &data, NULL, lock, inc_version);
}

int
RecSetRecordString(const char *name, const RecString rec_string, bool lock, bool inc_version)
{
  RecData data;
  data.rec_string = rec_string;
  return RecSetRecord(RECT_NULL, name, RECD_STRING, &data, NULL, lock, inc_version);
}

int
RecSetRecordCounter(const char *name, RecCounter rec_counter, bool lock, bool inc_version)
{
  RecData data;
  data.rec_counter = rec_counter;
  return RecSetRecord(RECT_NULL, name, RECD_COUNTER, &data, NULL, lock, inc_version);
}


//-------------------------------------------------------------------------
// RecReadStatsFile
//-------------------------------------------------------------------------
int
RecReadStatsFile()
{
  RecRecord *r;
  RecMessage *m;
  RecMessageItr itr;
  RecPersistT persist_type = RECP_NULL;
  ats_scoped_str snap_fpath(RecConfigReadPersistentStatsPath());

  // lock our hash table
  ink_rwlock_wrlock(&g_records_rwlock);

  if ((m = RecMessageReadFromDisk(snap_fpath)) != NULL) {
    if (RecMessageUnmarshalFirst(m, &itr, &r) != REC_ERR_FAIL) {
      do {
        if ((r->name == NULL) || (!strlen(r->name))) {
          continue;
        }

        // If we don't have a persistence type for this record, it means that it is not a stat, or it is
        // not registered yet. Either way, it's ok to just set the persisted value and keep going.
        if (RecGetRecordPersistenceType(r->name, &persist_type, false /* lock */) != REC_ERR_OKAY) {
          RecDebug(DL_Debug, "restoring value for persisted stat '%s'", r->name);
          RecSetRecord(r->rec_type, r->name, r->data_type, &(r->data), &(r->stat_meta.data_raw), false);
          continue;
        }

        if (!REC_TYPE_IS_STAT(r->rec_type)) {
          // This should not happen, but be defensive against records changing their type ..
          RecLog(DL_Warning, "skipping restore of non-stat record '%s'", r->name);
          continue;
        }

        // Check whether the persistence type was changed by a new software version. If the record is
        // already registered with an updated persistence type, then we don't want to set it. We should
        // keep the registered value.
        if (persist_type == RECP_NON_PERSISTENT) {
          RecDebug(DL_Debug, "preserving current value of formerly persistent stat '%s'", r->name);
          continue;
        }

        RecDebug(DL_Debug, "restoring value for persisted stat '%s'", r->name);
        RecSetRecord(r->rec_type, r->name, r->data_type, &(r->data), &(r->stat_meta.data_raw), false);
      } while (RecMessageUnmarshalNext(m, &itr, &r) != REC_ERR_FAIL);
    }
  }

  ink_rwlock_unlock(&g_records_rwlock);
  ats_free(m);

  return REC_ERR_OKAY;
}


//-------------------------------------------------------------------------
// RecSyncStatsFile
//-------------------------------------------------------------------------
int
RecSyncStatsFile()
{
  RecRecord *r;
  RecMessage *m;
  int i, num_records;
  bool sync_to_disk;
  ats_scoped_str snap_fpath(RecConfigReadPersistentStatsPath());

  /*
   * g_mode_type should be initialized by
   * RecLocalInit() or RecProcessInit() earlier.
   */
  ink_assert(g_mode_type != RECM_NULL);

  if (g_mode_type == RECM_SERVER || g_mode_type == RECM_STAND_ALONE) {
    m = RecMessageAlloc(RECG_NULL);
    num_records = g_num_records;
    sync_to_disk = false;
    for (i = 0; i < num_records; i++) {
      r = &(g_records[i]);
      rec_mutex_acquire(&(r->lock));
      if (REC_TYPE_IS_STAT(r->rec_type)) {
        if (r->stat_meta.persist_type == RECP_PERSISTENT) {
          m = RecMessageMarshal_Realloc(m, r);
          sync_to_disk = true;
        }
      }
      rec_mutex_release(&(r->lock));
    }
    if (sync_to_disk) {
      RecDebug(DL_Note, "Writing '%s' [%d bytes]", (const char *)snap_fpath, m->o_write - m->o_start + sizeof(RecMessageHdr));
      RecMessageWriteToDisk(m, snap_fpath);
    }
    RecMessageFree(m);
  }

  return REC_ERR_OKAY;
}

// Consume a parsed record, pushing it into the records hash table.
static void
RecConsumeConfigEntry(RecT rec_type, RecDataT data_type, const char * name, const char * value, bool inc_version)
{
    RecData data;

    memset(&data, 0, sizeof(RecData));
    RecDataSetFromString(data_type, &data, value);
    RecSetRecord(rec_type, name, data_type, &data, NULL, false, inc_version);
    RecDataClear(data_type, &data);
}

//-------------------------------------------------------------------------
// RecReadConfigFile
//-------------------------------------------------------------------------
int
RecReadConfigFile(bool inc_version)
{
  RecDebug(DL_Note, "Reading '%s'", g_rec_config_fpath);

  // lock our hash table
  ink_rwlock_wrlock(&g_records_rwlock);

  // Parse the actual fileand hash the values.
  RecConfigFileParse(g_rec_config_fpath, RecConsumeConfigEntry, inc_version);

  // release our hash table
  ink_rwlock_unlock(&g_records_rwlock);

  return REC_ERR_OKAY;
}


//-------------------------------------------------------------------------
// RecSyncConfigFile
//-------------------------------------------------------------------------
int
RecSyncConfigToTB(textBuffer * tb, bool *inc_version)
{
  int err = REC_ERR_FAIL;

  if (inc_version != NULL) {
    *inc_version = false;
  }
  /*
   * g_mode_type should be initialized by
   * RecLocalInit() or RecProcessInit() earlier.
   */
  ink_assert(g_mode_type != RECM_NULL);

  if (g_mode_type == RECM_SERVER || g_mode_type == RECM_STAND_ALONE) {
    RecRecord *r;
    int i, num_records;
    RecConfigFileEntry *cfe;
    bool sync_to_disk;

    ink_mutex_acquire(&g_rec_config_lock);

    num_records = g_num_records;
    sync_to_disk = false;
    for (i = 0; i < num_records; i++) {
      r = &(g_records[i]);
      rec_mutex_acquire(&(r->lock));
      if (REC_TYPE_IS_CONFIG(r->rec_type)) {
        if (r->sync_required & REC_DISK_SYNC_REQUIRED) {
          if (!ink_hash_table_isbound(g_rec_config_contents_ht, r->name)) {
            cfe = (RecConfigFileEntry *)ats_malloc(sizeof(RecConfigFileEntry));
            cfe->entry_type = RECE_RECORD;
            cfe->entry = ats_strdup(r->name);
            enqueue(g_rec_config_contents_llq, (void *) cfe);
            ink_hash_table_insert(g_rec_config_contents_ht, r->name, NULL);
          }
          r->sync_required = r->sync_required & ~REC_DISK_SYNC_REQUIRED;
          sync_to_disk = true;
          if (r->sync_required & REC_INC_CONFIG_VERSION) {
            r->sync_required = r->sync_required & ~REC_INC_CONFIG_VERSION;
            if (r->rec_type != RECT_LOCAL && inc_version != NULL) {
              *inc_version = true;
            }
          }
        }
      }
      rec_mutex_release(&(r->lock));
    }

    if (sync_to_disk) {
      char b[1024];

      // okay, we're going to write into our textBuffer
      err = REC_ERR_OKAY;
      tb->reUse();

      ink_rwlock_rdlock(&g_records_rwlock);

      LLQrec *llq_rec = g_rec_config_contents_llq->head;
      while (llq_rec != NULL) {
        cfe = (RecConfigFileEntry *) llq_rec->data;
        if (cfe->entry_type == RECE_COMMENT) {
          tb->copyFrom(cfe->entry, strlen(cfe->entry));
          tb->copyFrom("\n", 1);
        } else {
          if (ink_hash_table_lookup(g_records_ht, cfe->entry, (void **) &r)) {
            rec_mutex_acquire(&(r->lock));
            // rec_type
            switch (r->rec_type) {
            case RECT_CONFIG:
              tb->copyFrom("CONFIG ", 7);
              break;
            case RECT_PROCESS:
              tb->copyFrom("PROCESS ", 8);
              break;
            case RECT_NODE:
              tb->copyFrom("NODE ", 5);
              break;
            case RECT_CLUSTER:
              tb->copyFrom("CLUSTER ", 8);
              break;
            case RECT_LOCAL:
              tb->copyFrom("LOCAL ", 6);
              break;
            default:
              ink_assert(!"Unexpected RecT type");
              break;
            }
            // name
            tb->copyFrom(cfe->entry, strlen(cfe->entry));
            tb->copyFrom(" ", 1);
            // data_type and value
            switch (r->data_type) {
            case RECD_INT:
              tb->copyFrom("INT ", 4);
              snprintf(b, 1023, "%" PRId64 "", r->data.rec_int);
              tb->copyFrom(b, strlen(b));
              break;
            case RECD_FLOAT:
              tb->copyFrom("FLOAT ", 6);
              snprintf(b, 1023, "%f", r->data.rec_float);
              tb->copyFrom(b, strlen(b));
              break;
            case RECD_STRING:
              tb->copyFrom("STRING ", 7);
              if (r->data.rec_string) {
                tb->copyFrom(r->data.rec_string, strlen(r->data.rec_string));
              } else {
                tb->copyFrom("NULL", strlen("NULL"));
              }
              break;
            case RECD_COUNTER:
              tb->copyFrom("COUNTER ", 8);
              snprintf(b, 1023, "%" PRId64 "", r->data.rec_counter);
              tb->copyFrom(b, strlen(b));
              break;
            default:
              ink_assert(!"Unexpected RecD type");
              break;
            }
            tb->copyFrom("\n", 1);
            rec_mutex_release(&(r->lock));
          }
        }
        llq_rec = llq_rec->next;
      }
      ink_rwlock_unlock(&g_records_rwlock);
    }
    ink_mutex_release(&g_rec_config_lock);
  }

  return err;
}


//-------------------------------------------------------------------------
// RecExecConifgUpdateCbs
//-------------------------------------------------------------------------
int
RecExecConfigUpdateCbs(unsigned int update_required_type)
{
  RecRecord *r;
  int i, num_records;

  num_records = g_num_records;
  for (i = 0; i < num_records; i++) {
    r = &(g_records[i]);
    rec_mutex_acquire(&(r->lock));
    if (REC_TYPE_IS_CONFIG(r->rec_type)) {
      /* -- upgrade to support a list of callback functions
         if ((r->config_meta.update_required & update_required_type) &&
         (r->config_meta.update_cb)) {
         (*(r->config_meta.update_cb))(r->name, r->data_type, r->data,
         r->config_meta.update_cookie);
         r->config_meta.update_required =
         r->config_meta.update_required & ~update_required_type;
         }
       */

      if ((r->config_meta.update_required & update_required_type) && (r->config_meta.update_cb_list)) {
        RecConfigUpdateCbList *cur_callback = NULL;
        for (cur_callback = r->config_meta.update_cb_list; cur_callback; cur_callback = cur_callback->next) {
          (*(cur_callback->update_cb)) (r->name, r->data_type, r->data, cur_callback->update_cookie);
        }
        r->config_meta.update_required = r->config_meta.update_required & ~update_required_type;
      }
    }
    rec_mutex_release(&(r->lock));
  }

  return REC_ERR_OKAY;
}


//------------------------------------------------------------------------
// RecResetStatRecord
//------------------------------------------------------------------------
int
RecResetStatRecord(const char *name)
{
  RecRecord *r1 = NULL;
  int err = REC_ERR_OKAY;

  if (ink_hash_table_lookup(g_records_ht, name, (void **) &r1)) {
    if (i_am_the_record_owner(r1->rec_type)) {
      rec_mutex_acquire(&(r1->lock));
      ++(r1->version);
      RecDataSet(r1->data_type, &(r1->data), &(r1->data_default));
      rec_mutex_release(&(r1->lock));
      err = REC_ERR_OKAY;
    } else {
      RecRecord r2;

      RecRecordInit(&r2);
      r2.rec_type = r1->rec_type;
      r2.name = r1->name;
      r2.data_type = r1->data_type;
      r2.data = r1->data_default;

      err = send_reset_message(&r2);
      RecRecordFree(&r2);
    }
  } else {
    err = REC_ERR_FAIL;
  }

  return err;
}


//------------------------------------------------------------------------
// RecResetStatRecord
//------------------------------------------------------------------------
int
RecResetStatRecord(RecT type, bool all)
{
  int i, num_records;
  int err = REC_ERR_OKAY;

  RecDebug(DL_Note, "Reset Statistics Records");

  num_records = g_num_records;
  for (i = 0; i < num_records; i++) {
    RecRecord *r1 = &(g_records[i]);

    if (REC_TYPE_IS_STAT(r1->rec_type) && ((type == RECT_NULL) || (r1->rec_type == type)) &&
        (all || (r1->stat_meta.persist_type != RECP_NON_PERSISTENT)) &&
        (r1->data_type != RECD_STRING)) {
      if (i_am_the_record_owner(r1->rec_type)) {
        rec_mutex_acquire(&(r1->lock));
        ++(r1->version);
        if (!RecDataSet(r1->data_type, &(r1->data), &(r1->data_default))) {
          err = REC_ERR_FAIL;
        }
        rec_mutex_release(&(r1->lock));
      } else {
        RecRecord r2;

        RecRecordInit(&r2);
        r2.rec_type = r1->rec_type;
        r2.name = r1->name;
        r2.data_type = r1->data_type;
        r2.data = r1->data_default;

        err = send_reset_message(&r2);
        RecRecordFree(&r2);
      }
    }
  }
  return err;
}


int
RecSetSyncRequired(char *name, bool lock)
{
  int err = REC_ERR_FAIL;
  RecRecord *r1;

  // FIXME: Most of the time we set, we don't actually need to wrlock
  // since we are not modifying the g_records_ht.
  if (lock) {
    ink_rwlock_wrlock(&g_records_rwlock);
  }

  if (ink_hash_table_lookup(g_records_ht, name, (void **) &r1)) {
    if (i_am_the_record_owner(r1->rec_type)) {
      rec_mutex_acquire(&(r1->lock));
      r1->sync_required = REC_SYNC_REQUIRED;
      if (REC_TYPE_IS_CONFIG(r1->rec_type)) {
        r1->config_meta.update_required = REC_UPDATE_REQUIRED;
      }
      rec_mutex_release(&(r1->lock));
      err = REC_ERR_OKAY;
    } else {
      // No point of doing the following because our peer will
      // set the value with RecDataSet. However, since
      // r2.name == r1->name, the sync_required bit will not be
      // set.

      /*
         RecRecord r2;

         RecRecordInit(&r2);
         r2.rec_type  = r1->rec_type;
         r2.name      = r1->name;
         r2.data_type = r1->data_type;
         r2.data      = r1->data_default;

         err = send_set_message(&r2);
         RecRecordFree(&r2);
       */
    }
  }

  if (lock) {
    ink_rwlock_unlock(&g_records_rwlock);
  }

  return err;
}

int RecWriteConfigFile(textBuffer *tb)
{
#define TMP_FILENAME_EXT_STR ".tmp"
#define TMP_FILENAME_EXT_LEN (sizeof(TMP_FILENAME_EXT_STR) - 1)

  int nbytes;
  int filename_len;
  int tmp_filename_len;
  int result;
  char buff[1024];
  char *tmp_filename;

  filename_len = strlen(g_rec_config_fpath);
  tmp_filename_len = filename_len + TMP_FILENAME_EXT_LEN;
  if (tmp_filename_len < (int)sizeof(buff)) {
    tmp_filename = buff;
  } else {
    tmp_filename = (char *)ats_malloc(tmp_filename_len + 1);
  }
  sprintf(tmp_filename, "%s%s", g_rec_config_fpath, TMP_FILENAME_EXT_STR);

  RecDebug(DL_Note, "Writing '%s'", g_rec_config_fpath);

  RecHandle h_file = RecFileOpenW(tmp_filename);
  do {
    if (h_file == REC_HANDLE_INVALID) {
      RecLog(DL_Warning, "open file: %s to write fail, errno: %d, error info: %s",
          tmp_filename, errno, strerror(errno));
      result = REC_ERR_FAIL;
      break;
    }

    if (RecFileWrite(h_file, tb->bufPtr(), tb->spaceUsed(), &nbytes) != REC_ERR_OKAY) {
      RecLog(DL_Warning, "write to file: %s fail, errno: %d, error info: %s",
          tmp_filename, errno, strerror(errno));
      result = REC_ERR_FAIL;
      break;
    }

    if (nbytes != tb->spaceUsed()) {
      RecLog(DL_Warning, "write to file: %s fail, disk maybe full", tmp_filename);
      result = REC_ERR_FAIL;
      break;
    }

    if (RecFileSync(h_file) != REC_ERR_OKAY) {
      RecLog(DL_Warning, "fsync file: %s fail, errno: %d, error info: %s",
          tmp_filename, errno, strerror(errno));
      result = REC_ERR_FAIL;
      break;
    }
    if (RecFileClose(h_file) != REC_ERR_OKAY) {
      RecLog(DL_Warning, "close file: %s fail, errno: %d, error info: %s",
          tmp_filename, errno, strerror(errno));
      result = REC_ERR_FAIL;
      break;
    }
    h_file = REC_HANDLE_INVALID;

    if (rename(tmp_filename, g_rec_config_fpath) != 0) {
      RecLog(DL_Warning, "rename file %s to %s fail, errno: %d, error info: %s",
          tmp_filename, g_rec_config_fpath, errno, strerror(errno));
      result = REC_ERR_FAIL;
      break;
    }

    result = REC_ERR_OKAY;
  } while (0);

  if (h_file != REC_HANDLE_INVALID) {
    RecFileClose(h_file);
  }
  if (tmp_filename != buff) {
    ats_free(tmp_filename);
  }
  return result;
}

