/** @file

  Record core definitions

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

#include "libts.h"

#include "P_RecFile.h"
#include "P_RecCore.h"
#include "P_RecUtils.h"
#include "P_RecTree.h"
#include "I_Layout.h"

static bool g_initialized = false;

RecRecord *g_records = NULL;
InkHashTable *g_records_ht = NULL;
ink_rwlock g_records_rwlock;
int g_num_records = 0;

RecTree *g_records_tree = NULL;

//-------------------------------------------------------------------------
// register_record
//-------------------------------------------------------------------------
static RecRecord *
register_record(RecT rec_type, const char *name, RecDataT data_type, RecData data_default, RecPersistT persist_type)
{
  RecRecord *r = NULL;

  if (ink_hash_table_lookup(g_records_ht, name, (void **) &r)) {
    ink_release_assert(r->rec_type == rec_type);
    ink_release_assert(r->data_type == data_type);
    // Note: do not set r->data as we want to keep the previous value
    RecDataSet(r->data_type, &(r->data_default), &(data_default));
  } else {
    if ((r = RecAlloc(rec_type, name, data_type)) == NULL) {
      return NULL;
    }
    // Set the r->data to its default value as this is a new record
    RecDataSet(r->data_type, &(r->data), &(data_default));
    RecDataSet(r->data_type, &(r->data_default), &(data_default));
    ink_hash_table_insert(g_records_ht, name, (void *) r);

    if (REC_TYPE_IS_STAT(r->rec_type)) {
      r->stat_meta.persist_type = persist_type;
    }
  }

  // we're now registered
  r->registered = true;
  r->version = 0;

  return r;
}


//-------------------------------------------------------------------------
// link_XXX
//-------------------------------------------------------------------------
static int
link_int(const char * /* name */, RecDataT /* data_type */, RecData data, void *cookie)
{
  RecInt *rec_int = (RecInt *) cookie;
  ink_atomic_swap(rec_int, data.rec_int);
  return REC_ERR_OKAY;
}

static int
link_int32(const char * /* name */, RecDataT /* data_type */, RecData data, void *cookie)
{
  *((int32_t *) cookie) = (int32_t) data.rec_int;
  return REC_ERR_OKAY;
}

static int
link_uint32(const char * /* name */, RecDataT /* data_type */, RecData data, void *cookie)
{
  *((uint32_t *) cookie) = (uint32_t) data.rec_int;
  return REC_ERR_OKAY;
}

static int
link_float(const char * /* name */, RecDataT /* data_type */, RecData data, void *cookie)
{
  *((RecFloat *) cookie) = data.rec_float;
  return REC_ERR_OKAY;
}

static int
link_counter(const char * /* name */, RecDataT /* data_type */, RecData data, void *cookie)
{
  RecCounter *rec_counter = (RecCounter *) cookie;
  ink_atomic_swap(rec_counter, data.rec_counter);
  return REC_ERR_OKAY;
}

// This is a convenience wrapper, to allow us to treat the RecInt's as a
// 1-byte entity internally.
static int
link_byte(const char * /* name */, RecDataT /* data_type */, RecData data, void *cookie)
{
  RecByte *rec_byte = (RecByte *) cookie;
  RecByte byte = static_cast<RecByte>(data.rec_int);

  ink_atomic_swap(rec_byte, byte);
  return REC_ERR_OKAY;
}

