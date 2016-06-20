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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <ts/ts.h>
#include <ts/remap.h>
#include <ts/experimental.h>
#include "MurmurHash3.h"
#include <inttypes.h>

#include "P_collapsed_connection.h"

/**
 * Helper function for the config parser
 *  copied from plugins/conf_remap/conf_remap.cc
 *
 * @param const char *str config data type
 *
 * @return TSRecordDataType config data type
 */
inline TSRecordDataType
str_to_datatype(const char *str)
{
  TSRecordDataType type = TS_RECORDDATATYPE_NULL;

  if (!str || !*str)
    return TS_RECORDDATATYPE_NULL;

  if (!strcmp(str, "INT")) {
    type = TS_RECORDDATATYPE_INT;
  } else if (!strcmp(str, "STRING")) {
    type = TS_RECORDDATATYPE_STRING;
  }

  return type;
}

/**
 * Find collapsed_connection transaction config option
 *  modified from InkAPI.cc:TSHttpTxnConfigFind
 *
 * @param const char       *name  name of config options
 * @param int              length length of *name
 * @param CcConfigKey      *conf  config key
 * @param TSRecordDataType *type  config option data type
 *
 * @return CcTxnState hashEntry added, should be ignore and pass, or fail
 */
static TSReturnCode
CcHttpTxnConfigFind(const char *name, int length, CcConfigKey *conf, TSRecordDataType *type)
{
  *type = TS_RECORDDATATYPE_NULL;

  if (length == -1)
    length = strlen(name);

  switch (length) {
  case 46:
    if (!strncmp(name, "proxy.config.http.collapsed_connection.enabled", length)) {
      *conf = CcEnabled;
      *type = TS_RECORDDATATYPE_INT;
    }
    break;
  case 54:
    if (!strncmp(name, "proxy.config.http.collapsed_connection.required_header", length)) {
      *conf = CcRequiredHeader;
      *type = TS_RECORDDATATYPE_STRING;
    }
    break;
  case 60:
    if (!strncmp(name, "proxy.config.http.collapsed_connection.keep_pass_record_time", length)) {
      *conf = CcKeepPassRecordTime;
      *type = TS_RECORDDATATYPE_INT;
    }
    break;
  case 61:
    if (!strncmp(name, "proxy.config.http.collapsed_connection.insert_lock_retry_time", length)) {
      *conf = CcInsertLockRetryTime;
      *type = TS_RECORDDATATYPE_INT;
    } else if (!strncmp(name, "proxy.config.http.collapsed_connection.max_lock_retry_timeout", length)) {
      *conf = CcMaxLockRetryTimeout;
      *type = TS_RECORDDATATYPE_INT;
    }
    break;
  default:
    break;
  }

  return ((*type != TS_RECORDDATATYPE_NULL) ? TS_SUCCESS : TS_ERROR);
}

/**
 * Initial collapsed_connection plugin with config file
 *  modified from plugins/conf_remap/conf_remap.cc, RemapConfigs::parse_file
 *
 * @param const char *fn filename of config file
 *
 * @return CcPluginConfig *config object
 */
