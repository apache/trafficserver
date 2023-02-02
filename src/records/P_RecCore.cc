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

#include "tscore/ink_platform.h"
#include "tscore/ink_memory.h"

#include "tscore/TextBuffer.h"
#include "tscore/Tokenizer.h"
#include "tscore/ink_defs.h"
#include "tscore/ink_string.h"

#include "records/P_RecFile.h"
#include "records/P_RecUtils.h"
#include "records/P_RecMessage.h"
#include "records/P_RecCore.h"
#include "records/RecYAMLDecoder.h"

#include "swoc/bwf_std.h"

#include <fstream>

RecModeT g_mode_type = RECM_NULL;

//-------------------------------------------------------------------------
// RecRegisterStatXXX
//-------------------------------------------------------------------------
#define REC_REGISTER_STAT_XXX(A, B)                                                                                           \
  ink_assert((rec_type == RECT_NODE) || (rec_type == RECT_PROCESS) || (rec_type == RECT_LOCAL) || (rec_type == RECT_PLUGIN)); \
  RecRecord *r;                                                                                                               \
  RecData my_data_default;                                                                                                    \
  my_data_default.A = data_default;                                                                                           \
  if ((r = RecRegisterStat(rec_type, name, B, my_data_default, persist_type)) != nullptr) {                                   \
    if (i_am_the_record_owner(r->rec_type)) {                                                                                 \
      r->sync_required = r->sync_required | REC_PEER_SYNC_REQUIRED;                                                           \
    }                                                                                                                         \
    return REC_ERR_OKAY;                                                                                                      \
  } else {                                                                                                                    \
    return REC_ERR_FAIL;                                                                                                      \
  }

RecErrT
_RecRegisterStatInt(RecT rec_type, const char *name, RecInt data_default, RecPersistT persist_type)
{
  REC_REGISTER_STAT_XXX(rec_int, RECD_INT);
}

RecErrT
_RecRegisterStatFloat(RecT rec_type, const char *name, RecFloat data_default, RecPersistT persist_type)
{
  REC_REGISTER_STAT_XXX(rec_float, RECD_FLOAT);
}

RecErrT
_RecRegisterStatString(RecT rec_type, const char *name, RecString data_default, RecPersistT persist_type)
{
  REC_REGISTER_STAT_XXX(rec_string, RECD_STRING);
}

RecErrT
_RecRegisterStatCounter(RecT rec_type, const char *name, RecCounter data_default, RecPersistT persist_type)
{
  REC_REGISTER_STAT_XXX(rec_counter, RECD_COUNTER);
}

//-------------------------------------------------------------------------
// RecRegisterConfigXXX
//-------------------------------------------------------------------------
#define REC_REGISTER_CONFIG_XXX(A, B)                                                                                           \
  RecRecord *r;                                                                                                                 \
  RecData my_data_default;                                                                                                      \
  my_data_default.A = data_default;                                                                                             \
  if ((r = RecRegisterConfig(rec_type, name, B, my_data_default, update_type, check_type, check_regex, source, access_type)) != \
      nullptr) {                                                                                                                \
    if (i_am_the_record_owner(r->rec_type)) {                                                                                   \
      r->sync_required = r->sync_required | REC_PEER_SYNC_REQUIRED;                                                             \
    }                                                                                                                           \
    return REC_ERR_OKAY;                                                                                                        \
  } else {                                                                                                                      \
    return REC_ERR_FAIL;                                                                                                        \
  }

RecErrT
RecRegisterConfigInt(RecT rec_type, const char *name, RecInt data_default, RecUpdateT update_type, RecCheckT check_type,
                     const char *check_regex, RecSourceT source, RecAccessT access_type)
{
  ink_assert((rec_type == RECT_CONFIG) || (rec_type == RECT_LOCAL));
  REC_REGISTER_CONFIG_XXX(rec_int, RECD_INT);
}

RecErrT
RecRegisterConfigFloat(RecT rec_type, const char *name, RecFloat data_default, RecUpdateT update_type, RecCheckT check_type,
                       const char *check_regex, RecSourceT source, RecAccessT access_type)
{
  ink_assert((rec_type == RECT_CONFIG) || (rec_type == RECT_LOCAL));
  REC_REGISTER_CONFIG_XXX(rec_float, RECD_FLOAT);
}