// mimic Config.cc::config_string_alloc_cb
// cookie e.g. is the DEFAULT_xxx_str value which this functiion keeps up to date with
// the latest default applied during a config update from records.config
static int
link_string_alloc(const char * /* name */, RecDataT /* data_type */, RecData data, void *cookie)
{
  RecString _ss = data.rec_string;
  RecString _new_value = NULL;

  if (_ss) {
    _new_value = ats_strdup(_ss);
  }

  // set new string for DEFAULT_xxx_str tp point to
  RecString _temp2 = *((RecString *)cookie);
  *((RecString *)cookie) = _new_value;
  // free previous string DEFAULT_xxx_str points to
  ats_free(_temp2);

  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecCoreInit
//-------------------------------------------------------------------------
int
RecCoreInit(RecModeT mode_type, Diags *_diags)
{
  if (g_initialized) {
    return REC_ERR_OKAY;
  }

  // set our diags
  RecSetDiags(_diags);

  // Initialize config file parsing data structures.
  RecConfigFileInit();

  g_records_tree = new RecTree(NULL);
  g_num_records = 0;

  // initialize record array for our internal stats (this can be reallocated later)
  g_records = (RecRecord *)ats_malloc(REC_MAX_RECORDS * sizeof(RecRecord));

  // initialize record hash index
  g_records_ht = ink_hash_table_create(InkHashTableKeyType_String);
  ink_rwlock_init(&g_records_rwlock);
  if (!g_records_ht) {
    return REC_ERR_FAIL;
  }
  // read stats
  if ((mode_type == RECM_SERVER) || (mode_type == RECM_STAND_ALONE)) {
    RecReadStatsFile();
  }
  // read configs
  if ((mode_type == RECM_SERVER) || (mode_type == RECM_STAND_ALONE)) {
    ink_mutex_init(&g_rec_config_lock, NULL);
    // Import the file into memory; try the following in this order:
    // ./etc/trafficserver/records.config.shadow
    // ./records.config.shadow
    // ./etc/trafficserver/records.config
    // ./records.config
    bool file_exists = true;
    g_rec_config_fpath = Layout::relative_to(Layout::get()->sysconfdir, REC_CONFIG_FILE REC_SHADOW_EXT);
    if (RecFileExists(g_rec_config_fpath) == REC_ERR_FAIL) {
      ats_free((char *)g_rec_config_fpath);
      g_rec_config_fpath = Layout::relative_to(Layout::get()->sysconfdir, REC_CONFIG_FILE);
      if (RecFileExists(g_rec_config_fpath) == REC_ERR_FAIL) {
        RecLog(DL_Warning, "Could not find '%s', system will run with defaults\n", REC_CONFIG_FILE);
        file_exists = false;
      }
    }
    if (file_exists) {
      RecReadConfigFile(true);
    }
  }

  g_initialized = true;

  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecLinkCnfigXXX
//-------------------------------------------------------------------------
int
RecLinkConfigInt(const char *name, RecInt * rec_int)
{
  if (RecGetRecordInt(name, rec_int) == REC_ERR_FAIL) {
    return REC_ERR_FAIL;
  }
  return RecRegisterConfigUpdateCb(name, link_int, (void *) rec_int);
}

int
RecLinkConfigInt32(const char *name, int32_t * p_int32)
{
  return RecRegisterConfigUpdateCb(name, link_int32, (void *) p_int32);
}

int
RecLinkConfigUInt32(const char *name, uint32_t * p_uint32)
{
  return RecRegisterConfigUpdateCb(name, link_uint32, (void *) p_uint32);
}

int
RecLinkConfigFloat(const char *name, RecFloat * rec_float)
{
  if (RecGetRecordFloat(name, rec_float) == REC_ERR_FAIL) {
    return REC_ERR_FAIL;
  }
  return RecRegisterConfigUpdateCb(name, link_float, (void *) rec_float);
}

int
RecLinkConfigCounter(const char *name, RecCounter * rec_counter)
{
  if (RecGetRecordCounter(name, rec_counter) == REC_ERR_FAIL) {
    return REC_ERR_FAIL;
  }
  return RecRegisterConfigUpdateCb(name, link_counter, (void *) rec_counter);
}

int
RecLinkConfigString(const char *name, RecString * rec_string)
{
  if (RecGetRecordString_Xmalloc(name, rec_string) == REC_ERR_FAIL) {
    return REC_ERR_FAIL;
  }
  return RecRegisterConfigUpdateCb(name, link_string_alloc, (void *) rec_string);
}

int
RecLinkConfigByte(const char *name, RecByte * rec_byte)
{
  if (RecGetRecordByte(name, rec_byte) == REC_ERR_FAIL) {
    return REC_ERR_FAIL;
  }
  return RecRegisterConfigUpdateCb(name, link_byte, (void *) rec_byte);
}

int
RecLinkConfigBool(const char *name, RecBool * rec_bool)
{
  if (RecGetRecordBool(name, rec_bool) == REC_ERR_FAIL) {
    return REC_ERR_FAIL;
  }
  return RecRegisterConfigUpdateCb(name, link_byte, (void *) rec_bool);
}


//-------------------------------------------------------------------------
// RecRegisterConfigUpdateCb
//-------------------------------------------------------------------------
int
RecRegisterConfigUpdateCb(const char *name, RecConfigUpdateCb update_cb, void *cookie)
{
  int err = REC_ERR_FAIL;
  RecRecord *r;

  ink_rwlock_rdlock(&g_records_rwlock);

  if (ink_hash_table_lookup(g_records_ht, name, (void **) &r)) {
    rec_mutex_acquire(&(r->lock));
    if (REC_TYPE_IS_CONFIG(r->rec_type)) {
      /* -- upgrade to support a list of callback functions
         if (!(r->config_meta.update_cb)) {
         r->config_meta.update_cb = update_cb;
         r->config_meta.update_cookie = cookie;
         err = REC_ERR_OKAY;
         }
       */

      RecConfigUpdateCbList *new_callback = (RecConfigUpdateCbList *)ats_malloc(sizeof(RecConfigUpdateCbList));
      memset(new_callback, 0, sizeof(RecConfigUpdateCbList));
      new_callback->update_cb = update_cb;
      new_callback->update_cookie = cookie;

      new_callback->next = NULL;

      ink_assert(new_callback);
      if (!r->config_meta.update_cb_list) {
        r->config_meta.update_cb_list = new_callback;
      } else {
        RecConfigUpdateCbList *cur_callback = NULL;
        RecConfigUpdateCbList *prev_callback = NULL;
        for (cur_callback = r->config_meta.update_cb_list; cur_callback; cur_callback = cur_callback->next) {
          prev_callback = cur_callback;
        }
        ink_assert(prev_callback);
        ink_assert(!prev_callback->next);
        prev_callback->next = new_callback;
      }
      err = REC_ERR_OKAY;
    }

    rec_mutex_release(&(r->lock));
  }

  ink_rwlock_unlock(&g_records_rwlock);

  return err;
}


//-------------------------------------------------------------------------
// RecGetRecordXXX
//-------------------------------------------------------------------------
int
RecGetRecordInt(const char *name, RecInt *rec_int, bool lock)
{
  int err;
  RecData data;
  if ((err = RecGetRecord_Xmalloc(name, RECD_INT, &data, lock)) == REC_ERR_OKAY)
    *rec_int = data.rec_int;
  return err;
}

int
RecGetRecordFloat(const char *name, RecFloat * rec_float, bool lock)
{
  int err;
  RecData data;
  if ((err = RecGetRecord_Xmalloc(name, RECD_FLOAT, &data, lock)) == REC_ERR_OKAY)
    *rec_float = data.rec_float;
  return err;
}

int
RecGetRecordString(const char *name, char *buf, int buf_len, bool lock)
{
  int err = REC_ERR_OKAY;
  RecRecord *r;
  if (lock) {
    ink_rwlock_rdlock(&g_records_rwlock);
  }
  if (ink_hash_table_lookup(g_records_ht, name, (void **) &r)) {
    rec_mutex_acquire(&(r->lock));
    if (!r->registered || (r->data_type != RECD_STRING)) {
      err = REC_ERR_FAIL;
    } else {
      if (r->data.rec_string == NULL) {
        buf[0] = '\0';
      } else {
        ink_strlcpy(buf, r->data.rec_string, buf_len);
      }
    }
    rec_mutex_release(&(r->lock));
  } else {
    err = REC_ERR_FAIL;
  }
  if (lock) {
    ink_rwlock_unlock(&g_records_rwlock);
  }
  return err;
}

int
RecGetRecordString_Xmalloc(const char *name, RecString * rec_string, bool lock)
{
  int err;
  RecData data;
  if ((err = RecGetRecord_Xmalloc(name, RECD_STRING, &data, lock)) == REC_ERR_OKAY)
    *rec_string = data.rec_string;
  return err;
}

int
RecGetRecordCounter(const char *name, RecCounter * rec_counter, bool lock)
{
  int err;
  RecData data;
  if ((err = RecGetRecord_Xmalloc(name, RECD_COUNTER, &data, lock)) == REC_ERR_OKAY)
    *rec_counter = data.rec_counter;
  return err;
}

int
RecGetRecordByte(const char *name, RecByte *rec_byte, bool lock)
{
  int err;
  RecData data;
  if ((err = RecGetRecord_Xmalloc(name, RECD_INT, &data, lock)) == REC_ERR_OKAY)
    *rec_byte = data.rec_int;
  return err;
}

int
RecGetRecordBool(const char *name, RecBool *rec_bool, bool lock)
{
  int err;
  RecData data;
  if ((err = RecGetRecord_Xmalloc(name, RECD_INT, &data, lock)) == REC_ERR_OKAY)
    *rec_bool = 0 != data.rec_int;
  return err;
}

//-------------------------------------------------------------------------
// RecGetRec Attributes
//-------------------------------------------------------------------------
int
RecGetRecordType(const char *name, RecT * rec_type, bool lock)
{
  int err = REC_ERR_FAIL;
  RecRecord *r;

  if (lock) {
    ink_rwlock_rdlock(&g_records_rwlock);
  }

  if (ink_hash_table_lookup(g_records_ht, name, (void **) &r)) {
    rec_mutex_acquire(&(r->lock));
    *rec_type = r->rec_type;
    err = REC_ERR_OKAY;
    rec_mutex_release(&(r->lock));
  }

  if (lock) {
    ink_rwlock_unlock(&g_records_rwlock);
  }

  return err;
}


int
RecGetRecordDataType(const char *name, RecDataT * data_type, bool lock)
{
  int err = REC_ERR_FAIL;
  RecRecord *r = NULL;

  if (lock) {
    ink_rwlock_rdlock(&g_records_rwlock);
  }

  if (ink_hash_table_lookup(g_records_ht, name, (void **) &r)) {
    rec_mutex_acquire(&(r->lock));
    if (!r->registered) {
      err = REC_ERR_FAIL;
    } else {
      *data_type = r->data_type;
      err = REC_ERR_OKAY;
    }
    rec_mutex_release(&(r->lock));
  }

  if (lock) {
    ink_rwlock_unlock(&g_records_rwlock);
  }

  return err;
}

int
RecGetRecordPersistenceType(const char *name, RecPersistT * persist_type, bool lock)
{
  int err = REC_ERR_FAIL;
  RecRecord *r = NULL;

  if (lock) {
    ink_rwlock_rdlock(&g_records_rwlock);
  }

  *persist_type = RECP_NULL;

  if (ink_hash_table_lookup(g_records_ht, name, (void **) &r)) {
    rec_mutex_acquire(&(r->lock));
    if (REC_TYPE_IS_STAT(r->rec_type)) {
      *persist_type = r->stat_meta.persist_type;
      err = REC_ERR_OKAY;
    }
    rec_mutex_release(&(r->lock));
  }

  if (lock) {
    ink_rwlock_unlock(&g_records_rwlock);
  }

  return err;
}

int
RecGetRecordOrderAndId(const char *name, int* order, int* id, bool lock)
{
  int err = REC_ERR_FAIL;
  RecRecord *r = NULL;

  if (lock) {
    ink_rwlock_rdlock(&g_records_rwlock);
  }

  if (ink_hash_table_lookup(g_records_ht, name, (void **) &r)) {
    if (r->registered) {
      rec_mutex_acquire(&(r->lock));
      if (order)
        *order = r->order;
      if (id)
        *id = r->rsb_id;
      err = REC_ERR_OKAY;
      rec_mutex_release(&(r->lock));
    }
  }

  if (lock) {
    ink_rwlock_unlock(&g_records_rwlock);
  }

  return err;
}

int
RecGetRecordUpdateType(const char *name, RecUpdateT *update_type, bool lock)
{
  int err = REC_ERR_FAIL;
  RecRecord *r = NULL;

  if (lock) {
    ink_rwlock_rdlock(&g_records_rwlock);
  }

  if (ink_hash_table_lookup(g_records_ht, name, (void **) &r)) {
    rec_mutex_acquire(&(r->lock));
    if (REC_TYPE_IS_CONFIG(r->rec_type)) {
      *update_type = r->config_meta.update_type;
      err = REC_ERR_OKAY;
    } else {
      ink_assert(!"rec_type is not CONFIG");
    }
    rec_mutex_release(&(r->lock));
  }

  if (lock) {
    ink_rwlock_unlock(&g_records_rwlock);
  }

  return err;
}


int
RecGetRecordCheckType(const char *name, RecCheckT *check_type, bool lock)
{
  int err = REC_ERR_FAIL;
  RecRecord *r = NULL;

  if (lock) {
    ink_rwlock_rdlock(&g_records_rwlock);
  }

  if (ink_hash_table_lookup(g_records_ht, name, (void **) &r)) {
    rec_mutex_acquire(&(r->lock));
    if (REC_TYPE_IS_CONFIG(r->rec_type)) {
      *check_type = r->config_meta.check_type;
      err = REC_ERR_OKAY;
    } else {
      ink_assert(!"rec_type is not CONFIG");
    }
    rec_mutex_release(&(r->lock));
  }

  if (lock) {
    ink_rwlock_unlock(&g_records_rwlock);
  }

  return err;
}


int
RecGetRecordCheckExpr(const char *name, char **check_expr, bool lock)
{
  int err = REC_ERR_FAIL;
  RecRecord *r = NULL;

  if (lock) {
    ink_rwlock_rdlock(&g_records_rwlock);
  }

  if (ink_hash_table_lookup(g_records_ht, name, (void **) &r)) {
    rec_mutex_acquire(&(r->lock));
    if (REC_TYPE_IS_CONFIG(r->rec_type)) {
      *check_expr = r->config_meta.check_expr;
      err = REC_ERR_OKAY;
    } else {
      ink_assert(!"rec_type is not CONFIG");
    }
    rec_mutex_release(&(r->lock));
  }

  if (lock) {
    ink_rwlock_unlock(&g_records_rwlock);
  }

  return err;
}

int
RecGetRecordDefaultDataString_Xmalloc(char *name, char **buf, bool lock)
{
  int err;
  RecRecord *r = NULL;

  if (lock) {
    ink_rwlock_rdlock(&g_records_rwlock);
  }

  if (ink_hash_table_lookup(g_records_ht, name, (void **) &r)) {
    *buf = (char *)ats_malloc(sizeof(char) * 1024);
    memset(*buf, 0, 1024);
    err = REC_ERR_OKAY;

    switch (r->data_type) {
    case RECD_INT:
      snprintf(*buf, 1023, "%" PRId64 "", r->data_default.rec_int);
      break;
    case RECD_FLOAT:
      snprintf(*buf, 1023, "%f", r->data_default.rec_float);
      break;
    case RECD_STRING:
      if (r->data_default.rec_string) {
        ink_strlcpy(*buf, r->data_default.rec_string, 1024);
      } else {
        ats_free(*buf);
        *buf = NULL;
      }
      break;
    case RECD_COUNTER:
      snprintf(*buf, 1023, "%" PRId64 "", r->data_default.rec_counter);
      break;
    default:
      ink_assert(!"Unexpected RecD type");
      ats_free(*buf);
      *buf = NULL;
      break;
    }
  } else {
    err = REC_ERR_FAIL;
  }

  if (lock) {
    ink_rwlock_unlock(&g_records_rwlock);
  }

  return err;
}


int
RecGetRecordAccessType(const char *name, RecAccessT *access, bool lock)
{
  int err = REC_ERR_FAIL;
  RecRecord *r = NULL;

  if (lock) {
    ink_rwlock_rdlock(&g_records_rwlock);
  }

  if (ink_hash_table_lookup(g_records_ht, name, (void **) &r)) {
    rec_mutex_acquire(&(r->lock));
    *access = r->config_meta.access_type;
    err = REC_ERR_OKAY;
    rec_mutex_release(&(r->lock));
  }

  if (lock) {
    ink_rwlock_unlock(&g_records_rwlock);
  }

  return err;
}


int
RecSetRecordAccessType(const char *name, RecAccessT access, bool lock)
{
  int err = REC_ERR_FAIL;
  RecRecord *r = NULL;

  if (lock) {
    ink_rwlock_rdlock(&g_records_rwlock);
  }

  if (ink_hash_table_lookup(g_records_ht, name, (void **) &r)) {
    rec_mutex_acquire(&(r->lock));
    r->config_meta.access_type = access;
    err = REC_ERR_OKAY;
    rec_mutex_release(&(r->lock));
  }

  if (lock) {
    ink_rwlock_unlock(&g_records_rwlock);
  }

  return err;
}


//-------------------------------------------------------------------------
// RecRegisterStat
//-------------------------------------------------------------------------
RecRecord *
RecRegisterStat(RecT rec_type, const char *name, RecDataT data_type, RecData data_default, RecPersistT persist_type)
{
  RecRecord *r = NULL;

  ink_rwlock_wrlock(&g_records_rwlock);
  if ((r = register_record(rec_type, name, data_type, data_default, persist_type)) != NULL) {
    // If the persistence type we found in the records hash is not the same as the persistence
    // type we are registering, then that means that it changed between the previous software
    // version and the current version. If the metric changed to non-persistent, reset to the
    // new default value.
    if ((r->stat_meta.persist_type == RECP_NULL ||r->stat_meta.persist_type == RECP_PERSISTENT) &&
        persist_type == RECP_NON_PERSISTENT) {
      RecDebug(DL_Debug, "resetting default value for formerly persisted stat '%s'", r->name);
      RecDataSet(r->data_type, &(r->data), &(data_default));
    }

    r->stat_meta.persist_type = persist_type;
  } else {
    ink_assert(!"Can't register record!");
    RecDebug(DL_Warning, "failed to register '%s' record", name);
  }
  ink_rwlock_unlock(&g_records_rwlock);

  return r;
}


//-------------------------------------------------------------------------
// RecRegisterConfig
//-------------------------------------------------------------------------
RecRecord *
RecRegisterConfig(RecT rec_type, const char *name, RecDataT data_type,
                  RecData data_default, RecUpdateT update_type,
                  RecCheckT check_type, const char *check_expr, RecAccessT access_type)
{
  RecRecord *r;
  ink_rwlock_wrlock(&g_records_rwlock);
  if ((r = register_record(rec_type, name, data_type, data_default, RECP_NULL)) != NULL) {
    // Note: do not modify 'record->config_meta.update_required'
    r->config_meta.update_type = update_type;
    r->config_meta.check_type = check_type;
    if (r->config_meta.check_expr) {
      ats_free(r->config_meta.check_expr);
    }
    r->config_meta.check_expr = ats_strdup(check_expr);
    r->config_meta.update_cb_list = NULL;
    r->config_meta.access_type = access_type;
  }
  ink_rwlock_unlock(&g_records_rwlock);

  return r;
}


//-------------------------------------------------------------------------
// RecGetRecord_Xmalloc
//-------------------------------------------------------------------------
int
RecGetRecord_Xmalloc(const char *name, RecDataT data_type, RecData *data, bool lock)
{
  int err = REC_ERR_OKAY;
  RecRecord *r;

  if (lock) {
    ink_rwlock_rdlock(&g_records_rwlock);
  }

  if (ink_hash_table_lookup(g_records_ht, name, (void **) &r)) {
    rec_mutex_acquire(&(r->lock));
    if (!r->registered || (r->data_type != data_type)) {
      err = REC_ERR_FAIL;
    } else {
      // Clear the caller's record just in case it has trash in it.
      // Passing trashy records to RecDataSet will cause confusion.
      memset(data, 0, sizeof(RecData));
      RecDataSet(data_type, data, &(r->data));
    }
    rec_mutex_release(&(r->lock));
  } else {
    err = REC_ERR_FAIL;
  }

  if (lock) {
    ink_rwlock_unlock(&g_records_rwlock);
  }

  return err;
}


//-------------------------------------------------------------------------
// RecForceInsert
//-------------------------------------------------------------------------
RecRecord *
RecForceInsert(RecRecord * record)
{
  RecRecord *r = NULL;
  bool r_is_a_new_record;

  ink_rwlock_wrlock(&g_records_rwlock);

  if (ink_hash_table_lookup(g_records_ht, record->name, (void **) &r)) {
    r_is_a_new_record = false;
    rec_mutex_acquire(&(r->lock));
    r->rec_type = record->rec_type;
    r->data_type = record->data_type;
  } else {
    r_is_a_new_record = true;
    if ((r = RecAlloc(record->rec_type, record->name, record->data_type)) == NULL) {
      ink_rwlock_unlock(&g_records_rwlock);
      return NULL;
    }
  }

  // set the record value
  RecDataSet(r->data_type, &(r->data), &(record->data));
  RecDataSet(r->data_type, &(r->data_default), &(record->data_default));

  r->registered = record->registered;
  r->rsb_id = record->rsb_id;

  if (REC_TYPE_IS_STAT(r->rec_type)) {
    r->stat_meta.persist_type = record->stat_meta.persist_type;
    r->stat_meta.data_raw = record->stat_meta.data_raw;
  } else if (REC_TYPE_IS_CONFIG(r->rec_type)) {
    r->config_meta.update_required = record->config_meta.update_required;
    r->config_meta.update_type = record->config_meta.update_type;
    r->config_meta.check_type = record->config_meta.check_type;
    ats_free(r->config_meta.check_expr);
    r->config_meta.check_expr = ats_strdup(record->config_meta.check_expr);
    r->config_meta.access_type = record->config_meta.access_type;
  }

  if (r_is_a_new_record) {
    ink_hash_table_insert(g_records_ht, r->name, (void *) r);
  } else {
    rec_mutex_release(&(r->lock));
  }

  ink_rwlock_unlock(&g_records_rwlock);

  return r;
}


//-------------------------------------------------------------------------
// RecDumpRecordsHt
//-------------------------------------------------------------------------

static void
debug_record_callback(RecT /* rec_type */, void * /* edata */, int registered, const char *name, int data_type, RecData *datum)
{
  switch(data_type) {
  case RECD_INT:
    RecDebug(DL_Note, "  ([%d] '%s', '%" PRId64 "')", registered, name, datum->rec_int);
    break;
  case RECD_FLOAT:
    RecDebug(DL_Note, "  ([%d] '%s', '%f')", registered, name, datum->rec_float);
    break;
  case RECD_STRING:
    RecDebug(DL_Note, "  ([%d] '%s', '%s')",
             registered, name, datum->rec_string ? datum->rec_string : "NULL");
    break;
  case RECD_COUNTER:
    RecDebug(DL_Note, "  ([%d] '%s', '%" PRId64 "')", registered, name, datum->rec_counter);
    break;
  default:
    RecDebug(DL_Note, "  ([%d] '%s', <? ? ?>)", registered, name);
    break;
  }
}
void
RecDumpRecords(RecT rec_type, RecDumpEntryCb callback, void *edata)
{
  int i, num_records;

  num_records = g_num_records;
  for (i = 0; i < num_records; i++) {
    RecRecord *r = &(g_records[i]);
    if ((rec_type == RECT_NULL) || (rec_type & r->rec_type)) {
      rec_mutex_acquire(&(r->lock));
      callback(rec_type, edata, r->registered, r->name, r->data_type, &r->data);
      rec_mutex_release(&(r->lock));
    }
  }
}

void
RecDumpRecordsHt(RecT rec_type) {
  RecDebug(DL_Note, "Dumping Records:");
  RecDumpRecords(rec_type, debug_record_callback, NULL);
}

void
RecGetRecordTree(char *subtree)
{
  RecTree *tree = g_records_tree;
  if (subtree) {
    tree = tree->rec_tree_get(subtree);
  }
  tree->print();
}

void
RecGetRecordList(char *var, char ***buffer, int *count)
{
  g_records_tree->rec_tree_get_list(&(*var), &(*buffer), &(*count));
}


//-------------------------------------------------------------------------
// RecGetRecordPrefix_Xmalloc
//
//     mimics/replaces proxy/StatSystem.cc::stat_callback().
//     returns the number of variables name match the prefix.
//-------------------------------------------------------------------------
int
RecGetRecordPrefix_Xmalloc(char *prefix, char **buf, int *buf_len)
{
  int num_records = g_num_records;
  int result_size = num_records * 256;  /* estimate buffer size */
  int num_matched = 0;
  char *result = NULL;

  result = (char *)ats_malloc(result_size * sizeof(char));
  memset(result, 0, result_size * sizeof(char));

  int i;
  int total_bytes_written = 0;
  int error = 0;
  for (i = 0; !error && i < num_records; i++) {
    int bytes_written = 0;
    int bytes_avail = result_size - total_bytes_written;
    RecRecord *r = &(g_records[i]);
    if (strncmp(prefix, r->name, strlen(prefix)) == 0) {
      rec_mutex_acquire(&(r->lock));
      switch (r->data_type) {
      case RECD_INT:
        num_matched++;
        bytes_written = snprintf(result + total_bytes_written, bytes_avail, "%s=%" PRId64 "\r\n", r->name, r->data.rec_int);
        break;
      case RECD_FLOAT:
        num_matched++;
        bytes_written = snprintf(result + total_bytes_written, bytes_avail, "%s=%f\r\n", r->name, r->data.rec_float);
        break;
      case RECD_STRING:
        num_matched++;
        bytes_written = snprintf(result + total_bytes_written, bytes_avail, "%s=%s\r\n", r->name, r->data.rec_string ? r->data.rec_string : "NULL");
        break;
      case RECD_COUNTER:
        num_matched++;
        bytes_written = snprintf(result + total_bytes_written, bytes_avail, "%s=%" PRId64 "\r\n", r->name, r->data.rec_int);
        break;
      default:
        break;
      }

      if(bytes_written <= 0 || bytes_written > bytes_avail) {
        error = 1;
      } else
        total_bytes_written += bytes_written;

      rec_mutex_release(&(r->lock));
    }
  }

  if(error || total_bytes_written == result_size) {
    RecLog(DL_Error, "Stat system was unable to fully generate stat list, size exceeded limit of %d", result_size);
  }

  *buf = result;
  *buf_len = strlen(result);

  return num_matched;
}


//-------------------------------------------------------------------------
// Backwards compatibility ... TODO: Should eliminate these
//-------------------------------------------------------------------------
RecInt
REC_ConfigReadInteger(const char *name)
{
  RecInt t = 0;
  RecGetRecordInt(name, &t);
  return t;
}

char *
REC_ConfigReadString(const char *name)
{
  char *t = 0;
  RecGetRecordString_Xmalloc(name, (RecString *) & t);
  return t;
}

RecFloat
REC_ConfigReadFloat(const char *name)
{
  RecFloat t = 0;
  RecGetRecordFloat(name, (RecFloat *) & t);
  return t;
}

RecCounter
REC_ConfigReadCounter(const char *name)
{
  RecCounter t = 0;
  RecGetRecordCounter(name, (RecCounter *) & t);
  return t;
}


//-------------------------------------------------------------------------
// Backwards compatibility. TODO: Should remove these.
//-------------------------------------------------------------------------
RecInt
REC_readInteger(const char *name, bool * found, bool lock)
{
  ink_assert(name);
  RecInt _tmp = 0;
  bool _found;
  _found = (RecGetRecordInt(name, &_tmp, lock) == REC_ERR_OKAY);
  if (found)
    *found = _found;
  return _tmp;
}

RecFloat
REC_readFloat(char *name, bool * found, bool lock)
{
  ink_assert(name);
  RecFloat _tmp = 0.0;
  bool _found;
  _found = (RecGetRecordFloat(name, &_tmp, lock) == REC_ERR_OKAY);
  if (found)
    *found = _found;
  return _tmp;
}

RecCounter
REC_readCounter(char *name, bool * found, bool lock)
{
  ink_assert(name);
  RecCounter _tmp = 0;
  bool _found;
  _found = (RecGetRecordCounter(name, &_tmp, lock) == REC_ERR_OKAY);
  if (found)
    *found = _found;
  return _tmp;
}

RecString
REC_readString(const char *name, bool * found, bool lock)
{
  ink_assert(name);
  RecString _tmp = NULL;
  bool _found;
  _found = (RecGetRecordString_Xmalloc(name, &_tmp, lock) == REC_ERR_OKAY);
  if (found)
    *found = _found;
  return _tmp;
}

//-------------------------------------------------------------------------
// RecConfigReadRuntimeDir
//-------------------------------------------------------------------------
char *
RecConfigReadRuntimeDir()
{
  char buf[PATH_NAME_MAX + 1];

  buf[0] = '\0';
  RecGetRecordString("proxy.config.local_state_dir", buf, PATH_NAME_MAX);
  if (strlen(buf) > 0) {
    return Layout::get()->relative(buf);
  } else {
    return ats_strdup(Layout::get()->runtimedir);
  }
}

//-------------------------------------------------------------------------
// RecConfigReadLogDir
//-------------------------------------------------------------------------
char *
RecConfigReadLogDir()
{
  char buf[PATH_NAME_MAX + 1];

  buf[0] = '\0';
  RecGetRecordString("proxy.config.log.logfile_dir", buf, PATH_NAME_MAX);
  if (strlen(buf) > 0) {
    return Layout::get()->relative(buf);
  } else {
    return ats_strdup(Layout::get()->logdir);
  }
}

//-------------------------------------------------------------------------
// RecConfigReadBinDir
//-------------------------------------------------------------------------
char *
RecConfigReadBinDir()
{
  char buf[PATH_NAME_MAX + 1];

  buf[0] = '\0';
  RecGetRecordString("proxy.config.bin_path", buf, PATH_NAME_MAX);
  if (strlen(buf) > 0) {
    return Layout::get()->relative(buf);
  } else {
    return ats_strdup(Layout::get()->bindir);
  }
}

//-------------------------------------------------------------------------
// RecConfigReadSnapshotDir.
//-------------------------------------------------------------------------
char *
RecConfigReadSnapshotDir()
{
  char buf[PATH_NAME_MAX + 1];

  buf[0] = '\0';
  RecGetRecordString("proxy.config.snapshot_dir", buf, PATH_NAME_MAX);
  if (strlen(buf) > 0) {
    return Layout::get()->relative_to(Layout::get()->sysconfdir, buf);
  } else {
    return Layout::get()->relative_to(Layout::get()->sysconfdir, "snapshots");
  }
}

//-------------------------------------------------------------------------
// RecConfigReadConfigPath
//-------------------------------------------------------------------------
char *
RecConfigReadConfigPath(const char * file_variable, const char * default_value)
{
  char buf[PATH_NAME_MAX + 1];

  buf[0] = '\0';
  RecGetRecordString(file_variable, buf, PATH_NAME_MAX);
  if (strlen(buf) > 0) {
    return Layout::get()->relative_to(Layout::get()->sysconfdir, buf);
  }

  if (default_value) {
    return Layout::get()->relative_to(Layout::get()->sysconfdir, default_value);
  }

  return NULL;
}

//-------------------------------------------------------------------------
// RecConfigReadPrefixPath
//-------------------------------------------------------------------------
char *
RecConfigReadPrefixPath(const char * file_variable, const char * default_value)
{
  char buf[PATH_NAME_MAX + 1];

  buf[0] = '\0';
  RecGetRecordString(file_variable, buf, PATH_NAME_MAX);
  if (strlen(buf) > 0) {
    return Layout::get()->relative_to(Layout::get()->prefix, buf);
  }

  if (default_value) {
    return Layout::get()->relative_to(Layout::get()->prefix, default_value);
  }

  return NULL;
}

//-------------------------------------------------------------------------
// RecConfigReadPersistentStatsPath
//-------------------------------------------------------------------------
char *
RecConfigReadPersistentStatsPath()
{
  ats_scoped_str rundir(RecConfigReadRuntimeDir());
  return Layout::relative_to(rundir, REC_RAW_STATS_FILE);
}

void
RecSignalWarning(int sig, const char * fmt, ...)
{
  char msg[1024];
  va_list args;

  va_start(args, fmt);
  WarningV(fmt, args);
  va_end(args);

  va_start(args, fmt);
  vsnprintf(msg, sizeof(msg), fmt, args);
  RecSignalManager(sig, msg);
  va_end(args);
}