static CcPluginConfig *
initConfig(const char *fn)
{
  CcPluginData *plugin_data = getCcPlugin();
  CcPluginConfig *config    = static_cast<CcPluginConfig *>(TSmalloc(sizeof(CcPluginConfig)));

  // Default config
  if (NULL == plugin_data || NULL == plugin_data->global_config) {
    config->enabled                = true;
    config->required_header        = NULL;
    config->insert_lock_retry_time = DEFAULT_INSERT_LOCK_RETRY_TIME;
    config->max_lock_retry_timeout = DEFAULT_MAX_LOCK_RETRY_TIMEOUT;
    config->keep_pass_record_time  = DEFAULT_KEEP_PASS_RECORD_TIME;
  } else {
    // Inherit from global config
    CcPluginConfig *global_config = plugin_data->global_config;

    config->enabled                = global_config->enabled;
    config->required_header        = TSstrdup(global_config->required_header);
    config->insert_lock_retry_time = global_config->insert_lock_retry_time;
    config->max_lock_retry_timeout = global_config->max_lock_retry_timeout;
    config->keep_pass_record_time  = global_config->keep_pass_record_time;
  }

  if (NULL != fn) {
    if (1 == strlen(fn)) {
      if (0 == strcmp("0", fn)) {
        config->enabled = false;
      } else if (0 == strcmp("1", fn)) {
        config->enabled = true;
      } else {
        TSError("[collapsed_connection] Parameter '%s' ignored", fn);
      }
    } else {
      int line_num = 0;
      TSFile file;
      char buf[8192];
      CcConfigKey name;
      TSRecordDataType type, expected_type;

      if (NULL == (file = TSfopen(fn, "r"))) {
        TSError("[collapsed_connection] Could not open config file %s", fn);
      } else {
        while (NULL != TSfgets(file, buf, sizeof(buf))) {
          char *ln, *tok;
          char *s = buf;

          ++line_num; // First line is #1 ...
          while (isspace(*s))
            ++s;
          tok = strtok_r(s, " \t", &ln);

          // check for blank lines and comments
          if ((!tok) || (tok && ('#' == *tok)))
            continue;

          if (strncmp(tok, "CONFIG", 6)) {
            TSError("[collapsed_connection] File %s, line %d: non-CONFIG line encountered", fn, line_num);
            continue;
          }
          // Find the configuration name
          tok = strtok_r(NULL, " \t", &ln);
          if (CcHttpTxnConfigFind(tok, -1, &name, &expected_type) != TS_SUCCESS) {
            TSError("[collapsed_connection] File %s, line %d: no records.config name given", fn, line_num);
            continue;
          }
          // Find the type (INT or STRING only)
          tok = strtok_r(NULL, " \t", &ln);
          if (TS_RECORDDATATYPE_NULL == (type = str_to_datatype(tok))) {
            TSError("[collapsed_connection] File %s, line %d: only INT and STRING types supported", fn, line_num);
            continue;
          }

          if (type != expected_type) {
            TSError("[collapsed_connection] File %s, line %d: mismatch between provide data type, and expected type", fn, line_num);
            continue;
          }
          // Find the value (which depends on the type above)
          if (ln) {
            while (isspace(*ln))
              ++ln;
            if ('\0' == *ln) {
              tok = NULL;
            } else {
              tok = ln;
              while (*ln != '\0')
                ++ln;
              --ln;
              while (isspace(*ln) && (ln > tok))
                --ln;
              ++ln;
              *ln = '\0';
            }
          } else {
            tok = NULL;
          }
          if (!tok) {
            TSError("[collapsed_connection] File %s, line %d: the configuration must provide a value", fn, line_num);
            continue;
          }
          // Now store the new config
          switch (name) {
          case CcRequiredHeader:
            if (NULL != config->required_header) {
              TSfree(config->required_header);
            }
            if (4 == strlen(tok) && 0 == strcmp(tok, "NULL")) {
              config->required_header = NULL;
            } else {
              config->required_header = TSstrdup(tok);
            }
            break;
          case CcEnabled:
            config->enabled = strtoll(tok, NULL, 10);
            break;
          case CcInsertLockRetryTime:
            config->insert_lock_retry_time = strtoll(tok, NULL, 10);
            break;
          case CcMaxLockRetryTimeout:
            config->max_lock_retry_timeout = strtoll(tok, NULL, 10);
            break;
          case CcKeepPassRecordTime:
            config->keep_pass_record_time = strtoll(tok, NULL, 10);
            break;
          default:
            break;
          }
        }

        TSfclose(file);
      }
    }
  }
  if (config->required_header) {
    config->required_header_len = strlen(config->required_header);
  } else {
    config->required_header_len = 0;
  }

  TSDebug(PLUGIN_NAME, "enabled = %d", static_cast<int>(config->enabled));
  TSDebug(PLUGIN_NAME, "required_header = %s", config->required_header);
  TSDebug(PLUGIN_NAME, "insert_lock_retry_time = %d", static_cast<int>(config->insert_lock_retry_time));
  TSDebug(PLUGIN_NAME, "max_lock_retry_timeout = %d", static_cast<int>(config->max_lock_retry_timeout));
  TSDebug(PLUGIN_NAME, "keep_pass_record_time = %d", static_cast<int>(config->keep_pass_record_time));

  return config;
}

/**
 * Update and get current size in map
 *  it's ok to use static variable here because already protected by plugin_data->mutex
 *
 * @param UintMap *map Hash Map
 *
 * @return int64_t current Hash Map size
 */
static int64_t
getCurrentHashEntries(UintMap *map)
{
  static int64_t cur = 0;
  static int64_t max = 0;
  int64_t size       = map->size();
  int64_t diff       = size - cur;

  cur = size;
  if (diff != 0) {
    CcPluginData *plugin_data = getCcPlugin();

    TSStatIntSet(plugin_data->cur_hash_entries, cur);
    if (cur > max) {
      TSStatIntSet(plugin_data->max_hash_entries, cur);
      max = cur;
    }
  }

  return cur;
}

/**
 * Update and get current size in list
 *  it's ok to use static variable here because already protected by plugin_data->mutex
 *
 * @param UsecList *list List
 *
 * @return int64_t current List size
 */
static int64_t
getCurrentKeepPassEntries(UsecList *list)
{
  CcPluginData *plugin_data = getCcPlugin();
  static int64_t cur        = 0;
  static int64_t max        = 0;
  int64_t size              = list->size();
  int64_t diff              = size - cur;

  cur = size;
  if (diff != 0) {
    TSStatIntSet(plugin_data->cur_keep_pass_entries, cur);
    if (cur > max) {
      TSStatIntSet(plugin_data->max_keep_pass_entries, cur);
      max = cur;
    }
  }

  return cur;
}

/**
 * Add Or Check keep pass records from list
 *
 * @param uint32_t hash_key new hash_key to add
 * @param int64_t  timeout  timeout of this record
 *
 * @return TSReturnCode success or not
 */
