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

#include "inktomi++.h"

#include "P_RecCompatibility.h"
#include "P_RecCore.h"
#include "P_RecUtils.h"
#include "P_RecTree.h"

static bool g_initialized = false;

Diags *g_diags = NULL;

RecRecord *g_records = NULL;
InkHashTable *g_records_ht = NULL;
ink_rwlock g_records_rwlock;
int g_num_records = 0;

const char *g_rec_config_fpath = NULL;
LLQ *g_rec_config_contents_llq = NULL;
InkHashTable *g_rec_config_contents_ht = NULL;
ink_mutex g_rec_config_lock;

const char *g_stats_snap_fpath = NULL;
int g_num_update[RECT_MAX];

RecTree *g_records_tree = NULL;

int g_type_records[RECT_MAX][REC_MAX_RECORDS];
int g_type_num_records[RECT_MAX];

//-------------------------------------------------------------------------
// register_record
//-------------------------------------------------------------------------

static RecRecord *
register_record(RecT rec_type, const char *name, RecDataT data_type, RecData data_default, bool release_record_lock)
{

  RecRecord *r = NULL;

  if (ink_hash_table_lookup(g_records_ht, name, (void **) &r)) {

    rec_mutex_acquire(&(r->lock));
    ink_release_assert(r->rec_type == rec_type);
    ink_release_assert(r->data_type == data_type);
    // Note: do not set r->data as we want to keep the previous value
    RecDataSet(r->data_type, &(r->data_default), &(data_default));

  } else {

    if ((r = RecAlloc(rec_type, name, data_type)) == NULL) {
      return NULL;
    }
    rec_mutex_acquire(&(r->lock));
    // Set the r->data to its default value as this is a new record
    RecDataSet(r->data_type, &(r->data), &(data_default));
    RecDataSet(r->data_type, &(r->data_default), &(data_default));
    ink_hash_table_insert(g_records_ht, name, (void *) r);

  }

  // we're not registered
  r->registered = true;

  if (release_record_lock) {
    rec_mutex_release(&(r->lock));
  }

  return r;

}

//-------------------------------------------------------------------------
// link_XXX
//-------------------------------------------------------------------------

static int
link_int(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  REC_NOWARN_UNUSED(name);
  REC_NOWARN_UNUSED(data_type);
  RecInt *rec_int = (RecInt *) cookie;
  ink_atomic_swap64(rec_int, data.rec_int);
  return REC_ERR_OKAY;
}

static int
link_llong(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  REC_NOWARN_UNUSED(name);
  REC_NOWARN_UNUSED(data_type);
  RecLLong *rec_llong = (RecLLong *) cookie;
  ink_atomic_swap64(rec_llong, data.rec_llong);
  return REC_ERR_OKAY;
}

static int
link_ink32(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  REC_NOWARN_UNUSED(name);
  REC_NOWARN_UNUSED(data_type);
  *(ink32 *) cookie = (ink32) data.rec_int;
  return REC_ERR_OKAY;
}

static int
link_inku32(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  REC_NOWARN_UNUSED(name);
  REC_NOWARN_UNUSED(data_type);
  *(inku32 *) cookie = (inku32) data.rec_int;
  return REC_ERR_OKAY;
}

static int
link_float(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  REC_NOWARN_UNUSED(name);
  REC_NOWARN_UNUSED(data_type);
  *(int *) cookie = *((int *) &(data.rec_float));
  return REC_ERR_OKAY;
}

static int
link_counter(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  REC_NOWARN_UNUSED(name);
  REC_NOWARN_UNUSED(data_type);
  RecCounter *rec_counter = (RecInt *) cookie;
  ink_atomic_swap64(rec_counter, data.rec_counter);
  return REC_ERR_OKAY;
}

