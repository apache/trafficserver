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
#include "ink_string.h"

#include "P_RecCompatibility.h"
#include "P_RecUtils.h"


//-------------------------------------------------------------------------
// i_am_the_record_owner
//-------------------------------------------------------------------------
static bool
i_am_the_record_owner(RecT rec_type)
{
#if defined (REC_LOCAL)

  switch (rec_type) {
  case RECT_CONFIG:
  case RECT_NODE:
  case RECT_CLUSTER:
  case RECT_LOCAL:
    return true;
  case RECT_PROCESS:
  case RECT_PLUGIN:
    return false;
  default:
    ink_debug_assert(!"Unexpected RecT type");
    return false;
  }

#elif defined (REC_PROCESS)

  // g_mode_type is defined in either RecLocal.cc or RecProcess.cc.
  // We can access it since we're inlined by on of these two files.
  if (g_mode_type == RECM_CLIENT) {
    switch (rec_type) {
    case RECT_PROCESS:
    case RECT_PLUGIN:
      return true;
    case RECT_CONFIG:
    case RECT_NODE:
    case RECT_CLUSTER:
    case RECT_LOCAL:
      return false;
    default:
      ink_debug_assert(!"Unexpected RecT type");
      return false;
    }
  } else if (g_mode_type == RECM_STAND_ALONE) {
    switch (rec_type) {
    case RECT_CONFIG:
    case RECT_PROCESS:
    case RECT_NODE:
    case RECT_CLUSTER:
    case RECT_LOCAL:
    case RECT_PLUGIN:
      return true;
    default:
      ink_debug_assert(!"Unexpected RecT type");
      return false;
    }
  }
#else

#error "Required #define not specificed; expected REC_LOCAL or REC_PROCESS"

#endif

  return false;
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
static int
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
static int
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
        r->sync_required = r->sync_required & ~REC_PEER_SYNC_REQUIRED;
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
static int
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
          (REC_TYPE_IS_STAT(r->rec_type) && !(r->stat_meta.persist_type != RECP_NON_PERSISTENT))) {
        rec_mutex_acquire(&(r->lock));
        m = RecMessageMarshal_Realloc(m, r);
        r->sync_required = r->sync_required & ~REC_PEER_SYNC_REQUIRED;
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
static int
recv_message_cb(RecMessage * msg, RecMessageT msg_type, void *cookie)
{
  REC_NOWARN_UNUSED(cookie);

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
    ink_debug_assert(!"Unexpected RecG type");
    return REC_ERR_FAIL;

  }

  return REC_ERR_OKAY;
}


//-------------------------------------------------------------------------
// RecRegisterStatXXX
//-------------------------------------------------------------------------
#define REC_REGISTER_STAT_XXX(A, B) \
  ink_debug_assert((rec_type == RECT_NODE)    || \
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
RecRegisterStatInt(RecT rec_type, const char *name, RecInt data_default, RecPersistT persist_type)
{
  REC_REGISTER_STAT_XXX(rec_int, RECD_INT);
}

int
RecRegisterStatFloat(RecT rec_type, const char *name, RecFloat data_default, RecPersistT persist_type)
{
  REC_REGISTER_STAT_XXX(rec_float, RECD_FLOAT);
}

int
RecRegisterStatString(RecT rec_type, const char *name, RecString data_default, RecPersistT persist_type)
{
  REC_REGISTER_STAT_XXX(rec_string, RECD_STRING);
}

int
RecRegisterStatCounter(RecT rec_type, const char *name, RecCounter data_default, RecPersistT persist_type)
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
			     update_type, check_type, \
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
  ink_debug_assert((rec_type == RECT_CONFIG) || (rec_type == RECT_LOCAL));
  REC_REGISTER_CONFIG_XXX(rec_int, RECD_INT);
}

int
RecRegisterConfigFloat(RecT rec_type, const char *name,
                       RecFloat data_default, RecUpdateT update_type,
                       RecCheckT check_type, const char *check_regex, RecAccessT access_type)
{
  ink_debug_assert((rec_type == RECT_CONFIG) || (rec_type == RECT_LOCAL));
  REC_REGISTER_CONFIG_XXX(rec_float, RECD_FLOAT);
}