static TSReturnCode
addOrCheckKeepPassRecords(uint32_t hash_key, int64_t timeout)
{
  CcPluginData *plugin_data = getCcPlugin();
  UintMap *active_hash_map  = plugin_data->active_hash_map;
  UsecList *keep_pass_list  = plugin_data->keep_pass_list;
  std::list<PassRecord>::iterator it;
  PassRecord passRecord;
  bool added      = true;
  TSHRTime cur_ms = TShrtime() / TS_HRTIME_MSECOND; // TS-2200, ats_dev-4.1+

  // Only gc per 0.1ms
  if (0 == hash_key && timeout == 0) {
    if (cur_ms - plugin_data->last_gc_time < 100) {
      return TS_SUCCESS;
    }
  }

  passRecord.timeout  = cur_ms + timeout;
  passRecord.hash_key = hash_key;

  if (hash_key > 0) {
    bool push_back = false;

    if (keep_pass_list->empty()) {
      push_back = true;
    } else {
      PassRecord &lastRecord = *(keep_pass_list->end());

      if (lastRecord.timeout <= passRecord.timeout) {
        push_back = true;
      }
    }

    if (push_back) {
      keep_pass_list->push_back(passRecord);
      getCurrentKeepPassEntries(keep_pass_list);
      TSDebug(PLUGIN_NAME, "push_back pass entry with timeout = %" PRId64 ", hash_key = %" PRIu32, passRecord.timeout,
              passRecord.hash_key);
    } else {
      added = false;
    }
  }

  for (it = keep_pass_list->begin(); it != keep_pass_list->end(); ++it) {
    PassRecord &thisRecord = *it;

    if (thisRecord.timeout <= cur_ms) {
      UintMap::iterator pos = active_hash_map->find(thisRecord.hash_key);
      if (pos != active_hash_map->end()) {
        active_hash_map->erase(pos);
        getCurrentHashEntries(active_hash_map);
      }
      keep_pass_list->erase(it++);
      getCurrentKeepPassEntries(keep_pass_list);
      TSDebug(PLUGIN_NAME, "remove pass entry with timeout = %" PRId64 ", hash_key = %" PRIu32, thisRecord.timeout,
              thisRecord.hash_key);
    } else if (false == added) {
      if (thisRecord.timeout >= passRecord.timeout) {
        keep_pass_list->insert(it, passRecord);
        getCurrentKeepPassEntries(keep_pass_list);
        TSDebug(PLUGIN_NAME, "insert pass entry with timeout = %" PRId64 ", hash_key = %" PRIu32, passRecord.timeout,
                passRecord.hash_key);
        break;
      }
    } else {
      break;
    }
  }
  plugin_data->last_gc_time = cur_ms;

  return TS_SUCCESS;
}

/**
 * Insert new hashEntry into hashTable
 *
 * @param CcTxnData *txn_data transaction data
 *
 * @return CcTxnState hashEntry added, should be ignore and pass, or fail
 */
static CcTxnState
insertNewHashEntry(CcTxnData *txn_data)
{
  CcPluginData *plugin_data = getCcPlugin();
  CcTxnState ret            = CC_NONE;
  UintMap *active_hash_map  = plugin_data->active_hash_map;

  if (0 == txn_data->hash_key) {
    return ret;
  }

  if (TS_SUCCESS == TSMutexLockTry(plugin_data->mutex)) {
    std::pair<std::map<uint32_t, int8_t>::iterator, bool> map_ret;
    int64_t size = 0;
    addOrCheckKeepPassRecords(0, 0);
    map_ret = active_hash_map->insert(std::make_pair(txn_data->hash_key, CC_INSERT));
    size    = getCurrentHashEntries(active_hash_map);
    TSMutexUnlock(plugin_data->mutex);
    if (false != map_ret.second) {
      TSDebug(PLUGIN_NAME, "[%" PRIu64 "] hash_key inserted, active_hash_map.size = %" PRId64, txn_data->seq_id, size);
      ret = CC_INSERT;
    } else if (CC_PASS == map_ret.first->second) {
      TSDebug(PLUGIN_NAME, "hash value = %d, previous request mark it non-cacheable", map_ret.first->second);
      ret = CC_PASS;
    } else {
      TSDebug(PLUGIN_NAME, "hash value = %d, hash_key already exists, wait next schedule", map_ret.first->second);
      ret = CC_LOCKED;
    }
  } else {
    TSDebug(PLUGIN_NAME, "[%" PRIu64 "] Unable to get mutex", txn_data->seq_id);
  }

  if (CC_INSERT != ret && CC_PASS != ret) {
    TSHRTime cur_ms = TShrtime() / TS_HRTIME_MSECOND; // TS-2200, ats_dev-4.1+

    if (0 == txn_data->wait_time) {
      txn_data->wait_time = cur_ms;
    } else if (cur_ms - txn_data->wait_time > txn_data->config->max_lock_retry_timeout) {
      txn_data->wait_time = cur_ms - txn_data->wait_time;
      // Pass cache lock
      ret = CC_PASS;
      TSDebug(PLUGIN_NAME, "timeout (%" PRId64 " > %d), pass plugin", txn_data->wait_time,
              static_cast<int32_t>(txn_data->config->max_lock_retry_timeout));
    }
  } else if (0 != txn_data->wait_time) {
    txn_data->wait_time = TShrtime() / 1000000 - txn_data->wait_time;
    TSDebug(PLUGIN_NAME, "waited for %" PRId64 " ms", txn_data->wait_time);
  }

  return ret;
}

