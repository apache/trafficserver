/** @file

  A brief file description

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

#include "tscore/CryptoHash.h"

#define CACHE_INIT_FAILED -1
#define CACHE_INITIALIZING 0
#define CACHE_INITIALIZED 1

#define CACHE_ALT_INDEX_DEFAULT -1
#define CACHE_ALT_REMOVED -2

static const uint8_t CACHE_DB_MAJOR_VERSION = 24;
static const uint8_t CACHE_DB_MINOR_VERSION = 2;
// This is used in various comparisons because otherwise if the minor version is 0,
// the compile fails because the condition is always true or false. Running it through
// VersionNumber prevents that.
extern const VersionNumber CACHE_DB_VERSION;

static const uint8_t CACHE_DIR_MAJOR_VERSION = 18;
static const uint8_t CACHE_DIR_MINOR_VERSION = 0;

#define CACHE_DB_FDS 128

// opcodes
#define CACHE_OPEN_READ 1
#define CACHE_OPEN_READ_BUFFER 2
#define CACHE_OPEN_READ_LONG 3
#define CACHE_OPEN_READ_BUFFER_LONG 4
#define CACHE_OPEN_WRITE 5
#define CACHE_OPEN_WRITE_BUFFER 6
#define CACHE_OPEN_WRITE_LONG 7
#define CACHE_OPEN_WRITE_BUFFER_LONG 8
#define CACHE_UPDATE 9
#define CACHE_REMOVE 10
#define CACHE_LINK 11
#define CACHE_DEREF 12
#define CACHE_LOOKUP_OP 13

enum CacheType {
  CACHE_NONE_TYPE = 0, // for empty disk fragments
  CACHE_HTTP_TYPE = 1,
  CACHE_RTSP_TYPE = 2
};

// NOTE: All the failures are ODD, and one greater than the success
//       Some of these must match those in <ts/ts.h>
enum CacheEventType {
  CACHE_EVENT_LOOKUP           = CACHE_EVENT_EVENTS_START + 0,
  CACHE_EVENT_LOOKUP_FAILED    = CACHE_EVENT_EVENTS_START + 1,
  CACHE_EVENT_OPEN_READ        = CACHE_EVENT_EVENTS_START + 2,
  CACHE_EVENT_OPEN_READ_FAILED = CACHE_EVENT_EVENTS_START + 3,
  // 4-7 unused
  CACHE_EVENT_OPEN_WRITE        = CACHE_EVENT_EVENTS_START + 8,
  CACHE_EVENT_OPEN_WRITE_FAILED = CACHE_EVENT_EVENTS_START + 9,
  CACHE_EVENT_REMOVE            = CACHE_EVENT_EVENTS_START + 12,
  CACHE_EVENT_REMOVE_FAILED     = CACHE_EVENT_EVENTS_START + 13,
  CACHE_EVENT_UPDATE,
  CACHE_EVENT_UPDATE_FAILED,
  CACHE_EVENT_LINK,
  CACHE_EVENT_LINK_FAILED,
  CACHE_EVENT_DEREF,
  CACHE_EVENT_DEREF_FAILED,
  CACHE_EVENT_SCAN                   = CACHE_EVENT_EVENTS_START + 20,
  CACHE_EVENT_SCAN_FAILED            = CACHE_EVENT_EVENTS_START + 21,
  CACHE_EVENT_SCAN_OBJECT            = CACHE_EVENT_EVENTS_START + 22,
  CACHE_EVENT_SCAN_OPERATION_BLOCKED = CACHE_EVENT_EVENTS_START + 23,
  CACHE_EVENT_SCAN_OPERATION_FAILED  = CACHE_EVENT_EVENTS_START + 24,
  CACHE_EVENT_SCAN_DONE              = CACHE_EVENT_EVENTS_START + 25,
  //////////////////////////
  // Internal error codes //
  //////////////////////////
  CACHE_EVENT_RESPONSE = CACHE_EVENT_EVENTS_START + 50,
  CACHE_EVENT_RESPONSE_MSG,
  CACHE_EVENT_RESPONSE_RETRY
};

enum CacheScanResult {
  CACHE_SCAN_RESULT_CONTINUE = EVENT_CONT,
  CACHE_SCAN_RESULT_DONE     = EVENT_DONE,
  CACHE_SCAN_RESULT_DELETE   = 10,
  CACHE_SCAN_RESULT_DELETE_ALL_ALTERNATES,
  CACHE_SCAN_RESULT_UPDATE,
  CACHE_SCAN_RESULT_RETRY
};

enum CacheDataType {
  CACHE_DATA_HTTP_INFO = VCONNECTION_CACHE_DATA_BASE,
  CACHE_DATA_KEY,
  CACHE_DATA_RAM_CACHE_HIT_FLAG,
};

enum CacheFragType {
  CACHE_FRAG_TYPE_NONE,
  CACHE_FRAG_TYPE_HTTP_V23, ///< DB version 23 or prior.
  CACHE_FRAG_TYPE_RTSP,     ///< Should be removed once Cache Toolkit is implemented.
  CACHE_FRAG_TYPE_HTTP,
  NUM_CACHE_FRAG_TYPES
};

typedef CryptoHash CacheKey;

struct HttpCacheKey {
  int hostlen;
  const char *hostname;
  CacheKey hash;
  CacheKey hash2;
};

#define CACHE_ALLOW_MULTIPLE_WRITES 1
#define CACHE_EXPECTED_SIZE 32768

/* uses of the CacheKey
   word(0) - cache partition segment
   word(1) - cache partition bucket
   word(2) - tag (lower bits), hosttable hash (upper bits)
   word(3) - ram cache hash, lookaside cache
 */