int
RecRegisterConfigString(RecT rec_type, const char *name,
                        const char *data_default_tmp, RecUpdateT update_type,
                        RecCheckT check_type, const char *check_regex, RecAccessT access_type)
{
  RecString data_default = (RecString)data_default_tmp;
  ink_debug_assert((rec_type == RECT_CONFIG) || (rec_type == RECT_LOCAL));
  REC_REGISTER_CONFIG_XXX(rec_string, RECD_STRING);
}

int
RecRegisterConfigCounter(RecT rec_type, const char *name,
                         RecCounter data_default, RecUpdateT update_type,
                         RecCheckT check_type, const char *check_regex, RecAccessT access_type)
{
  ink_debug_assert((rec_type == RECT_CONFIG) || (rec_type == RECT_LOCAL));
  REC_REGISTER_CONFIG_XXX(rec_counter, RECD_COUNTER);
}


//-------------------------------------------------------------------------
// RecSetRecordXXX
//-------------------------------------------------------------------------
int
RecSetRecord(RecT rec_type, const char *name, RecDataT data_type, RecData *data, RecRawStat *data_raw, bool lock)
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
        g_num_update[r1->rec_type]++;

        if (RecDataSet(data_type, &(r1->data), data)) {
          r1->sync_required = REC_SYNC_REQUIRED;
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
      memset(&r2, 0, sizeof(RecRecord));
      r2.rec_type = rec_type;
      r2.name = name;
      r2.data_type = (data_type != RECD_NULL) ? data_type : r1->data_type;
      r2.data = *data;
      if (REC_TYPE_IS_STAT(r2.rec_type) && (data_raw != NULL)) {
        r2.stat_meta.data_raw = *data_raw;
      }
      err = send_set_message(&r2);
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
RecSetRecordConvert(const char *name, const RecString rec_string, bool lock)
{
  RecData data;
  data.rec_string = rec_string;
  return RecSetRecord(RECT_NULL, name, RECD_NULL, &data, NULL, lock);
}

int
RecSetRecordInt(const char *name, RecInt rec_int, bool lock)
{
  RecData data;
  data.rec_int = rec_int;
  return RecSetRecord(RECT_NULL, name, RECD_INT, &data, NULL, lock);
}

int
RecSetRecordFloat(const char *name, RecFloat rec_float, bool lock)
{
  RecData data;
  data.rec_float = rec_float;
  return RecSetRecord(RECT_NULL, name, RECD_FLOAT, &data, NULL, lock);
}

int
RecSetRecordString(const char *name, const RecString rec_string, bool lock)
{
  RecData data;
  data.rec_string = rec_string;
  return RecSetRecord(RECT_NULL, name, RECD_STRING, &data, NULL, lock);
}

int
RecSetRecordCounter(const char *name, RecCounter rec_counter, bool lock)
{
  RecData data;
  data.rec_counter = rec_counter;
  return RecSetRecord(RECT_NULL, name, RECD_COUNTER, &data, NULL, lock);
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

  // lock our hash table
  ink_rwlock_wrlock(&g_records_rwlock);

  if ((m = RecMessageReadFromDisk(g_stats_snap_fpath)) != NULL) {
    if (RecMessageUnmarshalFirst(m, &itr, &r) != REC_ERR_FAIL) {
      do {
        if ((r->name == NULL) || (!strlen(r->name)))
          continue;
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

  // g_mode_type is defined in either RecLocal.cc or RecProcess.cc.
  // We can access it since we're inlined by on of these two files.
  if (g_mode_type == RECM_SERVER || g_mode_type == RECM_STAND_ALONE) {
    m = RecMessageAlloc(RECG_NULL);
    num_records = g_num_records;
    sync_to_disk = false;
    for (i = 0; i < num_records; i++) {
      r = &(g_records[i]);
      rec_mutex_acquire(&(r->lock));
      if (REC_TYPE_IS_STAT(r->rec_type)) {
        if (r->stat_meta.persist_type != RECP_NON_PERSISTENT) {
          m = RecMessageMarshal_Realloc(m, r);
          sync_to_disk = true;
        }
      }
      rec_mutex_release(&(r->lock));
    }
    if (sync_to_disk) {
      RecDebug(DL_Note, "Writing '%s' [%d bytes]", g_stats_snap_fpath, m->o_write - m->o_start + sizeof(RecMessageHdr));
      RecMessageWriteToDisk(m, g_stats_snap_fpath);
    }
    RecMessageFree(m);
  }

  return REC_ERR_OKAY;
}


//-------------------------------------------------------------------------
// RecReadConfigFile
//-------------------------------------------------------------------------
int
RecReadConfigFile()
{
  char *fbuf;
  int fsize;

  const char *line;
  int line_num;

  char *rec_type_str, *name_str, *data_type_str, *data_str;
  RecT rec_type;
  RecDataT data_type;
  RecData data;

  Tokenizer line_tok("\r\n");
  tok_iter_state line_tok_state;

  RecConfigFileEntry *cfe;

  RecDebug(DL_Note, "Reading '%s'", g_rec_config_fpath);

  // watch out, we're altering our g_rec_config_xxx structures
  ink_mutex_acquire(&g_rec_config_lock);

  if (RecFileImport_Xmalloc(g_rec_config_fpath, &fbuf, &fsize) == REC_ERR_FAIL) {
    RecLog(DL_Warning, "Could not import '%s'", g_rec_config_fpath);
    ink_mutex_release(&g_rec_config_lock);
    return REC_ERR_FAIL;
  }
  // clear our g_rec_config_contents_xxx structures
  while (!queue_is_empty(g_rec_config_contents_llq)) {
    cfe = (RecConfigFileEntry *) dequeue(g_rec_config_contents_llq);
    ats_free(cfe->entry);
    ats_free(cfe);
  }
  ink_hash_table_destroy(g_rec_config_contents_ht);
  g_rec_config_contents_ht = ink_hash_table_create(InkHashTableKeyType_String);

  // lock our hash table
  ink_rwlock_wrlock(&g_records_rwlock);

  memset(&data, 0, sizeof(RecData));
  line_tok.Initialize(fbuf, SHARE_TOKS);
  line = line_tok.iterFirst(&line_tok_state);
  line_num = 1;
  while (line) {
    char *lc = ats_strdup(line);
    char *lt = lc;
    char *ln;

    while (isspace(*lt))
      lt++;
    rec_type_str = ink_strtok_r(lt, " \t", &ln);

    // check for blank lines and comments
    if ((!rec_type_str) || (rec_type_str && (*rec_type_str == '#'))) {
      goto L_next_line;
    }

    name_str = ink_strtok_r(NULL, " \t", &ln);
    data_type_str = ink_strtok_r(NULL, " \t", &ln);

    // extract the string data (a little bit tricker since it can have spaces)
    if (ln) {
      // 'ln' will point to either the next token or a bunch of spaces
      // if the user didn't supply a value (e.g. 'STRING   ').  First
      // scan past all of the spaces.  If we hit a '\0', then we we
      // know we didn't have a valid value.  If not, set 'data_str' to
      // the start of the token and scan until we find the end.  Once
      // the end is found, back-peddle to remove any trailing spaces.
      while (isspace(*ln))
        ln++;
      if (*ln == '\0') {
        data_str = NULL;
      } else {
        data_str = ln;
        while (*ln != '\0')
          ln++;
        ln--;
        while (isspace(*ln) && (ln > data_str))
          ln--;
        ln++;
        *ln = '\0';
      }
    } else {
      data_str = NULL;
    }

    // check for errors
    if (!(rec_type_str && name_str && data_type_str && data_str)) {
      RecLog(DL_Warning, "Could not parse line at '%s:%d' -- skipping line: '%s'", g_rec_config_fpath, line_num, line);
      goto L_next_line;
    }
    // record type
    rec_type = RECT_NULL;
    if (strcmp(rec_type_str, "CONFIG") == 0) {
      rec_type = RECT_CONFIG;
    } else if (strcmp(rec_type_str, "PROCESS") == 0) {
      rec_type = RECT_PROCESS;
    } else if (strcmp(rec_type_str, "NODE") == 0) {
      rec_type = RECT_NODE;
    } else if (strcmp(rec_type_str, "CLUSTER") == 0) {
      rec_type = RECT_CLUSTER;
    } else if (strcmp(rec_type_str, "LOCAL") == 0) {
      rec_type = RECT_LOCAL;
    } else {
      RecLog(DL_Warning, "Unknown record type '%s' at '%s:%d' -- skipping line", rec_type_str, g_rec_config_fpath, line_num);
      goto L_next_line;
    }

    // data_type
    data_type = RECD_NULL;
    if (strcmp(data_type_str, "INT") == 0) {
      data_type = RECD_INT;
    } else if (strcmp(data_type_str, "FLOAT") == 0) {
      data_type = RECD_FLOAT;
    } else if (strcmp(data_type_str, "STRING") == 0) {
      data_type = RECD_STRING;
    } else if (strcmp(data_type_str, "COUNTER") == 0) {
      data_type = RECD_COUNTER;
    } else {
      RecLog(DL_Warning, "Unknown data type '%s' at '%s:%d' -- skipping line", data_type_str, g_rec_config_fpath, line_num);
      goto L_next_line;
    }

    // set the record
    RecDataSetFromString(data_type, &data, data_str);
    RecSetRecord(rec_type, name_str, data_type, &data, NULL, false);
    RecDataClear(data_type, &data);

    // update our g_rec_config_contents_xxx
    cfe = (RecConfigFileEntry *)ats_malloc(sizeof(RecConfigFileEntry));
    cfe->entry_type = RECE_RECORD;
    cfe->entry = ats_strdup(name_str);
    enqueue(g_rec_config_contents_llq, (void *) cfe);
    ink_hash_table_insert(g_rec_config_contents_ht, name_str, NULL);
    goto L_done;

  L_next_line:
    // store this line into g_rec_config_contents_llq so that we can
    // write it out later
    cfe = (RecConfigFileEntry *)ats_malloc(sizeof(RecConfigFileEntry));
    cfe->entry_type = RECE_COMMENT;
    cfe->entry = ats_strdup(line);
    enqueue(g_rec_config_contents_llq, (void *) cfe);

  L_done:
    line = line_tok.iterNext(&line_tok_state);
    line_num++;
    ats_free(lc);
  }

  // release our hash table
  ink_rwlock_unlock(&g_records_rwlock);
  ink_mutex_release(&g_rec_config_lock);
  ats_free(fbuf);

  return REC_ERR_OKAY;
}


//-------------------------------------------------------------------------
// RecSyncConfigFile
//-------------------------------------------------------------------------
int
RecSyncConfigToTB(textBuffer * tb)
{
  int err = REC_ERR_FAIL;

  // g_mode_type is defined in either RecLocal.cc or RecProcess.cc.
  // We can access it since we're inlined by on of these two files.
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
              ink_debug_assert(!"Unexpected RecT type");
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
              ink_debug_assert(!"Unexpected RecD type");
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
RecExecConfigUpdateCbs()
{
  RecRecord *r;
  int i, num_records;
  unsigned int update_required_type;

#if defined (REC_LOCAL)
  update_required_type = REC_LOCAL_UPDATE_REQUIRED;
#elif defined (REC_PROCESS)
  update_required_type = REC_PROCESS_UPDATE_REQUIRED;
#else
#error "Required #define not specificed; expected REC_LOCAL or REC_PROCESS"
#endif

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
RecResetStatRecord(char *name)
{
  RecRecord *r1 = NULL;
  int err = REC_ERR_OKAY;

  if (ink_hash_table_lookup(g_records_ht, name, (void **) &r1)) {
    if (i_am_the_record_owner(r1->rec_type)) {
      rec_mutex_acquire(&(r1->lock));
      RecDataSet(r1->data_type, &(r1->data), &(r1->data_default));
      rec_mutex_release(&(r1->lock));
      err = REC_ERR_OKAY;
    } else {
      RecRecord r2;
      memset(&r2, 0, sizeof(RecRecord));
      r2.rec_type = r1->rec_type;
      r2.name = r1->name;
      r2.data_type = r1->data_type;
      r2.data = r1->data_default;

      err = send_set_message(&r2);
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
        if (!RecDataSet(r1->data_type, &(r1->data), &(r1->data_default))) {
          err = REC_ERR_FAIL;
        }
        rec_mutex_release(&(r1->lock));
      } else {
        RecRecord r2;
        memset(&r2, 0, sizeof(RecRecord));
        r2.rec_type = r1->rec_type;
        r2.name = r1->name;
        r2.data_type = r1->data_type;
        r2.data = r1->data_default;

        err = send_set_message(&r2);
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
         memset(&r2, 0, sizeof(RecRecord));
         r2.rec_type  = r1->rec_type;
         r2.name      = r1->name;
         r2.data_type = r1->data_type;
         r2.data      = r1->data_default;

         err = send_set_message(&r2);
       */
    }
  }

  if (lock) {
    ink_rwlock_unlock(&g_records_rwlock);
  }

  return err;
}