/**
 * Update or remove hashEntry from hashTable
 *
 * @param CcTxnData *txn_data transaction data
 *
 * @return TSReturnCode Success or failure
 */
static TSReturnCode
updateOrRemoveHashEntry(CcTxnData *txn_data)
{
  TSReturnCode ret          = TS_ERROR;
  CcPluginData *plugin_data = getCcPlugin();
  UintMap *active_hash_map  = plugin_data->active_hash_map;

  if (0 == txn_data->hash_key || CC_PASSED == txn_data->cc_state) {
    return TS_SUCCESS;
  }

  if (CC_PASS != txn_data->cc_state && CC_REMOVE != txn_data->cc_state) {
    return ret;
  }

  if (TS_SUCCESS == TSMutexLockTry(plugin_data->mutex)) {
    UintMap::iterator pos = active_hash_map->find(txn_data->hash_key);
    int64_t size          = 0;
    if (pos != active_hash_map->end()) {
      active_hash_map->erase(pos);
    }
    if (CC_PASS == txn_data->cc_state) {
      active_hash_map->insert(std::make_pair(txn_data->hash_key, CC_PASS));
      addOrCheckKeepPassRecords(txn_data->hash_key, txn_data->config->keep_pass_record_time);
      size = getCurrentHashEntries(active_hash_map);
      TSMutexUnlock(plugin_data->mutex);

      TSDebug(PLUGIN_NAME, "[%" PRIu64 "] hashEntry updated, active_hash_map.size = %" PRId64, txn_data->seq_id, size);
      txn_data->cc_state = CC_PASSED;
    } else {
      addOrCheckKeepPassRecords(0, 0);
      size = getCurrentHashEntries(active_hash_map);
      TSMutexUnlock(plugin_data->mutex);

      TSDebug(PLUGIN_NAME, "[%" PRIu64 "] hashEntry removed, active_hash_map.size = %" PRId64, txn_data->seq_id, size);
      txn_data->cc_state = CC_DONE;
    }
    ret = TS_SUCCESS;
  } else {
    TSDebug(PLUGIN_NAME, "[%" PRIu64 "] Unable to get mutex", txn_data->seq_id);
  }

  return ret;
}

/**
 * Get hash_key from CacheUrl
 *
 * @param TSHttpTxn txnp    Transaction ptr
 * @param TSMBuffer bufp    TS memory buffer
 * @param TSMLoc    hdr_loc TS memory loc
 *
 * @return uint32_t hash_key
 */
static uint32_t
getCacheUrlHashKey(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc /* hdr_loc ATS_UNUSED */)
{
  TSMLoc url_loc = TS_NULL_MLOC;
  int url_len;
  char *url         = NULL;
  uint32_t hash_key = 0;

  if (TS_SUCCESS != TSUrlCreate(bufp, &url_loc)) {
    TSDebug(PLUGIN_NAME, "unable to create url");
    return 0;
  }

  if (TS_SUCCESS == TSHttpTxnCacheLookupUrlGet(txnp, bufp, url_loc)) {
    url = TSUrlStringGet(bufp, url_loc, &url_len);
  } else {
    TSDebug(PLUGIN_NAME, "use EffectiveUrl as CacheLookupUrl instead");
    url = TSHttpTxnEffectiveUrlStringGet(txnp, &url_len);
  }

  MurmurHash3_x86_32(url, url_len, c_hashSeed, &hash_key);
  TSDebug(PLUGIN_NAME, "CacheLookupUrl = %s, hash_key = %u", url, hash_key);
  TSfree(url);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, url_loc);

  return hash_key;
}

/**
 * Is response public cacheable or not
 *  try to find Expires and Cache-Control: public, max-age=... headers from server response
 *
 * @param TSMBuffer bufp    TS memory buffer
 * @param TSMLoc    hdr_loc TS memory loc
 *
 * @return bool
 */
static bool
isResponseCacheable(TSMBuffer bufp, TSMLoc hdr_loc)
{
  bool cacheable    = false;
  bool found_public = false;
  bool found_maxage = false;
  bool found_expire = false;
  TSMLoc field_loc  = TS_NULL_MLOC;

  if (0 != (field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_EXPIRES, TS_MIME_LEN_EXPIRES))) {
    found_expire = true;
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  }

  if (0 != (field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_CACHE_CONTROL, TS_MIME_LEN_CACHE_CONTROL))) {
    int field_cnt = TSMimeHdrFieldValuesCount(bufp, hdr_loc, field_loc);

    for (int i = 0; i < field_cnt; i++) {
      int len         = 0;
      const char *val = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, i, &len);
      if (0 == i) {
        TSDebug(PLUGIN_NAME, "Cache-Control: %s", val);
      }
      if (len == TS_HTTP_LEN_PUBLIC && 0 == strncasecmp(val, TS_HTTP_VALUE_PUBLIC, TS_HTTP_LEN_PUBLIC)) {
        found_public = true;
      }
      if (len > TS_HTTP_LEN_MAX_AGE && 0 == strncasecmp(val, TS_HTTP_VALUE_MAX_AGE, TS_HTTP_LEN_MAX_AGE)) {
        found_maxage = true;
      }
    }
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  }

  if (found_public && (found_expire || found_maxage)) {
    TSDebug(PLUGIN_NAME, "Response is public cacheable");
    cacheable = true;
  } else {
    TSDebug(PLUGIN_NAME, "Response is non-cacheable");
  }

  return cacheable;
}