// mimic Config.cc::config_string_alloc_cb
static int
link_string_alloc(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  REC_NOWARN_UNUSED(name);
  REC_NOWARN_UNUSED(data_type);

  RecString _ss = (RecString) cookie;
  RecString _new_value = 0;

  int len = -1;
  if (_ss) {
    len = strlen(_ss);
    _new_value = (RecString) xmalloc(len + 1);
    memcpy(_new_value, _ss, len + 1);
  }

  RecString _temp2 = data.rec_string;
  data.rec_string = _new_value;

  if (_temp2 != 0) {
    xfree(_temp2);
  }

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
  ink_atomic_swap_ptr(&g_diags, _diags);

  g_records_tree = new RecTree(NULL);

  // initialize record array
  g_records = (RecRecord *) xmalloc(REC_MAX_RECORDS * sizeof(RecRecord));
  memset(g_records, 0, REC_MAX_RECORDS * sizeof(RecRecord));
  g_num_records = 0;

  // initialize record hash index
  g_records_ht = ink_hash_table_create(InkHashTableKeyType_String);
  ink_rwlock_init(&g_records_rwlock);
  if (!g_records_ht) {
    return REC_ERR_FAIL;
  }
  // read stats
  if ((mode_type == RECM_SERVER) || (mode_type == RECM_STAND_ALONE)) {
    g_stats_snap_fpath = REC_RAW_STATS_DIR REC_RAW_STATS_FILE;
    RecReadStatsFile();
  }
  // read configs  
  if ((mode_type == RECM_SERVER) || (mode_type == RECM_STAND_ALONE)) {
    ink_mutex_init(&g_rec_config_lock, NULL);
    g_rec_config_contents_llq = create_queue();
    g_rec_config_contents_ht = ink_hash_table_create(InkHashTableKeyType_String);
    // Import the file into memory; try the following in this order:
    // ./etc/trafficserver/records.config.shadow
    // ./records.config.shadow
    // ./etc/trafficserver/records.config
    // ./records.config
    bool file_exists = true;
    g_rec_config_fpath = REC_CONFIG_DIR REC_CONFIG_FILE REC_SHADOW_EXT;
    if (RecFileExists(g_rec_config_fpath) == REC_ERR_FAIL) {
      g_rec_config_fpath = REC_CONFIG_FILE REC_SHADOW_EXT;
      if (RecFileExists(g_rec_config_fpath) == REC_ERR_FAIL) {
        g_rec_config_fpath = REC_CONFIG_DIR REC_CONFIG_FILE;
        if (RecFileExists(g_rec_config_fpath) == REC_ERR_FAIL) {
          g_rec_config_fpath = REC_CONFIG_FILE;
          if (RecFileExists(g_rec_config_fpath) == REC_ERR_FAIL) {
            RecLog(DL_Warning, "Could not find '%s', system will run with defaults\n", REC_CONFIG_FILE);
            file_exists = false;
          }
        }
      }
    }
    if (file_exists) {
      RecReadConfigFile();
    }
  }

  for (int i = 0; i < RECT_MAX; i++) {
    g_num_update[i] = 0;
    g_type_num_records[i] = 0;
    for (int j = 0; j < REC_MAX_RECORDS; j++) {
      g_type_records[i][j] = -1;
    }
  }

  g_initialized = true;

  return REC_ERR_OKAY;

}

//-------------------------------------------------------------------------
// RecSetDiags
//-------------------------------------------------------------------------