RecErrT
RecRegisterConfigString(RecT rec_type, const char *name, const char *data_default_tmp, RecUpdateT update_type, RecCheckT check_type,
                        const char *check_regex, RecSourceT source, RecAccessT access_type)
{
  RecString data_default = const_cast<RecString>(data_default_tmp);
  ink_assert((rec_type == RECT_CONFIG) || (rec_type == RECT_LOCAL));
  REC_REGISTER_CONFIG_XXX(rec_string, RECD_STRING);
}

RecErrT
RecRegisterConfigCounter(RecT rec_type, const char *name, RecCounter data_default, RecUpdateT update_type, RecCheckT check_type,
                         const char *check_regex, RecSourceT source, RecAccessT access_type)
{
  ink_assert((rec_type == RECT_CONFIG) || (rec_type == RECT_LOCAL));
  REC_REGISTER_CONFIG_XXX(rec_counter, RECD_COUNTER);
}

//-------------------------------------------------------------------------
// RecSetRecordXXX
//-------------------------------------------------------------------------
RecErrT
RecSetRecord(RecT rec_type, const char *name, RecDataT data_type, RecData *data, RecRawStat *data_raw, RecSourceT source, bool lock)
{
  RecErrT err = REC_ERR_OKAY;
  RecRecord *r1;

  // FIXME: Most of the time we set, we don't actually need to wrlock
  // since we are not modifying the g_records_ht.
  if (lock) {
    ink_rwlock_wrlock(&g_records_rwlock);
  }
  if (auto it = g_records_ht.find(name); it != g_records_ht.end()) {
    r1 = it->second;
    if (i_am_the_record_owner(r1->rec_type)) {
      rec_mutex_acquire(&(r1->lock));
      if ((data_type != RECD_NULL) && (r1->data_type != data_type)) {
        err = REC_ERR_FAIL;
      } else {
        bool rec_updated_p = false;
        if (data_type == RECD_NULL) {
          // If the caller didn't know the data type, they gave us a string
          // and we should convert based on the record's data type.
          ink_release_assert(data->rec_string != nullptr);
          rec_updated_p = RecDataSetFromString(r1->data_type, &(r1->data), data->rec_string);
        } else {
          rec_updated_p = RecDataSet(data_type, &(r1->data), data);
        }

        if (rec_updated_p) {
          r1->sync_required = REC_SYNC_REQUIRED;
          if (REC_TYPE_IS_CONFIG(r1->rec_type)) {
            r1->config_meta.update_required = REC_UPDATE_REQUIRED;
          }
        }

        if (REC_TYPE_IS_STAT(r1->rec_type) && (data_raw != nullptr)) {
          r1->stat_meta.data_raw = *data_raw;
        } else if (REC_TYPE_IS_CONFIG(r1->rec_type)) {
          r1->config_meta.source = source;
        }
      }
      rec_mutex_release(&(r1->lock));
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
    if (REC_TYPE_IS_STAT(r1->rec_type) && (data_raw != nullptr)) {
      r1->stat_meta.data_raw = *data_raw;
    } else if (REC_TYPE_IS_CONFIG(r1->rec_type)) {
      r1->config_meta.source = source;
    }
    if (i_am_the_record_owner(r1->rec_type)) {
      r1->sync_required = r1->sync_required | REC_PEER_SYNC_REQUIRED;
    }
    // else, error if  from rec_type?
    g_records_ht.emplace(name, r1);
  }

Ldone:
  if (lock) {
    ink_rwlock_unlock(&g_records_rwlock);
  }

  return err;
}

RecErrT
RecSetRecordConvert(const char *name, const RecString rec_string, RecSourceT source, bool lock)
{
  RecData data;
  data.rec_string = rec_string;
  return RecSetRecord(RECT_NULL, name, RECD_NULL, &data, nullptr, source, lock);
}

RecErrT
RecSetRecordInt(const char *name, RecInt rec_int, RecSourceT source, bool lock)
{
  RecData data;
  data.rec_int = rec_int;
  return RecSetRecord(RECT_NULL, name, RECD_INT, &data, nullptr, source, lock);
}

RecErrT
RecSetRecordFloat(const char *name, RecFloat rec_float, RecSourceT source, bool lock)
{
  RecData data;
  data.rec_float = rec_float;
  return RecSetRecord(RECT_NULL, name, RECD_FLOAT, &data, nullptr, source, lock);
}

RecErrT
RecSetRecordString(const char *name, const RecString rec_string, RecSourceT source, bool lock)
{
  RecData data;
  data.rec_string = rec_string;
  return RecSetRecord(RECT_NULL, name, RECD_STRING, &data, nullptr, source, lock);
}

RecErrT
RecSetRecordCounter(const char *name, RecCounter rec_counter, RecSourceT source, bool lock)
{
  RecData data;
  data.rec_counter = rec_counter;
  return RecSetRecord(RECT_NULL, name, RECD_COUNTER, &data, nullptr, source, lock);
}

// check the version of the snap file to remove records.snap or not
static void
CheckSnapFileVersion(const char *path)
{
  std::ifstream f(path, std::ios::binary);
  if (f.good()) {
    // get version, compare and remove
    char data[VERSION_HDR_SIZE];
    if (!f.read(data, VERSION_HDR_SIZE)) {
      return;
    }
    if (data[0] != 'V' || data[1] != PACKAGE_VERSION[0] || data[2] != PACKAGE_VERSION[2] || data[3] != PACKAGE_VERSION[4] ||
        data[4] != '\0') {
      // not the right version found
      if (remove(path) != 0) {
        ink_warning("unable to remove incompatible snap file '%s'", path);
      }
    }
  }
}

//-------------------------------------------------------------------------
// RecReadStatsFile
//-------------------------------------------------------------------------
RecErrT
RecReadStatsFile()
{
  RecRecord *r;
  RecMessage *m;
  RecMessageItr itr;
  RecPersistT persist_type = RECP_NULL;
  ats_scoped_str snap_fpath(RecConfigReadPersistentStatsPath());

  // lock our hash table
  ink_rwlock_wrlock(&g_records_rwlock);

  CheckSnapFileVersion(snap_fpath);

  if ((m = RecMessageReadFromDisk(snap_fpath)) != nullptr) {
    if (RecMessageUnmarshalFirst(m, &itr, &r) != REC_ERR_FAIL) {
      do {
        if ((r->name == nullptr) || (!strlen(r->name))) {
          continue;
        }

        // If we don't have a persistence type for this record, it means that it is not a stat, or it is
        // not registered yet. Either way, it's ok to just set the persisted value and keep going.
        if (RecGetRecordPersistenceType(r->name, &persist_type, false /* lock */) != REC_ERR_OKAY) {
          RecDebug(DL_Debug, "restoring value for persisted stat '%s'", r->name);
          RecSetRecord(r->rec_type, r->name, r->data_type, &(r->data), &(r->stat_meta.data_raw), REC_SOURCE_EXPLICIT, false);
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
        RecSetRecord(r->rec_type, r->name, r->data_type, &(r->data), &(r->stat_meta.data_raw), REC_SOURCE_EXPLICIT, false);
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
RecErrT
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
    m            = RecMessageAlloc(RECG_NULL);
    num_records  = g_num_records;
    sync_to_disk = false;
    for (i = 0; i < num_records; i++) {
      r = &(g_records[i]);
      rec_mutex_acquire(&(r->lock));
      if (REC_TYPE_IS_STAT(r->rec_type)) {
        if (r->stat_meta.persist_type == RECP_PERSISTENT) {
          m            = RecMessageMarshal_Realloc(m, r);
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
RecConsumeConfigEntry(RecT rec_type, RecDataT data_type, const char *name, const char *value, RecSourceT source)
{
  RecData data;

  memset(&data, 0, sizeof(RecData));
  RecDataSetFromString(data_type, &data, value);
  RecSetRecord(rec_type, name, data_type, &data, nullptr, source, false);
  RecDataZero(data_type, &data);
}

//-------------------------------------------------------------------------
// RecReadConfigFile
//-------------------------------------------------------------------------
RecErrT
RecReadConfigFile()
{
  RecDebug(DL_Note, "Reading '%s'", g_rec_config_fpath);

  // lock our hash table
  ink_rwlock_wrlock(&g_records_rwlock);

  // Parse the actual file and hash the values.
  RecConfigFileParse(g_rec_config_fpath, RecConsumeConfigEntry);

  // release our hash table
  ink_rwlock_unlock(&g_records_rwlock);

  return REC_ERR_OKAY;
}

swoc::Errata
RecReadYamlConfigFile()
{
  RecDebug(DL_Debug, "Reading '%s'", g_rec_config_fpath);

  // lock our hash table
  ink_rwlock_wrlock(&g_records_rwlock); // review this lock maybe it should be done inside the API

  // Parse the actual file and hash the values.
  auto ret = RecYAMLConfigFileParse(g_rec_config_fpath, SetRecordFromYAMLNode);

  ink_rwlock_unlock(&g_records_rwlock);
  return ret;
}

//-------------------------------------------------------------------------
// RecExecConfigUpdateCbs
//-------------------------------------------------------------------------
RecUpdateT
RecExecConfigUpdateCbs(unsigned int update_required_type)
{
  RecRecord *r;
  int i, num_records;
  RecUpdateT update_type = RECU_NULL;

  ink_rwlock_rdlock(&g_records_rwlock);

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

      if (r->config_meta.update_required) {
        if (r->config_meta.update_type > update_type) {
          update_type = r->config_meta.update_type;
        }
      }

      if ((r->config_meta.update_required & update_required_type) && (r->config_meta.update_cb_list)) {
        RecConfigUpdateCbList *cur_callback = nullptr;
        for (cur_callback = r->config_meta.update_cb_list; cur_callback; cur_callback = cur_callback->next) {
          (*(cur_callback->update_cb))(r->name, r->data_type, r->data, cur_callback->update_cookie);
        }
        r->config_meta.update_required = r->config_meta.update_required & ~update_required_type;
      }
    }
    rec_mutex_release(&(r->lock));
  }

  ink_rwlock_unlock(&g_records_rwlock);

  return update_type;
}

static RecErrT
reset_stat_record(RecRecord *rec)
{
  RecErrT err = REC_ERR_FAIL;

  if (i_am_the_record_owner(rec->rec_type)) {
    rec_mutex_acquire(&(rec->lock));
    ++(rec->version);
    err = RecDataSet(rec->data_type, &(rec->data), &(rec->data_default)) ? REC_ERR_OKAY : REC_ERR_FAIL;
    rec_mutex_release(&(rec->lock));
  }

  return err;
}

//------------------------------------------------------------------------
// RecResetStatRecord
//------------------------------------------------------------------------
RecErrT
RecResetStatRecord(const char *name)
{
  RecErrT err = REC_ERR_FAIL;

  if (auto it = g_records_ht.find(name); it != g_records_ht.end()) {
    err = reset_stat_record(it->second);
  }

  return err;
}

//------------------------------------------------------------------------
// RecResetStatRecord
//------------------------------------------------------------------------
RecErrT
RecResetStatRecord(RecT type, bool all)
{
  int i, num_records;
  RecErrT err = REC_ERR_OKAY;

  RecDebug(DL_Note, "Reset Statistics Records");

  num_records = g_num_records;
  for (i = 0; i < num_records; i++) {
    RecRecord *r1 = &(g_records[i]);

    if (!REC_TYPE_IS_STAT(r1->rec_type)) {
      continue;
    }

    if (r1->data_type == RECD_STRING) {
      continue;
    }

    if (((type == RECT_NULL) || (r1->rec_type == type)) && (all || (r1->stat_meta.persist_type != RECP_NON_PERSISTENT))) {
      if (reset_stat_record(r1) != REC_ERR_OKAY) {
        err = REC_ERR_FAIL;
      }
    }
  }

  return err;
}

RecErrT
RecSetSyncRequired(char *name, bool lock)
{
  RecErrT err = REC_ERR_FAIL;
  RecRecord *r1;

  // FIXME: Most of the time we set, we don't actually need to wrlock
  // since we are not modifying the g_records_ht.
  if (lock) {
    ink_rwlock_wrlock(&g_records_rwlock);
  }
  if (auto it = g_records_ht.find(name); it != g_records_ht.end()) {
    r1 = it->second;
    if (i_am_the_record_owner(r1->rec_type)) {
      rec_mutex_acquire(&(r1->lock));
      r1->sync_required = REC_PEER_SYNC_REQUIRED;
      if (REC_TYPE_IS_CONFIG(r1->rec_type)) {
        r1->config_meta.update_required = REC_UPDATE_REQUIRED;
      }
      rec_mutex_release(&(r1->lock));
      err = REC_ERR_OKAY;
    }
  }

  if (lock) {
    ink_rwlock_unlock(&g_records_rwlock);
  }

  return err;
}