/**
 * Retry CacheUrl lock event handler
 *
 * @param TSCont  contp  Continuation ptr
 * @param TSEvent event  TS event
 * @param void    *edata event Data
 *
 * @return int
 */
static int
retryCacheUrlLock(TSCont contp, TSEvent /* event ATS_UNUSED */, void * /* edata ATS_UNUSED */)
{
  TryLockData *data = reinterpret_cast<TryLockData *>(TSContDataGet(contp));
  TSDebug(PLUGIN_NAME, "[%" PRIu64 "] event = %d retry", data->txn_data->seq_id, data->event);
  collapsedConnectionMainHandler(NULL, data->event, data->txn_data->txnp);
  TSfree(data);
  TSContDataSet(contp, NULL);
  TSContDestroy(contp);

  return 0;
}

/**
 * Add TS Cont Schedule to retry mutex lock/update
 *
 * @param CcTxnData *txn_data transaction data
 * @param TSEvent   event     TS Event
 * @param TSHRTime  timeout   schedule timeout
 *
 * @return void
 */
static void
addMutexRetry(CcTxnData *txn_data, TSEvent event, TSHRTime timeout)
{
  TSCont contp      = TSContCreate(retryCacheUrlLock, NULL);
  TryLockData *data = static_cast<TryLockData *>(TSmalloc(sizeof(TryLockData)));

  data->event    = event;
  data->txn_data = txn_data;
  TSContDataSet(contp, data);
  TSContSchedule(contp, timeout, TS_THREAD_POOL_DEFAULT);
}

/**
 * Try to get Collapsed Connection transaction data
 *  if is first entry hook, allocate it
 *  else try to get it back from TxnArg
 *
 * @param TSHttpTxn txnp   Transaction ptr
 * @param bool      create Should create new CcTxnData if NULL
 * @param bool      remap  Calling from remap
 *
 * @return *CcTxnData
 */
static CcTxnData *
getCcTxnData(TSHttpTxn txnp, bool create, bool remap)
{
  CcPluginData *plugin_data = getCcPlugin();
  CcTxnData *txn_data       = NULL;

  txn_data = reinterpret_cast<CcTxnData *>(TSHttpTxnArgGet(txnp, plugin_data->txn_slot));
  if (NULL == txn_data && true == create) {
    txn_data            = static_cast<CcTxnData *>(TSmalloc(sizeof(CcTxnData)));
    txn_data->config    = plugin_data->global_config;
    txn_data->seq_id    = plugin_data->seq_id++;
    txn_data->txnp      = txnp;
    txn_data->contp     = NULL;
    txn_data->hash_key  = 0;
    txn_data->cc_state  = CC_NONE;
    txn_data->wait_time = 0;
    TSHttpTxnArgSet(txnp, plugin_data->txn_slot, txn_data);
    if (remap) {
      TSStatIntIncrement(plugin_data->tol_remap_hook_reqs, 1);
    } else {
      TSStatIntIncrement(plugin_data->tol_global_hook_reqs, 1);
    }
    TSDebug(PLUGIN_NAME, "txn_data created, active_hash_map.size = %zu", plugin_data->active_hash_map->size());
  }

  return txn_data;
}

/**
 * Free Collapsed Connection transaction data
 *
 * @param CcTxnData *txn_data transaction data
 *
 * @return void
 */
static void
freeCcTxnData(CcTxnData *txn_data)
{
  CcPluginData *plugin_data = getCcPlugin();

  if (txn_data->contp) {
    TSContDataSet(txn_data->contp, NULL);
    TSContDestroy(txn_data->contp);
  }
  if (txn_data->txnp) {
    TSHttpTxnArgSet(txn_data->txnp, plugin_data->txn_slot, NULL);
    TSHttpTxnReenable(txn_data->txnp, TS_EVENT_HTTP_CONTINUE);
  }
  TSDebug(PLUGIN_NAME, "[%" PRIu64 "] txn_data released", txn_data->seq_id);
  TSfree(txn_data);
}

/**
 * Lookup CacheUrl in hashTable and lock it in hashTable for collapsed connection
 *
 * @param CcTxnData *txn_data transaction data
 * @param TSEvent   event     TS event
 *
 * @return TSReturnCode
 */
