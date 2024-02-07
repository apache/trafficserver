/** @file

  Implements RFC 5861 (HTTP Cache-Control Extensions for Stale Content)

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

#include "ts/apidefs.h"
#include "ts_wrap.h"
#include "ts/ts.h"

#include <cstdint>
#include <map>

struct BodyData;

using UintBodyMap = std::map<uint32_t, BodyData *>;

const unsigned int c_hashSeed = 99991;
extern const char PLUGIN_TAG[];
extern const char PLUGIN_TAG_BAD[];

EXT_DBG_CTL(PLUGIN_TAG)
EXT_DBG_CTL(PLUGIN_TAG_BAD)

struct LogInfo {
  TSTextLogObject object      = nullptr;
  bool all                    = false;
  bool stale_if_error         = false;
  bool stale_while_revalidate = false;
  char const *filename        = PLUGIN_TAG;
};

struct ConfigInfo {
  ConfigInfo() : body_data{new UintBodyMap()}, body_data_mutex(TSMutexCreate()) {}
  UintBodyMap *body_data = nullptr;
  TSMutex body_data_mutex;
  int64_t body_data_memory_usage = 0;
  int txn_slot                   = 0;

  bool intercept_reroute             = false;
  bool force_parallel_async          = false;
  int64_t max_body_data_memory_usage = c_default_max_body_data_memory_usage;

  time_t stale_if_error_override         = 0;
  time_t stale_while_revalidate_override = 0;
  time_t stale_if_error_default          = 0;
  time_t stale_while_revalidate_default  = 0;

  int rfc_stat_swr_hit         = 0;
  int rfc_stat_swr_hit_skip    = 0;
  int rfc_stat_swr_miss_locked = 0;
  int rfc_stat_sie_hit         = 0;
  int rfc_stat_memory_over     = 0;

  LogInfo log_info;

private:
  static constexpr int64_t c_default_max_body_data_memory_usage = 1024 * 1024 * 1024; // default to 1 GB
};

struct CachedHeaderInfo {
  time_t date;
  time_t stale_while_revalidate;
  time_t stale_if_error;
  time_t max_age;
};

struct RequestInfo {
  char *effective_url;
  int effective_url_length;
  TSMBuffer http_hdr_buf;
  TSMLoc http_hdr_loc;
  struct sockaddr *client_addr;
  uint32_t key_hash;
};

struct ResponseInfo {
  TSMBuffer http_hdr_buf;
  TSMLoc http_hdr_loc;
  TSHttpParser parser;
  bool parsed;
  TSHttpStatus status;
};

struct StateInfo {
  StateInfo(TSHttpTxn txnp, TSCont contp)
    : txnp{txnp}, transaction_contp{contp}, plugin_config{static_cast<ConfigInfo *>(TSContDataGet(contp))}
  {
    time(&this->txn_start);
  }
  TSHttpTxn txnp                      = nullptr;
  TSCont transaction_contp            = nullptr;
  bool swr_active                     = false;
  bool sie_active                     = false;
  bool over_max_memory                = false;
  TSIOBuffer req_io_buf               = nullptr;
  TSIOBuffer resp_io_buf              = nullptr;
  TSIOBufferReader req_io_buf_reader  = nullptr;
  TSIOBufferReader resp_io_buf_reader = nullptr;
  TSVIO r_vio                         = nullptr;
  TSVIO w_vio                         = nullptr;
  TSVConn vconn                       = nullptr;
  RequestInfo *req_info               = nullptr;
  ResponseInfo *resp_info             = nullptr;
  time_t txn_start                    = 0;
  ConfigInfo *plugin_config           = nullptr;
  char *pristine_url                  = nullptr;
  BodyData *sie_body                  = nullptr;
  BodyData *cur_save_body             = nullptr;
  bool intercept_request              = false;
};

BodyData *async_check_active(uint32_t key_hash, ConfigInfo *plugin_config);
bool async_check_and_add_active(uint32_t key_hash, ConfigInfo *plugin_config);
bool async_remove_active(uint32_t key_hash, ConfigInfo *plugin_config);

// 500, 502, 503, 504
inline bool
valid_sie_status(TSHttpStatus status)
{
  return ((status == 500) || ((status >= 502) && (status <= 504)));
}

/*-----------------------------------------------------------------------------------------------*/
