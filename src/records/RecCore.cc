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

#include <deque>
#include <utility>
#include <iostream>

#include "swoc/swoc_file.h"

#include "tscore/ink_platform.h"
#include "tscore/ink_memory.h"
#include "tscore/ink_string.h"
#include "tscore/Filenames.h"

#include "records/RecordsConfig.h"
#include "P_RecFile.h"
#include "P_RecCore.h"
#include "P_RecUtils.h"
#include "tscore/Layout.h"
#include "tsutil/ts_errata.h"
#include "tsutil/Metrics.h"

using ts::Metrics;

// This is needed to manage the size of the librecords record. It can't be static, because it needs to be modified
// and used (read) from several binaries / modules.
int                                          max_records_entries = REC_DEFAULT_ELEMENTS_SIZE;
static bool                                  g_initialized       = false;
RecRecord                                   *g_records           = nullptr;
std::unordered_map<std::string, RecRecord *> g_records_ht;
ink_rwlock                                   g_records_rwlock;
int                                          g_num_records = 0;

//-------------------------------------------------------------------------
// register_record
//-------------------------------------------------------------------------
static RecRecord *
register_record(RecT rec_type, const char *name, RecDataT data_type, RecData data_default, RecPersistT persist_type,
                bool *updated_p = nullptr)
{
  RecRecord *r = nullptr;

  // Metrics are restored from persistence before they are registered. In this case, when the registration arrives, we
  // might find that they have changed. For example, a metric might change it's type due to a software upgrade. Records
  // must not flip between config and metrics, but changing within those classes is OK.
  if (auto it = g_records_ht.find(name); it != g_records_ht.end()) {
    r = it->second;
    if (REC_TYPE_IS_STAT(rec_type)) {
      ink_release_assert(REC_TYPE_IS_STAT(r->rec_type));
    }

    if (REC_TYPE_IS_CONFIG(rec_type)) {
      ink_release_assert(REC_TYPE_IS_CONFIG(r->rec_type));
    }

    if (data_type != r->data_type) {
      // Clear with the old type before resetting with the new type.
      RecDataZero(r->data_type, &(r->data));
      RecDataZero(r->data_type, &(r->data_default));

      // If the data type changed, reset the current value to the default.
      RecDataSet(data_type, &(r->data), &(data_default));
    }

    // NOTE: Do not set r->data as we want to keep the previous value because we almost certainly restored a persisted
    // value before the metric was registered.
    RecDataSet(data_type, &(r->data_default), &(data_default));

    r->data_type = data_type;
    r->rec_type  = rec_type;

    if (updated_p) {
      *updated_p = true;
    }
  } else {
    if ((r = RecAlloc(rec_type, name, data_type)) == nullptr) {
      return nullptr;
    }

    // Set the r->data to its default value as this is a new record
    RecDataSet(r->data_type, &(r->data), &(data_default));
    RecDataSet(r->data_type, &(r->data_default), &(data_default));
    g_records_ht.emplace(name, r);

    if (REC_TYPE_IS_STAT(r->rec_type)) {
      r->stat_meta.persist_type = persist_type;
    }

    if (updated_p) {
      *updated_p = false;
    }
  }

  // we're now registered
  r->registered = true;
  r->version    = 0;

  return r;
}

//-------------------------------------------------------------------------
// link_XXX
//-------------------------------------------------------------------------
static int
link_int(const char * /* name */, RecDataT /* data_type */, RecData data, void *cookie)
{
  RecInt *rec_int = static_cast<RecInt *>(cookie);
  ink_atomic_swap(rec_int, data.rec_int);
  return REC_ERR_OKAY;
}

static int
link_int32(const char * /* name */, RecDataT /* data_type */, RecData data, void *cookie)
{
  *(static_cast<int32_t *>(cookie)) = static_cast<int32_t>(data.rec_int);
  return REC_ERR_OKAY;
}

static int
link_uint32(const char * /* name */, RecDataT /* data_type */, RecData data, void *cookie)
{
  *(static_cast<uint32_t *>(cookie)) = static_cast<uint32_t>(data.rec_int);
  return REC_ERR_OKAY;
}

static int
link_float(const char * /* name */, RecDataT /* data_type */, RecData data, void *cookie)
{
  *(static_cast<RecFloat *>(cookie)) = data.rec_float;
  return REC_ERR_OKAY;
}

