/** @file

  Clocked Least Frequently Used by Size (CLFUS) RAM cache replacement policy.

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

// See https://cwiki.apache.org/confluence/display/TS/RamCache

#include "P_RamCache.h"

#include "iocore/eventsystem/IOBuffer.h"
#include "tscore/CryptoHash.h"
#include "tscore/List.h"

#include <climits>
#include <cstdint>

class EThread;
class StripeSM;

struct RamCacheCLFUSEntry {
  CryptoHash key;
  uint64_t   auxkey;
  uint64_t   hits;
  uint32_t   size; // memory used including padding in buffer
  uint32_t   len;  // actual data length
  uint32_t   compressed_len;
  union {
    struct {
      uint32_t compressed     : 3; // compression type
      uint32_t incompressible : 1;
      uint32_t lru            : 1;
      uint32_t copy           : 1; // copy-in-copy-out
    } flag_bits;
    uint32_t flags;
  };
  LINK(RamCacheCLFUSEntry, lru_link);
  LINK(RamCacheCLFUSEntry, hash_link);
  Ptr<IOBufferData> data;
};

class RamCacheCLFUS : public RamCache
{
public:
  RamCacheCLFUS() {}

  // returns 1 on found/stored, 0 on not found/stored, if provided auxkey1 and auxkey2 must match
  int     get(CryptoHash *key, Ptr<IOBufferData> *ret_data, uint64_t auxkey = 0) override;
  int     put(CryptoHash *key, IOBufferData *data, uint32_t len, bool copy = false, uint64_t auxkey = 0) override;
  int     fixup(const CryptoHash *key, uint64_t old_auxkey, uint64_t new_auxkey) override;
  int64_t size() const override;

  void init(int64_t max_bytes, StripeSM *stripe) override;

  void compress_entries(EThread *thread, int do_at_most = INT_MAX);

  // TODO move it to private.
  StripeSM *stripe = nullptr; // for stats
private:
  int64_t _max_bytes = 0;
  int64_t _bytes     = 0;
  int64_t _objects   = 0;

  double  _average_value                        = 0;
  int64_t _history                              = 0;
  int     _ibuckets                             = 0;
  int     _nbuckets                             = 0;
  DList(RamCacheCLFUSEntry, hash_link) *_bucket = nullptr;
  Que(RamCacheCLFUSEntry, lru_link) _lru[2];
  uint16_t           *_seen        = nullptr;
  int                 _ncompressed = 0;
  RamCacheCLFUSEntry *_compressed  = nullptr; // first uncompressed lru[0] entry

  void                _resize_hashtable();
  void                _victimize(RamCacheCLFUSEntry *e);
  void                _move_compressed(RamCacheCLFUSEntry *e);
  RamCacheCLFUSEntry *_destroy(RamCacheCLFUSEntry *e);
  void                _requeue_victims(Que(RamCacheCLFUSEntry, lru_link) & victims);
  void                _tick(); // move CLOCK on history
};