static TSReturnCode
lookupAndTryLockCacheUrl(CcTxnData *txn_data, TSEvent event)
{
  CcTxnState ret;
  CcPluginData *plugin_data = getCcPlugin();

  if (0 == txn_data->hash_key) {
    // New request, check is GET method and gen hash_key
    TSMBuffer bufp = (TSMBuffer)NULL;
    TSMLoc hdr_loc = TS_NULL_MLOC;
    int method_len;
    const char *method = NULL;

    if (TS_SUCCESS != TSHttpTxnClientReqGet(txn_data->txnp, &bufp, &hdr_loc)) {
      TSDebug(PLUGIN_NAME, "unable to get client request");
      freeCcTxnData(txn_data);
      return TS_ERROR;
    }

    if (txn_data->config->required_header_len > 0) {
      TSMLoc field_loc =
        TSMimeHdrFieldFind(bufp, hdr_loc, txn_data->config->required_header, txn_data->config->required_header_len);
      if (!field_loc) {
        TSDebug(PLUGIN_NAME, "%s header not found, ignore it", txn_data->config->required_header);
        TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
        freeCcTxnData(txn_data);
        return TS_SUCCESS;
      }
      TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    }

    method = TSHttpHdrMethodGet(bufp, hdr_loc, &method_len);
    if (TS_HTTP_LEN_GET != method_len || 0 != memcmp(method, TS_HTTP_METHOD_GET, TS_HTTP_LEN_GET)) {
      TSDebug(PLUGIN_NAME, "method is not GET, ignore it");
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      freeCcTxnData(txn_data);
      return TS_SUCCESS;
    }

    txn_data->hash_key = getCacheUrlHashKey(txn_data->txnp, bufp, hdr_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    if (0 == txn_data->hash_key) {
      freeCcTxnData(txn_data);
      return TS_ERROR;
    }
    TSStatIntIncrement(plugin_data->tol_collapsed_reqs, 1);
  }

  /*
   * CC_NONE:   unable to get mutex, try next time
   * CC_PASS:   previous request mark it non-cacheable, pass, do NOT collapse
   * CC_LOCKED: hash entry found, wait for release
   * CC_INSERT: hash entry added, must remove it after receiving response
   */
  ret = insertNewHashEntry(txn_data);
  if (CC_NONE == ret || CC_LOCKED == ret) {
    addMutexRetry(txn_data, event, txn_data->config->insert_lock_retry_time);
  } else if (CC_PASS == ret) {
    TSStatIntIncrement(plugin_data->tol_got_passed_reqs, 1);
    freeCcTxnData(txn_data);
  } else if (CC_INSERT == ret) {
    if (!txn_data->contp) {
      // txn contp is already created from remap, but only global contp for global hook
      txn_data->contp = TSContCreate(collapsedConnectionMainHandler, NULL);
    }
    txn_data->cc_state = ret;
    TSHttpTxnHookAdd(txn_data->txnp, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, txn_data->contp);
    TSHttpTxnHookAdd(txn_data->txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, txn_data->contp);
    TSHttpTxnHookAdd(txn_data->txnp, TS_HTTP_TXN_CLOSE_HOOK, txn_data->contp);
    TSHttpTxnReenable(txn_data->txnp, TS_EVENT_HTTP_CONTINUE);
  } else {
    TSAssert(!"Unexpected return code");
  }

  return TS_SUCCESS;
}

/**
 * Test origin server response is STATUS_OK & Cacheable or not
 *  if ! STATUS_OK, try to remove it from hashTable
 *  if ! cacheable, try to mark it uncacheable in hashTable to let further request pass
 *  if read_while_writer, try to remove it from hashTable
 *
 * @param CcTxnData *txn_data transaction data
 *
 * @return TSReturnCode
 */
static TSReturnCode
testResponseCacheable(CcTxnData *txn_data)
{
  TSMBuffer bufp = (TSMBuffer)NULL;
  TSMLoc hdr_loc = TS_NULL_MLOC;
  TSHttpStatus resp_status;

  if (0 == txn_data->hash_key) {
    return TS_ERROR;
  }

  if (TS_SUCCESS != TSHttpTxnServerRespGet(txn_data->txnp, &bufp, &hdr_loc)) {
    TSDebug(PLUGIN_NAME, "unable to get server response");
    return TS_ERROR;
  }
  resp_status = TSHttpHdrStatusGet(bufp, hdr_loc);

  if (TS_HTTP_STATUS_OK != resp_status) {
    TSDebug(PLUGIN_NAME, "[%" PRIu64 "] response status is not 200 OK, ignore it", txn_data->seq_id);
    txn_data->cc_state = CC_REMOVE;
  } else {
    CcPluginData *plugin_data = getCcPlugin();
    if (!isResponseCacheable(bufp, hdr_loc)) {
      TSDebug(PLUGIN_NAME, "[%" PRIu64 "] response is not public cacheable, let all requests pass", txn_data->seq_id);

      txn_data->cc_state = CC_PASS;
      TSStatIntIncrement(plugin_data->tol_non_cacheable_reqs, 1);
    } else if (plugin_data->read_while_writer) {
      txn_data->cc_state = CC_REMOVE;
    }
  }
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

  if (CC_PASS == txn_data->cc_state || CC_REMOVE == txn_data->cc_state) {
    if (TS_SUCCESS != updateOrRemoveHashEntry(txn_data)) {
      // It's ok to unable to update/remove it here, can update it at next stage
      TSHttpTxnHookAdd(txn_data->txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, txn_data->contp);
    }
  }

  return TS_SUCCESS;
}

/**
 * Test cache lookup result is hit_fresh or not
 *  if hit_fresh, try to remove it from hashTable
 *
 * @param CcTxnData *txn_data transaction data
 *
 * @return TSReturnCode
 */
static TSReturnCode
testCacheLookupResult(CcTxnData *txn_data)
{
  int status = 0;

  if (TS_SUCCESS != TSHttpTxnCacheLookupStatusGet(txn_data->txnp, &status)) {
    TSDebug(PLUGIN_NAME, "unable to get cache lookup result");
    return TS_ERROR;
  }

  if (TS_CACHE_LOOKUP_HIT_FRESH == status || TS_CACHE_LOOKUP_SKIPPED == status) {
    if (TS_CACHE_LOOKUP_HIT_FRESH == status) {
      TSDebug(PLUGIN_NAME, "[%" PRIu64 "] cache lookup hit fresh", txn_data->seq_id);
    } else if (TS_CACHE_LOOKUP_SKIPPED == status) {
      // client request is not lookupable(no-cache) or in proxy mode only
      TSDebug(PLUGIN_NAME, "[%" PRIu64 "] cache lookup skipped", txn_data->seq_id);
    }
    txn_data->cc_state = CC_REMOVE;
    // whether success or not, we'll remove it at TXN_CLOSE stage anyway
    updateOrRemoveHashEntry(txn_data);
  }

  return TS_SUCCESS;
}

/**
 * CollapsedConnection event handler
 *
 * @param TSCont  contp  Continuation ptr
 * @param TSEvent event  TS event
 * @param void    *edata TS Http transaction
 *
 * @return int
 */
static int
collapsedConnectionMainHandler(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  TSHttpTxn txnp      = reinterpret_cast<TSHttpTxn>(edata);
  CcTxnData *txn_data = getCcTxnData(txnp, TS_EVENT_HTTP_POST_REMAP == event, false);

  if (NULL != txn_data) {
    TSDebug(PLUGIN_NAME, "[%" PRIu64 "], event = %d, txn_data-> hash_key = %u, cc_state = %d", txn_data->seq_id, event,
            txn_data->hash_key, txn_data->cc_state);

    switch (event) {
    case TS_EVENT_HTTP_POST_REMAP:
      // hook in global but disabled in remap
      if (0 == txn_data->config->enabled) {
        freeCcTxnData(txn_data);
        return TS_SUCCESS;
      }
      lookupAndTryLockCacheUrl(txn_data, event);
      break;
    case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
      testCacheLookupResult(txn_data);
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
      break;
    case TS_EVENT_HTTP_READ_RESPONSE_HDR:
      testResponseCacheable(txn_data);
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
      break;
    case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
      // whether success or not, we'll remove it at TXN_CLOSE stage anyway
      updateOrRemoveHashEntry(txn_data);
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
      break;
    case TS_EVENT_HTTP_TXN_CLOSE:
      if (CC_DONE == txn_data->cc_state) {
        freeCcTxnData(txn_data);
        txn_data = NULL;
        TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
      } else if (CC_PASS == txn_data->cc_state || CC_PASSED == txn_data->cc_state) {
        // keep pass sentinel for config->keep_pass_record_time
        if (txn_data->config->keep_pass_record_time > 0) {
          if (CC_PASS == txn_data->cc_state && TS_SUCCESS != updateOrRemoveHashEntry(txn_data)) {
            addMutexRetry(txn_data, event, 0);
          } else {
            freeCcTxnData(txn_data);
            txn_data = NULL;
          }
        } else {
          txn_data->cc_state = CC_REMOVE;
        }
      }
      if (txn_data && (CC_INSERT == txn_data->cc_state || CC_REMOVE == txn_data->cc_state)) {
        txn_data->cc_state = CC_REMOVE;
        if (TS_SUCCESS == updateOrRemoveHashEntry(txn_data)) {
          freeCcTxnData(txn_data);
        } else {
          // We're at the last stage, must remove hashEntry anyway
          addMutexRetry(txn_data, event, 0);
        }
      }
      break;
    default:
      TSAssert(!"Unexpected event");
      break;
    }
  } else {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  }

  return 0;
}

/**
 * Try to get Collapsed Connection plugin data
 *  if is first entry hook, allocate it
 *
 * @return *CcPluginData
 */
static CcPluginData *
getCcPlugin()
{
  static CcPluginData *data = NULL;

  if (NULL == data) {
    TSMgmtInt read_while_writer = 0;
    data                        = static_cast<CcPluginData *>(TSmalloc(sizeof(CcPluginData)));
    data->mutex                 = TSMutexCreate();
    data->active_hash_map       = new UintMap();
    data->keep_pass_list        = new UsecList();
    data->seq_id                = 0;
    data->global_config         = NULL;
    TSHttpArgIndexReserve(PLUGIN_NAME, "reserve txn_data slot", &(data->txn_slot));

    if (TS_SUCCESS == TSMgmtIntGet("proxy.config.cache.enable_read_while_writer", &read_while_writer) && read_while_writer > 0) {
      data->read_while_writer = true;
    }

    data->tol_global_hook_reqs =
      TSStatCreate("collapsed_connection.total.global.reqs", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
    data->tol_remap_hook_reqs =
      TSStatCreate("collapsed_connection.total.remap.reqs", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
    data->tol_collapsed_reqs =
      TSStatCreate("collapsed_connection.total.collapsed.reqs", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
    data->tol_non_cacheable_reqs =
      TSStatCreate("collapsed_connection.total.noncacheable.reqs", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
    data->tol_got_passed_reqs =
      TSStatCreate("collapsed_connection.total.got_passed.reqs", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
    data->cur_hash_entries =
      TSStatCreate("collapsed_connection.current.hash.entries", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
    data->cur_keep_pass_entries = TSStatCreate("collapsed_connection.current.keep_pass.entries", TS_RECORDDATATYPE_INT,
                                               TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
    data->max_hash_entries =
      TSStatCreate("collapsed_connection.max.hash.entries", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
    data->max_keep_pass_entries =
      TSStatCreate("collapsed_connection.max.keep_pass.entries", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  }

  return data;
}

///////////////////////////////////////////////////////////////////////////////
// Initialize the TSRemapAPI plugin.
//
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  if (!api_info) {
    strncpy(errbuf, "[TSRemapInit] - Invalid TSRemapInterface argument", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->size < sizeof(TSRemapInterface)) {
    strncpy(errbuf, "[TSRemapInit] - Incorrect size of TSRemapInterface structure", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size - 1, "[TSRemapInit] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
             (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  CcPluginData *plugin_data = getCcPlugin();

  TSDebug(PLUGIN_NAME, "Remap plugin is succesfully initialized, txn_slot = %d", plugin_data->txn_slot);
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *, int)
{
  if (argc > 2) {
    *ih = static_cast<CcPluginConfig *>(initConfig(argv[2]));
  } else {
    *ih = static_cast<CcPluginConfig *>(initConfig(NULL));
  }

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  CcPluginConfig *config = static_cast<CcPluginConfig *>(ih);

  if (NULL != config->required_header) {
    TSfree(config->required_header);
  }

  TSfree(config);
}

///////////////////////////////////////////////////////////////////////////////
// This is the main "entry" point for the plugin, called for every request.
//
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo * /* rri ATS_UNUSED */)
{
  TSHttpTxn txnp            = static_cast<TSHttpTxn>(rh);
  CcPluginData *plugin_data = getCcPlugin();
  CcTxnData *txn_data       = getCcTxnData(txnp, true, true);

  txn_data->config = reinterpret_cast<CcPluginConfig *>(ih);

  if (!plugin_data->global_config || !plugin_data->global_config->enabled) {
    if (txn_data->config->enabled) {
      TSCont contp = TSContCreate(collapsedConnectionMainHandler, NULL);
      TSHttpTxnHookAdd(txnp, TS_HTTP_POST_REMAP_HOOK, contp);

      txn_data->contp = contp;
      TSHttpTxnArgSet(txnp, plugin_data->txn_slot, txn_data);
    } else {
      // global & remap were both disabled
      txn_data->txnp = NULL;
      freeCcTxnData(txn_data);
    }
  } else {
    // if globally enabled, set txn_data for remap config
    TSHttpTxnArgSet(txnp, plugin_data->txn_slot, txn_data);
  }

  return TSREMAP_NO_REMAP;
}

///////////////////////////////////////////////////////////////////////////////
// Initialize the TSAPI plugin for the global hooks we support.
//
void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  TSMgmtInt http_cache = 0;
  TSCont contp         = NULL;

  info.plugin_name   = const_cast<char *>(PLUGIN_NAME);
  info.vendor_name   = const_cast<char *>(PLUGIN_VENDOR);
  info.support_email = const_cast<char *>(PLUGIN_SUPPORT);

  if (TS_SUCCESS != TSPluginRegister(&info)) {
    TSError("[collapsed_connection] Plugin registration failed");
    return;
  }

  if (TS_SUCCESS != TSMgmtIntGet("proxy.config.http.cache.http", &http_cache) || 0 == http_cache) {
    TSError("[collapsed_connection] Http cache is disabled, plugin would not work");
    return;
  }

  if (!(contp = TSContCreate(collapsedConnectionMainHandler, NULL))) {
    TSError("[collapsed_connection] Could not create continuation");
    return;
  }

  CcPluginData *plugin_data = getCcPlugin();
  if (argc > 1) {
    plugin_data->global_config = initConfig(argv[1]);
  } else {
    plugin_data->global_config = initConfig(NULL);
  }

  if (plugin_data->global_config->enabled) {
    // Last API hook before cache lookup
    TSHttpHookAdd(TS_HTTP_POST_REMAP_HOOK, contp);
    TSDebug(PLUGIN_NAME, "TS_HTTP_POST_REMAP_HOOK added, txn_slot = %d", plugin_data->txn_slot);
  } else {
    TSDebug(PLUGIN_NAME, "plugin generally disabled");
  }
}