int
RecSetDiags(Diags * _diags)
{
  // Warning! It's very dangerous to change diags on the fly!  This
  // function only exists so that we can boot-strap TM on startup.
  ink_atomic_swap_ptr(&g_diags, _diags);
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
RecLinkConfigLLong(const char *name, RecLLong * rec_llong)
{
  if (RecGetRecordLLong(name, rec_llong) == REC_ERR_FAIL) {
    return REC_ERR_FAIL;
  }
  return RecRegisterConfigUpdateCb(name, link_llong, (void *) rec_llong);
}

int
RecLinkConfigInk32(const char *name, ink32 * p_ink32)
{
  return RecRegisterConfigUpdateCb(name, link_ink32, (void *) p_ink32);
}

int
RecLinkConfigInkU32(const char *name, inku32 * p_inku32)
{
  return RecRegisterConfigUpdateCb(name, link_inku32, (void *) p_inku32);
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

      RecConfigUpdateCbList *new_callback = (RecConfigUpdateCbList *) xmalloc(sizeof(RecConfigUpdateCbList));
      memset(new_callback, 0, sizeof(RecConfigUpdateCbList));
      new_callback->update_cb = update_cb;
      new_callback->update_cookie = cookie;
      new_callback->next = NULL;

      ink_debug_assert(new_callback);
      if (!r->config_meta.update_cb_list) {
        r->config_meta.update_cb_list = new_callback;
      } else {
        RecConfigUpdateCbList *cur_callback = NULL;
        RecConfigUpdateCbList *prev_callback = NULL;
        for (cur_callback = r->config_meta.update_cb_list; cur_callback; cur_callback = cur_callback->next) {
          prev_callback = cur_callback;
        }
        ink_debug_assert(prev_callback);
        ink_debug_assert(!prev_callback->next);
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
// RecRegisterStatUpdateFunc
//-------------------------------------------------------------------------

int
RecRegisterStatUpdateFunc(char *name, RecStatUpdateFunc update_func, void *cookie)
{

  return RecRegisterRawStatUpdateFunc(name, NULL, 0, update_func, cookie);

}

int
RecRegisterRawStatUpdateFunc(const char *name, RecRawStatBlock * rsb, int id, RecStatUpdateFunc update_func, void *cookie)
{

  int err = REC_ERR_FAIL;
  RecRecord *r;

  ink_rwlock_rdlock(&g_records_rwlock);

  if (ink_hash_table_lookup(g_records_ht, name, (void **) &r)) {
    rec_mutex_acquire(&(r->lock));
    if (REC_TYPE_IS_STAT(r->rec_type)) {
      RecStatUpdateFuncList *new_function = (RecStatUpdateFuncList *) xmalloc(sizeof(RecStatUpdateFuncList));
      memset(new_function, 0, sizeof(RecStatUpdateFuncList));
      new_function->rsb = rsb;
      new_function->id = id;
      new_function->update_func = update_func;
      new_function->update_cookie = cookie;
      new_function->next = NULL;

      ink_debug_assert(new_function);
      if (!r->stat_meta.update_func_list) {
        r->stat_meta.update_func_list = new_function;
      } else {
        RecStatUpdateFuncList *cur_function = NULL;
        RecStatUpdateFuncList *prev_function = NULL;
        for (cur_function = r->stat_meta.update_func_list; cur_function; cur_function = cur_function->next) {
          prev_function = cur_function;
        }
        ink_debug_assert(prev_function);
        ink_debug_assert(!prev_function->next);
        prev_function->next = new_function;
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
RecGetRecordLLong(const char *name, RecLLong * rec_llong, bool lock)
{
  int err;
  RecData data;
  if ((err = RecGetRecord_Xmalloc(name, RECD_LLONG, &data, lock)) == REC_ERR_OKAY)
    *rec_llong = data.rec_llong;
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
        strncpy(buf, r->data.rec_string, buf_len - 1);
        buf[buf_len - 1] = '\0';
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
RecGetRecordGeneric_Xmalloc(const char *name, RecString * rec_string, bool lock)
{
  int err;

  RecDataT data_type = RECD_INT;
  if ((err = RecGetRecordDataType(name, &data_type, lock))
      != REC_ERR_OKAY) {
    return err;
  }

  RecData data;
  if ((err = RecGetRecord_Xmalloc(name, data_type, &data, lock))
      != REC_ERR_OKAY) {
    return err;
  }

  *rec_string = (RecString) xmalloc(sizeof(char) * 1024);
  memset(*rec_string, 0, 1024);
  switch (data_type) {
  case RECD_INT:
    ink_snprintf(*rec_string, 1023, "%lld", data.rec_int);
    break;
  case RECD_LLONG:
    ink_snprintf(*rec_string, 1023, "%lld", data.rec_llong);
    break;
  case RECD_FLOAT:
    ink_snprintf(*rec_string, 1023, "%f", data.rec_float);
    break;
  case RECD_STRING:
    ink_snprintf(*rec_string, 1023, "%s", data.rec_string);
    break;
  case RECD_COUNTER:
    ink_snprintf(*rec_string, 1023, "%lld", data.rec_counter);
    break;
  default:
    return REC_ERR_FAIL;
    break;
  }

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
RecGetRecordUpdateCount(RecT data_type)
{
  return g_num_update[data_type];
}

int
RecGetRecordRelativeOrder(const char *name, int *order, bool lock)
{
  int err = REC_ERR_FAIL;
  RecRecord *r = NULL;

  if (lock) {
    ink_rwlock_rdlock(&g_records_rwlock);
  }

  if (ink_hash_table_lookup(g_records_ht, name, (void **) &r)) {
    rec_mutex_acquire(&(r->lock));
    *order = r->relative_order;
    err = REC_ERR_OKAY;
    rec_mutex_release(&(r->lock));
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
      ink_debug_assert(!"rec_type is not CONFIG");
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
      ink_debug_assert(!"rec_type is not CONFIG");
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
      ink_debug_assert(!"rec_type is not CONFIG");
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

  REC_NOWARN_UNUSED(lock);
  int err;
  RecRecord *r = NULL;

  if (ink_hash_table_lookup(g_records_ht, name, (void **) &r)) {

    *buf = (char *) xmalloc(sizeof(char) * 1024);
    memset(*buf, 0, 1024);
    err = REC_ERR_OKAY;

    switch (r->data_type) {
    case RECD_INT:
      ink_snprintf(*buf, 1023, "%lld", r->data_default.rec_int);
      break;
    case RECD_LLONG:
      ink_snprintf(*buf, 1023, "%lld", r->data_default.rec_llong);
      break;
    case RECD_FLOAT:
      ink_snprintf(*buf, 1023, "%f", r->data_default.rec_float);
      break;
    case RECD_STRING:
      if (r->data_default.rec_string) {
        strncpy(*buf, r->data_default.rec_string, 1023);
        buf[1023] = '\0';
      } else {
        xfree(*buf);
        *buf = NULL;
      }
      break;
    case RECD_COUNTER:
      ink_snprintf(*buf, 1023, "%lld", r->data_default.rec_counter);
      break;
    default:
      ink_debug_assert(!"Unexpected RecD type");
      xfree(*buf);
      *buf = NULL;
      break;
    }
  } else {
    err = REC_ERR_FAIL;
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
  if ((r = register_record(rec_type, name, data_type, data_default, false)) != NULL) {
    r->stat_meta.persist_type = persist_type;
    rec_mutex_release(&(r->lock));
  } else {
    ink_debug_assert(!"Can't register record!");
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
  if ((r = register_record(rec_type, name, data_type, data_default, false)) != NULL) {
    // Note: do not modify 'record->config_meta.update_required'
    r->config_meta.update_type = update_type;
    r->config_meta.check_type = check_type;
    if (r->config_meta.check_expr) {
      xfree(r->config_meta.check_expr);
    }
    r->config_meta.check_expr = xstrdup(check_expr);
    r->config_meta.update_cb_list = NULL;
    r->config_meta.access_type = access_type;
    rec_mutex_release(&(r->lock));
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
  if (REC_TYPE_IS_STAT(r->rec_type)) {
    r->stat_meta.persist_type = record->stat_meta.persist_type;
    r->stat_meta.data_raw = record->stat_meta.data_raw;
  } else if (REC_TYPE_IS_CONFIG(r->rec_type)) {
    r->config_meta.update_required = record->config_meta.update_required;
    r->config_meta.update_type = record->config_meta.update_type;
    r->config_meta.check_type = record->config_meta.check_type;
    if (r->config_meta.check_expr) {
      xfree(r->config_meta.check_expr);
    }
    r->config_meta.check_expr = xstrdup(record->config_meta.check_expr);
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

void
RecDumpRecordsHt(RecT rec_type)
{

  int i, num_records;

  RecDebug(DL_Note, "Dumping Records:");

  num_records = g_num_records;
  for (i = 0; i < num_records; i++) {
    RecRecord *r = &(g_records[i]);
    if ((rec_type == RECT_NULL) || (rec_type == r->rec_type)) {
      rec_mutex_acquire(&(r->lock));
      switch (r->data_type) {
      case RECD_INT:
        RecDebug(DL_Note, "  ([%d] '%s', '%lld')", r->registered, r->name, r->data.rec_int);
        break;
      case RECD_LLONG:
        RecDebug(DL_Note, "  ([%d] '%s', '%lld')", r->registered, r->name, r->data.rec_llong);
        break;
      case RECD_FLOAT:
        RecDebug(DL_Note, "  ([%d] '%s', '%f')", r->registered, r->name, r->data.rec_float);
        break;
      case RECD_STRING:
        RecDebug(DL_Note, "  ([%d] '%s', '%s')",
                 r->registered, r->name, r->data.rec_string ? r->data.rec_string : "NULL");
        break;
      case RECD_COUNTER:
        RecDebug(DL_Note, "  ([%d] '%s', '%lld')", r->registered, r->name, r->data.rec_counter);
        break;
      default:
        RecDebug(DL_Note, "  ([%d] '%s', <? ? ?>)", r->registered, r->name);
        break;
      }
      rec_mutex_release(&(r->lock));
    }
  }

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
  int result_size = num_records * 128;  /* estimate buffer size */
  int num_matched = 0;
  char *result = NULL;

  result = (char *) xmalloc(result_size * sizeof(char));
  memset(result, 0, result_size * sizeof(char));

  int i;
  for (i = 0; i < num_records; i++) {
    RecRecord *r = &(g_records[i]);
    if (strncmp(prefix, r->name, strlen(prefix)) == 0) {
      rec_mutex_acquire(&(r->lock));
      switch (r->data_type) {
      case RECD_INT:
        num_matched++;
        ink_sprintf(&result[strlen(result)], "%s=%lld\r\n", r->name, r->data.rec_int);
        break;
      case RECD_LLONG:
        num_matched++;
        ink_sprintf(&result[strlen(result)], "%s=%lld\r\n", r->name, r->data.rec_llong);
        break;
      case RECD_FLOAT:
        num_matched++;
        ink_sprintf(&result[strlen(result)], "%s=%f\r\n", r->name, r->data.rec_float);
        break;
      case RECD_STRING:
        num_matched++;
        ink_sprintf(&result[strlen(result)], "%s=%s\r\n", r->name, r->data.rec_string ? r->data.rec_string : "NULL");
        break;
      case RECD_COUNTER:
        num_matched++;
        ink_sprintf(&result[strlen(result)], "%s=%lld\r\n", r->name, r->data.rec_int);
        break;
      default:
        break;
      }
      rec_mutex_release(&(r->lock));
    }
  }


  *buf = result;
  *buf_len = strlen(result);

  return num_matched;

}


//-------------------------------------------------------------------------
// REC_ConfigReadInteger (backwards compatibility)
//-------------------------------------------------------------------------

RecInt
REC_ConfigReadInteger(const char *name)
{
  RecInt t = 0;
  RecGetRecordInt(name, &t);
  return t;
}

//-------------------------------------------------------------------------
// REC_ConfigReadLLong (backwards compatibility)
//-------------------------------------------------------------------------

RecLLong
REC_ConfigReadLLong(const char *name)
{
  RecLLong t = 0;
  RecGetRecordLLong(name, &t);
  return t;
}

//-------------------------------------------------------------------------
// REC_ConfigReadString (backwards compatibility)
//-------------------------------------------------------------------------

char *
REC_ConfigReadString(const char *name)
{
  char *t = 0;
  RecGetRecordString_Xmalloc(name, (RecString *) & t);
  return t;
}

//-------------------------------------------------------------------------
// REC_ConfigReadFloat (backwards compatibility)
//-------------------------------------------------------------------------

RecFloat
REC_ConfigReadFloat(const char *name)
{
  RecFloat t = 0;
  RecGetRecordFloat(name, (RecFloat *) & t);
  return t;
}

//-------------------------------------------------------------------------
// REC_ConfigReadCounter (backwards compatibility)
//-------------------------------------------------------------------------

RecCounter
REC_ConfigReadCounter(const char *name)
{
  RecCounter t = 0;
  RecGetRecordCounter(name, (RecCounter *) & t);
  return t;
}


//-------------------------------------------------------------------------
// MGMT2 Marco (backwards compatibility)
//-------------------------------------------------------------------------

RecInt
REC_readInteger(const char *name, bool * found, bool lock)
{
  ink_debug_assert(name);
  RecInt _tmp = 0;
  bool _found;
  _found = (RecGetRecordInt(name, &_tmp, lock) == REC_ERR_OKAY);
  if (found)
    *found = _found;
  return _tmp;
}

RecLLong
REC_readLLong(char *name, bool * found, bool lock)
{
  ink_debug_assert(name);
  RecLLong _tmp = 0L;
  bool _found;
  _found = (RecGetRecordLLong(name, &_tmp, lock) == REC_ERR_OKAY);
  if (found)
    *found = _found;
  return _tmp;
}

RecFloat
REC_readFloat(char *name, bool * found, bool lock)
{
  ink_debug_assert(name);
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
  ink_debug_assert(name);
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
  ink_debug_assert(name);
  RecString _tmp = NULL;
  bool _found;
  _found = (RecGetRecordString_Xmalloc(name, &_tmp, lock) == REC_ERR_OKAY);
  if (found)
    *found = _found;
  return _tmp;
}

// MGMT2 Marco's -- converting lmgmt->record_data->setXXX

bool
REC_setInteger(const char *name, int value, bool dirty)
{
  REC_NOWARN_UNUSED(dirty);
  return RecSetRecordInt(name, value);
}

bool
REC_setLLong(const char *name, RecLLong value, bool dirty)
{
  REC_NOWARN_UNUSED(dirty);
  return RecSetRecordLLong(name, value);
}

bool
REC_setFloat(const char *name, float value, bool dirty)
{
  REC_NOWARN_UNUSED(dirty);
  return RecSetRecordFloat(name, value);
}

bool
REC_setCounter(const char *name, ink64 value, bool dirty)
{
  REC_NOWARN_UNUSED(dirty);
  return RecSetRecordCounter(name, value);
}

bool
REC_setString(const char *name, char *value, bool dirty)
{
  REC_NOWARN_UNUSED(dirty);
  return RecSetRecordString(name, value);
}


//-------------------------------------------------------------------------
// REC_SignalAlarm (TM) & REC_SignalManager (TS)
//-------------------------------------------------------------------------
#if defined (REC_BUILD_MGMT2)

#if defined(LOCAL_MANAGER)

#include "LocalManager.h"

#define RecSignalError(_buf, _already) \
{ \
  if(_already == false) RecSignalManager(REC_SIGNAL_CONFIG_ERROR, _buf); \
  _already = true; \
  RecLog(DL_Warning, _buf); \
}

void
RecSignalAlarm(int id, char *msg)
{
  ink_debug_assert(lmgmt);
  lmgmt->signalAlarm(id, msg);
}

void
RecSignalManager(int id, const char *msg)
{
  REC_NOWARN_UNUSED(id);
  REC_NOWARN_UNUSED(msg);
}

int
RecRegisterManagerCb(int _signal, RecManagerCb _fn, void *_data)
{
  return lmgmt->registerMgmtCallback(_signal, _fn, _data);
}

#elif defined(PROCESS_MANAGER)

#include "ProcessManager.h"

#define RecSignalError(_buf, _already) \
{ \
  if(_already == false) RecSignalManager(REC_ALARM_PROXY_CONFIG_ERROR, _buf); \
  _already = true; \
  RecLog(DL_Error, _buf); \
}

void
RecSignalAlarm(int id, char *msg)
{
  REC_NOWARN_UNUSED(id);
  REC_NOWARN_UNUSED(msg);
}

void
RecSignalManager(int id, const char *msg)
{
  ink_debug_assert(pmgmt);
  pmgmt->signalManager(id, msg);
}

int
RecRegisterManagerCb(int _signal, RecManagerCb _fn, void *_data)
{
  return pmgmt->registerMgmtCallback(_signal, _fn, _data);
}

#endif // LOCAL_MANAGER

#else

#define RecSignalError(_buf, _already) \
{ \
  RecLog(DL_Error, _buf); \
}

void
RecSignalAlarm(int id, char *msg)
{
  REC_NOWARN_UNUSED(id);
  RecLog(DL_Warning, msg);
}

void
RecSignalManager(int id, const char *msg)
{
  REC_NOWARN_UNUSED(id);
  RecLog(DL_Warning, msg);
}

int
RecRegisterManagerCb(int _signal, RecManagerCb _fn, void *_data)
{
  return -1;
}

#endif // REC_BUILD_MGMT2