static int
link_counter(const char * /* name */, RecDataT /* data_type */, RecData data, void *cookie)
{
  RecCounter *rec_counter = static_cast<RecCounter *>(cookie);
  ink_atomic_swap(rec_counter, data.rec_counter);
  return REC_ERR_OKAY;
}

// This is a convenience wrapper, to allow us to treat the RecInt's as a
// 1-byte entity internally.
static int
link_byte(const char * /* name */, RecDataT /* data_type */, RecData data, void *cookie)
{
  RecByte *rec_byte = static_cast<RecByte *>(cookie);
  RecByte  byte     = static_cast<RecByte>(data.rec_int);

  ink_atomic_swap(rec_byte, byte);
  return REC_ERR_OKAY;
}

// mimic Config.cc::config_string_alloc_cb
// cookie e.g. is the DEFAULT_xxx_str value which this function keeps up to date with
// the latest default applied during a config update from records
static int
link_string_alloc(const char * /* name */, RecDataT /* data_type */, RecData data, void *cookie)
{
  RecString _ss        = data.rec_string;
  RecString _new_value = nullptr;

  if (_ss) {
    _new_value = ats_strdup(_ss);
  }

  // set new string for DEFAULT_xxx_str tp point to
  RecString _temp2                    = *(static_cast<RecString *>(cookie));
  *(static_cast<RecString *>(cookie)) = _new_value;
  // free previous string DEFAULT_xxx_str points to
  ats_free(_temp2);

  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecCoreInit
//-------------------------------------------------------------------------
int
RecCoreInit(Diags *_diags)
{
  if (g_initialized) {
    return REC_ERR_OKAY;
  }

  // set our diags
  RecSetDiags(_diags);

  // Initialize config file parsing data structures.
  RecConfigFileInit();

  g_num_records = 0;

  // initialize record array for our internal stats (this can be reallocated later)
  g_records = static_cast<RecRecord *>(ats_malloc(max_records_entries * sizeof(RecRecord)));

  // initialize record rwlock
  ink_rwlock_init(&g_records_rwlock);

  // read stats
  RecReadStatsFile();

  // read configs
  bool file_exists = true;

  ink_mutex_init(&g_rec_config_lock);

  g_rec_config_fpath = ats_stringdup(RecConfigReadConfigPath(nullptr, ts::filename::RECORDS));

  // Make sure there is no legacy file, if so we drop a BIG WARNING and fail.
  // This is to avoid issues with someone ignoring that we now use records.yaml
  swoc::file::path old_config{RecConfigReadConfigPath(nullptr, "records.config")};
  if (swoc::file::is_readable(old_config)) {
    RecLog(DL_Fatal,
           "**** Found a legacy config file (%s). Please remove it and migrate to the new YAML format before continuing. ****",
           old_config.c_str());
  }

  if (RecFileExists(g_rec_config_fpath) == REC_ERR_FAIL) {
    RecLog(DL_Warning, "Could not find '%s', system will run with defaults\n", ts::filename::RECORDS);
    file_exists = false;
  }

  if (file_exists) {
    auto err = RecReadYamlConfigFile();
    RecLog(DL_Note, "records parsing completed.");
    if (!err.empty()) {
      std::string text;
      RecLog(DL_Warning, "%s",
             swoc::bwprint(text, "We have found the following issues when reading the records node:\n {}", err).c_str());
    }
  } else {
    RecLog(DL_Note, "%s does not exist.", g_rec_config_fpath);
  }

  RecLog(DL_Note, "%s finished loading", std::string{g_rec_config_fpath}.c_str());
  g_initialized = true;

  return REC_ERR_OKAY;
}

//-------------------------------------------------------------------------
// RecLinkConfigXXX
//-------------------------------------------------------------------------
RecErrT
RecLinkConfigInt(const char *name, RecInt *rec_int)
{
  auto [tmp, err]{RecGetRecordInt(name)};
  if (err == REC_ERR_FAIL) {
    return REC_ERR_FAIL;
  }
  *rec_int = tmp;
  return RecRegisterConfigUpdateCb(name, link_int, (void *)rec_int);
}

RecErrT
RecLinkConfigInt32(const char *name, int32_t *p_int32)
{
  return RecRegisterConfigUpdateCb(name, link_int32, (void *)p_int32);
}

RecErrT
RecLinkConfigUInt32(const char *name, uint32_t *p_uint32)
{
  return RecRegisterConfigUpdateCb(name, link_uint32, (void *)p_uint32);
}

RecErrT
RecLinkConfigFloat(const char *name, RecFloat *rec_float)
{
  auto [tmp, err]{RecGetRecordFloat(name)};
  if (err == REC_ERR_FAIL) {
    return REC_ERR_FAIL;
  }
  *rec_float = tmp;
  return RecRegisterConfigUpdateCb(name, link_float, (void *)rec_float);
}

RecErrT
RecLinkConfigCounter(const char *name, RecCounter *rec_counter)
{
  auto [tmp, err]{RecGetRecordCounter(name)};
  if (err == REC_ERR_FAIL) {
    return REC_ERR_FAIL;
  }
  *rec_counter = tmp;
  return RecRegisterConfigUpdateCb(name, link_counter, (void *)rec_counter);
}

RecErrT
RecLinkConfigString(const char *name, RecString *rec_string)
{
  {
    auto [tmp, err]{RecGetRecordStringAlloc(name)};
    if (err == REC_ERR_FAIL) {
      return REC_ERR_FAIL;
    }
    *rec_string = ats_stringdup(tmp);
  }
  return RecRegisterConfigUpdateCb(name, link_string_alloc, (void *)rec_string);
}

RecErrT
RecLinkConfigByte(const char *name, RecByte *rec_byte)
{
  auto [tmp, err]{RecGetRecordInt(name)};
  if (err == REC_ERR_FAIL) {
    return REC_ERR_FAIL;
  }
  *rec_byte = tmp;
  return RecRegisterConfigUpdateCb(name, link_byte, (void *)rec_byte);
}

//-------------------------------------------------------------------------
// RecRegisterConfigUpdateCb
//-------------------------------------------------------------------------
RecErrT
RecRegisterConfigUpdateCb(const char *name, RecConfigUpdateCb const &update_cb, void *cookie)
{
  RecErrT err = REC_ERR_FAIL;

  ink_rwlock_rdlock(&g_records_rwlock);

  if (auto it = g_records_ht.find(name); it != g_records_ht.end()) {
    RecRecord *r = it->second;

    rec_mutex_acquire(&(r->lock));
    if (REC_TYPE_IS_CONFIG(r->rec_type)) {
      /* -- upgrade to support a list of callback functions
         if (!(r->config_meta.update_cb)) {
         r->config_meta.update_cb = update_cb;
         r->config_meta.update_cookie = cookie;
         err = REC_ERR_OKAY;
         }
       */

      RecConfigUpdateCbList *new_callback = new RecConfigUpdateCbList(update_cb, cookie);
      ink_assert(new_callback);
      if (!r->config_meta.update_cb_list) {
        r->config_meta.update_cb_list = new_callback;
      } else {
        RecConfigUpdateCbList *cur_callback  = nullptr;
        RecConfigUpdateCbList *prev_callback = nullptr;
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

void
Enable_Config_Var(std::string_view const &name, RecContextCb record_cb, RecConfigUpdateCb const &config_cb, void *cookie)
{
  // Must use this indirection because the API requires a pure function, therefore no values can
  // be bound in the lambda. Instead this is needed to pass in the data for both the lambda and
  // the actual callback.
  using Context = std::tuple<decltype(record_cb), void *>;

  // To deal with process termination cleanup, store the context instances in a deque where
  // tail insertion doesn't invalidate pointers. These persist until process shutdown.
  static std::deque<Context> storage;

  Context &ctx = storage.emplace_back(record_cb, cookie);
  // Register the call back - this handles external updates.
  RecRegisterConfigUpdateCb(
    name.data(),
    [&config_cb](const char *name, RecDataT dtype, RecData data, void *ctx) -> int {
      auto &&[context_cb, cookie] = *static_cast<Context *>(ctx);
      if ((*context_cb)(name, dtype, data, cookie)) {
        config_cb(name, dtype, data, cookie); // Let the caller handle the runtime config update.
      }
      return REC_ERR_OKAY;
    },
    &ctx);

  // Use the record to do the initial data load.
  // Look it up and call the updater @a cb on that data.
  RecLookupRecord(
    name.data(),
    [](RecRecord const *r, void *ctx) -> void {
      auto &&[cb, cookie] = *static_cast<Context *>(ctx);
      (*cb)(r->name, r->data_type, r->data, cookie);
    },
    &ctx);
}

//-------------------------------------------------------------------------
// RecGetRecordXXX
//-------------------------------------------------------------------------
std::pair<RecInt, RecErrT>
RecGetRecordInt(const char *name, bool lock)
{
  RecErrT err;
  RecData data;
  RecInt  rec_int;

  if ((err = RecGetRecord_Xmalloc(name, RECD_INT, &data, lock)) == REC_ERR_OKAY) {
    rec_int = data.rec_int;
  } else {
    rec_int = 0;
  }
  return std::make_pair(rec_int, err);
}

std::pair<RecFloat, RecErrT>
RecGetRecordFloat(const char *name, bool lock)
{
  RecErrT  err;
  RecData  data;
  RecFloat rec_float;

  if ((err = RecGetRecord_Xmalloc(name, RECD_FLOAT, &data, lock)) == REC_ERR_OKAY) {
    rec_float = data.rec_float;
  } else {
    rec_float = 0;
  }
  return std::make_pair(rec_float, err);
}

RecErrT
RecGetRecordString(const char *name, char *buf, int buf_len, bool lock)
{
  RecErrT err = REC_ERR_OKAY;

  if (lock) {
    ink_rwlock_rdlock(&g_records_rwlock);
  }
  if (auto it = g_records_ht.find(name); it != g_records_ht.end()) {
    RecRecord *r = it->second;

    rec_mutex_acquire(&(r->lock));
    if (!r->registered || (r->data_type != RECD_STRING)) {
      err = REC_ERR_FAIL;
    } else {
      if (r->data.rec_string == nullptr) {
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

std::pair<std::optional<std::string>, RecErrT>
RecGetRecordStringAlloc(const char *name, bool lock)
{
  // Must use this indirection because the API requires a pure function, therefore no values can
  // be bound in the lambda.
  using Context = std::pair<std::optional<std::string>, RecErrT>;
  Context ret{std::nullopt, REC_ERR_FAIL};

  RecLookupRecord(
    name,
    [](RecRecord const *r, void *ctx) -> void {
      auto &&[str, err] = *static_cast<Context *>(ctx);
      if (r->registered && r->data_type == RECD_STRING) {
        err = REC_ERR_OKAY;
        if (auto rec_str{r->data.rec_string}; rec_str) {
          auto len{strlen(rec_str)};
          if (len) {
            // Chop trailing spaces
            auto end{rec_str + len - 1};
            while (end >= rec_str && isspace(*end)) {
              end--;
            }
            len = static_cast<std::string::size_type>(end + 1 - rec_str);
          }
          str = len ? std::string{rec_str, len} : std::string{};
        }
      }
    },
    &ret, lock);
  return ret;
}

std::pair<RecCounter, RecErrT>
RecGetRecordCounter(const char *name, bool lock)
{
  RecErrT    err;
  RecData    data;
  RecCounter rec_counter;

  if ((err = RecGetRecord_Xmalloc(name, RECD_COUNTER, &data, lock)) == REC_ERR_OKAY) {
    rec_counter = data.rec_counter;
  } else {
    rec_counter = 0;
  }
  return std::make_pair(rec_counter, err);
}

//-------------------------------------------------------------------------
// RecGetRec Attributes
//-------------------------------------------------------------------------

RecErrT
RecLookupRecord(const char *name, void (*callback)(const RecRecord *, void *), void *data, bool lock)
{
  RecErrT      err     = REC_ERR_FAIL;
  ts::Metrics &metrics = ts::Metrics::instance();
  auto         it      = metrics.find(name);

  if (it != metrics.end()) {
    RecRecord r;
    auto &&[name, val] = *it;

    r.rec_type     = RECT_PLUGIN;
    r.data_type    = RECD_INT;
    r.name         = name.data();
    r.data.rec_int = val;

    callback(&r, data);
    err = REC_ERR_OKAY;
  } else {
    if (lock) {
      ink_rwlock_rdlock(&g_records_rwlock);
    }

    if (auto it = g_records_ht.find(name); it != g_records_ht.end()) {
      RecRecord *r = it->second;

      rec_mutex_acquire(&(r->lock));
      callback(r, data);
      err = REC_ERR_OKAY;
      rec_mutex_release(&(r->lock));
    }

    if (lock) {
      ink_rwlock_unlock(&g_records_rwlock);
    }
  }

  return err;
}

RecErrT
RecLookupMatchingRecords(unsigned rec_type, const char *match, void (*callback)(const RecRecord *, void *), void *data,
                         bool /* lock ATS_UNUSED */)
{
  int num_records;
  DFA regex;

  if (!regex.compile(match, RE_CASE_INSENSITIVE | RE_UNANCHORED)) {
    return REC_ERR_FAIL;
  }

  if ((rec_type & (RECT_PROCESS | RECT_NODE | RECT_PLUGIN))) {
    // First find the new metrics, this is a bit of a hack, because we still use the old
    // librecords callback with a "pseudo" record.
    RecRecord tmp;

    tmp.rec_type  = RECT_PROCESS;
    tmp.data_type = RECD_INT;

    for (auto &&[name, val] : ts::Metrics::instance()) {
      if (regex.match(name.data()) >= 0) {
        tmp.name         = name.data();
        tmp.data.rec_int = val;
        callback(&tmp, data);
      }
    }
    // Fall through to return any matching string metrics
  }

  num_records = g_num_records;
  for (int i = 0; i < num_records; i++) {
    RecRecord *r = &(g_records[i]);

    if ((r->rec_type & rec_type) == 0) {
      continue;
    }

    if (regex.match(r->name) < 0) {
      continue;
    }

    rec_mutex_acquire(&(r->lock));
    callback(r, data);
    rec_mutex_release(&(r->lock));
  }

  return REC_ERR_OKAY;
}

RecErrT
RecGetRecordType(const char *name, RecT *rec_type, bool lock)
{
  RecErrT err = REC_ERR_FAIL;

  if (lock) {
    ink_rwlock_rdlock(&g_records_rwlock);
  }

  if (auto it = g_records_ht.find(name); it != g_records_ht.end()) {
    RecRecord *r = it->second;

    rec_mutex_acquire(&(r->lock));
    *rec_type = r->rec_type;
    err       = REC_ERR_OKAY;
    rec_mutex_release(&(r->lock));
  }

  if (lock) {
    ink_rwlock_unlock(&g_records_rwlock);
  }

  return err;
}

RecErrT
RecGetRecordDataType(const char *name, RecDataT *data_type, bool lock)
{
  RecErrT err = REC_ERR_FAIL;

  if (lock) {
    ink_rwlock_rdlock(&g_records_rwlock);
  }

  if (auto it = g_records_ht.find(name); it != g_records_ht.end()) {
    RecRecord *r = it->second;

    rec_mutex_acquire(&(r->lock));
    if (!r->registered) {
      err = REC_ERR_FAIL;
    } else {
      *data_type = r->data_type;
      err        = REC_ERR_OKAY;
    }
    rec_mutex_release(&(r->lock));
  }

  if (lock) {
    ink_rwlock_unlock(&g_records_rwlock);
  }

  return err;
}

RecErrT
RecGetRecordPersistenceType(const char *name, RecPersistT *persist_type, bool lock)
{
  RecErrT err = REC_ERR_FAIL;

  if (lock) {
    ink_rwlock_rdlock(&g_records_rwlock);
  }

  *persist_type = RECP_NULL;

  if (auto it = g_records_ht.find(name); it != g_records_ht.end()) {
    RecRecord *r = it->second;

    rec_mutex_acquire(&(r->lock));
    if (REC_TYPE_IS_STAT(r->rec_type)) {
      *persist_type = r->stat_meta.persist_type;
      err           = REC_ERR_OKAY;
    }
    rec_mutex_release(&(r->lock));
  }

  if (lock) {
    ink_rwlock_unlock(&g_records_rwlock);
  }

  return err;
}

RecErrT
RecGetRecordSource(const char *name, RecSourceT *source, bool lock)
{
  RecErrT err = REC_ERR_FAIL;

  if (lock) {
    ink_rwlock_rdlock(&g_records_rwlock);
  }

  if (auto it = g_records_ht.find(name); it != g_records_ht.end()) {
    RecRecord *r = it->second;

    rec_mutex_acquire(&(r->lock));
    *source = r->config_meta.source;
    err     = REC_ERR_OKAY;
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
  RecRecord *r = nullptr;

  ink_rwlock_wrlock(&g_records_rwlock);
  if ((r = register_record(rec_type, name, data_type, data_default, persist_type)) != nullptr) {
    // If the persistence type we found in the records hash is not the same as the persistence
    // type we are registering, then that means that it changed between the previous software
    // version and the current version. If the metric changed to non-persistent, reset to the
    // new default value.
    if ((r->stat_meta.persist_type == RECP_NULL || r->stat_meta.persist_type == RECP_PERSISTENT) &&
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
RecRegisterConfig(RecT rec_type, const char *name, RecDataT data_type, RecData data_default, RecUpdateT update_type,
                  RecCheckT check_type, const char *check_expr, RecSourceT source, RecAccessT access_type)
{
  RecRecord *r;
  bool       updated_p;

  ink_rwlock_wrlock(&g_records_rwlock);
  if ((r = register_record(rec_type, name, data_type, data_default, RECP_NULL, &updated_p)) != nullptr) {
    // Note: do not modify 'record->config_meta.update_required'
    r->config_meta.update_type = update_type;
    r->config_meta.check_type  = check_type;

    ats_free(r->config_meta.check_expr);

    r->config_meta.check_expr     = ats_strdup(check_expr);
    r->config_meta.update_cb_list = nullptr;
    r->config_meta.access_type    = access_type;
    if (!updated_p) {
      r->config_meta.source = source;
    }
  }
  ink_rwlock_unlock(&g_records_rwlock);

  return r;
}

//-------------------------------------------------------------------------
// RecGetRecord_Xmalloc
//-------------------------------------------------------------------------
RecErrT
RecGetRecord_Xmalloc(const char *name, RecDataT data_type, RecData *data, bool lock)
{
  RecErrT err = REC_ERR_OKAY;

  if (lock) {
    ink_rwlock_rdlock(&g_records_rwlock);
  }

  if (auto it = g_records_ht.find(name); it != g_records_ht.end()) {
    RecRecord *r = it->second;

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
RecForceInsert(RecRecord *record)
{
  RecRecord *r = nullptr;
  bool       r_is_a_new_record;

  ink_rwlock_wrlock(&g_records_rwlock);

  if (auto it = g_records_ht.find(record->name); it != g_records_ht.end()) {
    r                 = it->second;
    r_is_a_new_record = false;
    rec_mutex_acquire(&(r->lock));
    r->rec_type  = record->rec_type;
    r->data_type = record->data_type;
  } else {
    r_is_a_new_record = true;
    if ((r = RecAlloc(record->rec_type, record->name, record->data_type)) == nullptr) {
      ink_rwlock_unlock(&g_records_rwlock);
      return nullptr;
    }
  }

  // set the record value
  RecDataSet(r->data_type, &(r->data), &(record->data));
  RecDataSet(r->data_type, &(r->data_default), &(record->data_default));

  r->registered = record->registered;
  r->rsb_id     = record->rsb_id;

  if (REC_TYPE_IS_STAT(r->rec_type)) {
    r->stat_meta.persist_type = record->stat_meta.persist_type;
    r->stat_meta.data_raw     = record->stat_meta.data_raw;
  } else if (REC_TYPE_IS_CONFIG(r->rec_type)) {
    r->config_meta.update_required = record->config_meta.update_required;
    r->config_meta.update_type     = record->config_meta.update_type;
    r->config_meta.check_type      = record->config_meta.check_type;
    ats_free(r->config_meta.check_expr);
    r->config_meta.check_expr  = ats_strdup(record->config_meta.check_expr);
    r->config_meta.access_type = record->config_meta.access_type;
    r->config_meta.source      = record->config_meta.source;
  }

  if (r_is_a_new_record) {
    g_records_ht.emplace(r->name, r);
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
  switch (data_type) {
  case RECD_INT:
    RecDebug(DL_Note, "  ([%d] '%s', '%" PRId64 "')", registered, name, datum->rec_int);
    break;
  case RECD_FLOAT:
    RecDebug(DL_Note, "  ([%d] '%s', '%f')", registered, name, datum->rec_float);
    break;
  case RECD_STRING:
    RecDebug(DL_Note, "  ([%d] '%s', '%s')", registered, name, datum->rec_string ? datum->rec_string : "NULL");
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
      callback(r->rec_type, edata, r->registered, r->name, r->data_type, &r->data);
      rec_mutex_release(&(r->lock));
    }
  }

  // Dump all new metrics as well (no "type" for them)
  RecData datum;

  for (auto &&[name, val] : ts::Metrics::instance()) {
    datum.rec_int = val;
    callback(RECT_PLUGIN, edata, true, name.data(), TS_RECORDDATATYPE_INT, &datum);
  }
}

void
RecDumpRecordsHt(RecT rec_type)
{
  RecDebug(DL_Note, "Dumping Records:");
  RecDumpRecords(rec_type, debug_record_callback, nullptr);
}

//-------------------------------------------------------------------------
// RecConfigReadConfigDir
//-------------------------------------------------------------------------
std::string
RecConfigReadConfigDir()
{
  if (const char *env = getenv("PROXY_CONFIG_CONFIG_DIR")) {
    return Layout::get()->relative(env);
  } else {
    return Layout::get()->sysconfdir;
  }
}

//-------------------------------------------------------------------------
// RecConfigReadRuntimeDir
//-------------------------------------------------------------------------
std::string
RecConfigReadRuntimeDir()
{
  char buf[PATH_NAME_MAX];

  buf[0] = '\0';
  RecGetRecordString("proxy.config.local_state_dir", buf, PATH_NAME_MAX);
  if (strlen(buf) > 0) {
    return Layout::get()->relative(buf);
  } else {
    return Layout::get()->runtimedir;
  }
}

//-------------------------------------------------------------------------
// RecConfigReadLogDir
//-------------------------------------------------------------------------
std::string
RecConfigReadLogDir()
{
  char buf[PATH_NAME_MAX];

  buf[0] = '\0';
  RecGetRecordString("proxy.config.log.logfile_dir", buf, PATH_NAME_MAX);
  if (strlen(buf) > 0) {
    return Layout::get()->relative(buf);
  } else {
    return Layout::get()->logdir;
  }
}

//-------------------------------------------------------------------------
// RecConfigReadBinDir
//-------------------------------------------------------------------------
std::string
RecConfigReadBinDir()
{
  char buf[PATH_NAME_MAX];

  buf[0] = '\0';
  RecGetRecordString("proxy.config.bin_path", buf, PATH_NAME_MAX);
  if (strlen(buf) > 0) {
    return Layout::get()->relative(buf);
  } else {
    return Layout::get()->bindir;
  }
}

//-------------------------------------------------------------------------
// RecConfigReadPluginDir
//-------------------------------------------------------------------------
std::string
RecConfigReadPluginDir()
{
  char buf[PATH_NAME_MAX];

  buf[0] = '\0';
  RecGetRecordString("proxy.config.plugin.plugin_dir", buf, PATH_NAME_MAX);
  if (strlen(buf) > 0) {
    return Layout::get()->relative(buf);
  } else {
    return Layout::get()->libexecdir;
  }
}

//-------------------------------------------------------------------------
// RecConfigReadConfigPath
//-------------------------------------------------------------------------
std::string
RecConfigReadConfigPath(const char *file_variable, const char *default_value)
{
  std::string sysconfdir(RecConfigReadConfigDir());

  // If the file name is in a configuration variable, look it up first ...
  if (file_variable) {
    char buf[PATH_NAME_MAX];

    buf[0] = '\0';
    RecGetRecordString(file_variable, buf, PATH_NAME_MAX);
    if (strlen(buf) > 0) {
      return Layout::get()->relative_to(sysconfdir, buf);
    }
  }

  // Otherwise take the default ...
  if (default_value) {
    return Layout::get()->relative_to(sysconfdir, default_value);
  }

  return {};
}

//-------------------------------------------------------------------------
// RecConfigReadPersistentStatsPath
//-------------------------------------------------------------------------
std::string
RecConfigReadPersistentStatsPath()
{
  std::string rundir(RecConfigReadRuntimeDir());
  return Layout::relative_to(rundir, ts::filename::RECORDS_STATS);
}

//-------------------------------------------------------------------------
// RecConfigWarnIfUnregistered
//-------------------------------------------------------------------------
/// Generate a warning if the record is a configuration name/value but is not registered.
void
RecConfigWarnIfUnregistered()
{
  RecDumpRecords(
    RECT_CONFIG,
    [](RecT, void *, int registered_p, const char *name, int, RecData *) -> void {
      if (!registered_p) {
        Warning("Unrecognized configuration value '%s'", name);
      }
    },
    nullptr);
}

//-------------------------------------------------------------------------
// i_am_the_record_owner, only used for librecords_p.a
//-------------------------------------------------------------------------
bool
i_am_the_record_owner(RecT rec_type)
{
  switch (rec_type) {
  case RECT_CONFIG:
  case RECT_PROCESS:
  case RECT_NODE:
  case RECT_LOCAL:
  case RECT_PLUGIN:
    return true;
  default:
    ink_assert(!"Unexpected RecT type");
    return false;
  }

  return false;
}
